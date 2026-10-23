#pragma once

#include "rtx_material.hpp"
#include "rtx_tv.hpp"
#include "rtx_nrc.hpp"
#include "rtx_nrd.hpp"
#include <vulkanizer/ddgi.hpp>
#include <vulkanizer/shadow.hpp>
#include <vulkanizer/reflection.hpp>
#include <vulkanizer/graphics_pipeline_builder.hpp>
#include <vulkanizer/descriptor_set_builder.hpp>
#include <vulkanizer/ktx.hpp>
#include <vulkanizer/render.hpp>
#include <vulkanizer/barrier.hpp>
#include <vulkanizer/commands.hpp>
#include <cmrc/cmrc.hpp>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <cmath>
#include <iostream>
#include <glm/gtc/packing.hpp>

CMRC_DECLARE(sponza_lighting_shaders);

struct sponza_lighting_options {
    vkz::path video_path{};
    std::array<bool, 3> full_resolution{};
    bool living_room{};
    bool lights{true};
    bool ceiling_light{true};
    bool standing_light{true};
    bool gi{true};
    bool nrc{};
    bool nrd{};
    bool shadows{true};
    bool reflections{true};
    bool probe_offsetting{true};
    bool visualize_probes{};
    bool indirect_only{};
    bool reflection_only{};
    bool verify_lighting{};
    float gi_multiplier{1};
    float exposure{1};
};

// One instance per eye: depth, motion, and temporal histories must not cross eyes.
class sponza_lighting {
public:
    sponza_lighting(vkz::context &context, vkz::vma_memory_allocator &allocator, VkQueue queue, uint32_t family,
        const vkz::mesh::data &scene, const vkz::buffer &vertices, const vkz::buffer &indices,
        glm::uvec2 resolution, VkFormat format, const sponza_lighting_options &options, rtx::tv_light *tv_light = nullptr)
        : context_(context), allocator_(allocator), ray_allocator_(allocator), device_(allocator.device), queue_(queue),
          queue_family_(family), scene_(scene), vertices_(vertices), indices_(indices), render_size_(resolution),
          color_format_(format), living_room_(options.living_room), full_resolution_(options.full_resolution), visualize_probes_(options.visualize_probes) {
        tv_light_ = tv_light;
        fallback_tv_parameters_ = load_buffer(vkz::buffer_usage_uniform, std::array<float, 20>{}.data(), 80);

        draw_order_.resize(scene_.instances.size());
        std::iota(draw_order_.begin(), draw_order_.end(), 0);
        if (living_room_) {
            for (const auto &light : scene_.lights) {
                if (light.type == vkz::mesh::light_type::point && lights_.size() < 2)
                    lights_.push_back(light);
            }
            if (lights_.size() != 2)
                throw std::runtime_error("Living room requires its two authored point lights");
        } else {
            constexpr glm::vec3 color_5000_k{1.00118284f, 0.97817062f, 0.84464176f};
            lights_ = {{.type = vkz::mesh::light_type::point, .color = color_5000_k * 10.0f,
                .intensity = 10.0f, .range = 5.0f, .position = {0, 1, -2}}};
        }

        if (!options.lights) {
            for (auto &light : lights_)
                light.intensity = 0;
        }

        load_buffers();
        load_textures();
        load_gbuffer();
        load_ddgi(options.probe_offsetting);
        load_shadows();
        load_reflections();
        use_nrc_ = options.nrc;
        if (options.nrc) load_nrc();
        if (options.nrd) {
            load_denoising_textures();
            load_nrd();
        }
        load_descriptor_set_layouts();
        load_descriptor_pool();
        load_descriptors();
        load_pipelines();
    }

    ~sponza_lighting() { destroy(); }

    void record(VkCommandBuffer cmd, const glm::mat4 &view, const glm::mat4 &projection,
        glm::vec3 eye, vkz::image_view target, const sponza_lighting_options &options) {
        camera_.view = view;
        camera_.projection = projection;
        camera_.position = eye;
        camera_.znear = 0.05f;
        camera_.zfar = 1000.0f;
        if (options.nrc && !nrc_) load_nrc();
        use_nrc_ = options.nrc;
        if (options.nrd && !nrd_) {
            load_denoising_textures();
            load_nrd();
        }
        const auto view_projection = projection * view;
        vkz::rtx::scene_uniform frame;
        frame.view_projection = view_projection;
        frame.eye = glm::vec4(eye, 1);
        frame.vertex_layout.x = int(vkz::rtx::vertex_stride(scene_));
        frame.num_lights = uint32_t(lights_.size());
        frame.enable_ddgi = options.gi;
        frame.enable_shadows = options.shadows;
        frame.enable_reflection = options.reflections;
        frame.indirect_only = options.indirect_only;
        frame.reflection_only = options.reflection_only;
        frame.gi_multiplier = options.gi_multiplier;
        frame.exposure = options.exposure;
        frame.render_resolution = {render_size_.x, render_size_.y};
        write_buffer(scene_buffer_, &frame, sizeof(frame));

        reflections_->new_frame({glm::inverse(view_projection), view_projection, eye, options.reflections, !options.nrd, options.gi && !options.nrc});
        update_lighting_descriptors(options);
        render_gbuffer(cmd);
        shadows_->new_frame({glm::inverse(view_projection), view_projection, eye, !options.nrd});
        if (options.shadows) shadows_->exec(cmd);
        ddgi_->new_frame({glm::inverse(view_projection), eye});
        if ((options.gi && !options.nrc) || visualize_probes_) ddgi_->exec(cmd);
        reflections_->exec(cmd);
        if (options.gi && options.nrc) {
            nrc_->exec(cmd);
            nrc_frame_pending_ = true;
        }
        if (options.nrd) {
            const bool reset = !previous_nrd_ || previous_nrc_ != options.nrc
                || previous_reflections_ != options.reflections;
            const auto inputs = nrd_inputs();
            nrd_->exec(cmd, camera_, inputs, reset);
            nrd_reflection_->exec(cmd, camera_, inputs, reset);
            for (auto &denoiser : nrd_shadows_)
                denoiser->exec(cmd, camera_, inputs, reset);
        }
        previous_nrd_ = options.nrd;
        previous_nrc_ = options.nrc;
        previous_reflections_ = options.reflections;
        render_lighting(cmd);

        vkz::render_info presentation;
        presentation.color_attachments.push_back({target, color_format_});
        presentation.render_area = render_size_;
        vkz::render(cmd, presentation, [&] {
            vkz::set_viewport(cmd, {0, 0, float(render_size_.x), float(render_size_.y), 0, 1});
            vkz::set_scissor(cmd, {{0, 0}, render_size_});
            vkz::bind_pipeline(cmd, tone_mapping_pipeline_);
            vkz::bind_descriptor_sets(cmd, tone_mapping_pipeline_, {scene_set_});
            const uint32_t srgb_attachment = color_format_ == VK_FORMAT_R8G8B8A8_SRGB || color_format_ == VK_FORMAT_B8G8R8A8_SRGB;
            vkz::push_constants(cmd, tone_mapping_pipeline_, srgb_attachment, vkz::shader_stage_fragment);
            vkz::draw(cmd, 3);
        });
    }

    void verify_lighting(const sponza_lighting_options &options) {
        if (tv_light_ && !options.indirect_only && !options.reflection_only)
            tv_light_->verify_lighting(scene_color_, [&](VkCommandBuffer cmd) { render_lighting(cmd); });

        const auto verify = [&](const vkz::texture &texture, const char *name, bool require_light) {
            const auto extent = texture.image.create_info.extent;
            const auto count = size_t(extent.width) * extent.height * extent.depth;
            const bool scalar = texture.image.create_info.format == VK_FORMAT_R16_SFLOAT;
            const bool float32 = texture.image.create_info.format == VK_FORMAT_R32G32B32A32_SFLOAT;
            const size_t channels = scalar ? 1 : 4;
            auto readback =
                vkz::buffer::builder(allocator_)
                    .size(count * channels * (float32 ? sizeof(float) : sizeof(uint16_t)))
                    .usage(vkz::buffer_usage_transfer_dst)
                    .memory_usage(VMA_MEMORY_USAGE_GPU_TO_CPU)
                .build();
            vkz::command_pool commands{device_, queue_family_, vkz::command_pool_flags_transient, queue_};
            const auto cmd = commands.create_command_buffer();
            auto image = texture.image;
            vkz::copy(cmd, image, readback);
            vkz::barrier::push_and_flush(cmd, image, {static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, 0, 1},
                vkz::pipeline_stage_transfer, vkz::pipeline_stage_all_commands,
                vkz::access_transfer_read, vkz::access_shader_read_write, VK_IMAGE_LAYOUT_GENERAL);
            vkz::barrier::gpu_to_cpu(cmd);
            commands.submit_and_wait(cmd);
            VKZ_CHECK_VULKAN(vmaInvalidateAllocation(readback.allocator, readback.allocation, 0, VK_WHOLE_SIZE));
            auto mapping = readback.map();
            const auto *values = mapping.as<uint16_t>();
            float maximum = 0;
            double sum = 0;
            bool valid = true;
            for (size_t i = 0; i < count; ++i) {
                for (size_t channel = 0; channel < (scalar ? 1u : 3u); ++channel) {
                    const float value = float32 ? mapping.as<float>()[i * channels + channel]
                        : glm::unpackHalf1x16(values[i * channels + channel]);
                    valid &= std::isfinite(value) && value >= 0 && (!scalar || value <= 1);
                    maximum = std::max(maximum, value);
                    sum += value;
                }
            }
            mapping.unmap();
            readback.destroy();
            std::cout << name << ": mean=" << sum / (count * (scalar ? 1 : 3)) << ", max=" << maximum << "\n";
            if (!valid || (require_light && maximum <= 0))
                throw std::runtime_error(std::string(name) + " has invalid or empty lighting");
        };
        if (options.gi) verify(options.nrc ? nrc_->indirect_light_texture() : ddgi_->indirect_light_texture(), options.nrc ? "NRC" : "DDGI", true);
        if (options.nrd) {
            if (options.gi) verify(nrd_diffuse_, "NRD diffuse", true);
            if (options.reflections) verify(nrd_specular_, "NRD reflections", false);
            if (options.shadows) verify(nrd_shadow_, "NRD shadows", true);
        }
        if (options.shadows) verify(shadows_->visibility_texture(), "Shadows", true);
        if (options.reflections) verify(reflections_->reflection_texture(), "Reflections", false);
    }

    // NRC EndFrame must follow the Vulkan submission for this eye.
    void submitted() {
        if (nrc_frame_pending_) {
            nrc_->end_frame(queue_);
            nrc_frame_pending_ = false;
        }
    }

    void end_frame() {
        reflections_->end_frame();
        shadows_->end_frame();
        ddgi_->end_frame();
    }

private:
    void destroy() {
        vkz::batch_submission{queue_}.wait_idle();
        nrd_shadows_.clear();
        nrd_reflection_.reset();
        nrd_.reset();
        nrc_.reset();
        for (auto *texture : {&nrd_diffuse_, &nrd_specular_, &nrd_shadow_}) texture->destroy();
        reflections_.reset();
        shadows_.reset();
        ddgi_.reset();
        acceleration_.reset();
        for (auto &pipeline : gbuffer_pipelines_) pipeline.destroy();
        for (auto &pipeline : pipelines_) pipeline.destroy();
        tone_mapping_pipeline_.destroy();
        probe_pipeline_.destroy();
        descriptor_pool_.destroy();
        for (auto &layout : layouts_) layout.destroy();
        for (auto &buffer : material_buffers_) buffer.destroy();
        scene_buffer_.destroy();
        light_buffer_.destroy();
        fallback_tv_.image_view.destroy();
        fallback_tv_parameters_.destroy();
        if (tv_light_)
            textures_.pop_back();

        for (auto &texture : textures_) texture.destroy();
        for (auto *texture : {&scene_depth_, &scene_normal_, &scene_color_, &scene_surface_, &scene_mesh_id_, &scene_color_msaa_, &scene_depth_msaa_}) texture->destroy();
    }

    glm::uvec2 lighting_resolution(size_t effect) const {
        return full_resolution_[effect] ? render_size_ : (render_size_ + 1u) / 2u;
    }

    void load_nrc() {
        auto bounds = scene_.boxes.at(scene_.instances.front().mesh_id).get_transformed(scene_.instances.front().model);
        for (const auto &instance : scene_.instances) {
            const auto box = scene_.boxes.at(instance.mesh_id).get_transformed(instance.model);
            bounds.combine_point(box.min);
            bounds.combine_point(box.max);
        }
        const auto center = (bounds.min + bounds.max) * 0.5f;
        const glm::vec4 cache_bounds{center, std::max(glm::length(bounds.get_size()) * 0.5f, 1.0f)};
        nrc_ = std::make_unique<rtx::nrc_gi>(context_, allocator_, *reflections_, uint32_t(textures_.size()),
            lighting_resolution(0), cache_bounds, queue_, queue_family_);
    }

    rtx::nrd_denoiser::input nrd_inputs() {
        const auto &diffuse = use_nrc_ ? nrc_->indirect_light_texture() : ddgi_->indirect_light_texture();
        return {scene_depth_, scene_normal_, scene_surface_, diffuse, reflections_->raw_texture(), shadows_->penumbra_texture(),
            nrd_diffuse_, nrd_specular_, nrd_shadow_};
    }

    void load_nrd() {
        const auto inputs = nrd_inputs();
        nrd_ = std::make_unique<rtx::nrd_denoiser>(context_, lighting_resolution(0), inputs, -3);
        nrd_reflection_ = std::make_unique<rtx::nrd_denoiser>(context_, lighting_resolution(2), inputs, -2);
        for (uint32_t i = 0; i < lights_.size(); ++i)
            nrd_shadows_.push_back(std::make_unique<rtx::nrd_denoiser>(context_, lighting_resolution(1), inputs, int(i)));
    }

    void update_lighting_descriptors(const sponza_lighting_options &options) {
        const auto &diffuse = options.nrd ? nrd_diffuse_
            : options.nrc ? nrc_->indirect_light_texture() : ddgi_->indirect_light_texture();
        vkz::update_descriptor(device_, {
            .descriptor_set = scene_set_,
            .bindings = {
                vkz::make_descriptor<vkz::texture_descriptor>(diffuse, 3),
                vkz::make_descriptor<vkz::texture_descriptor>(options.nrd ? nrd_shadow_ : shadows_->visibility_texture(), 5),
                vkz::make_descriptor<vkz::texture_descriptor>(options.nrd ? nrd_specular_ : reflections_->reflection_texture(), 6)
            }
        });
    }

    void load_denoising_textures() {
        const auto create = [&](vkz::texture &texture, bool volume, size_t effect) {
            const auto resolution = lighting_resolution(effect);
            const auto format = volume ? VK_FORMAT_R16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;
            texture.image =
                vkz::image::builder(ray_allocator_)
                    .type(volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D)
                    .format(format)
                    .extent(resolution.x, resolution.y, volume ? uint32_t(lights_.size()) : 1u)
                    .usage(vkz::image_usage_sampled_storage_transfer)
                .build();
            texture.image_view =
                vkz::image_view::builder(device_)
                    .image(texture.image)
                    .view_type(volume ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D)
                    .format(format)
                    .aspect_mask(vkz::image_aspect_color)
                    .level_count(1)
                    .layer_count(1)
                .build();
            texture.sampler =
                vkz::sampler::builder(device_)
                    .address_mode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
                .build();
        };
        create(nrd_diffuse_, false, 0);
        create(nrd_specular_, false, 2);
        create(nrd_shadow_, true, 1);
        vkz::command_pool uploads{device_, queue_family_, vkz::command_pool_flags_transient, queue_};
        const auto cmd = uploads.create_command_buffer();
        for (auto *texture : {&nrd_diffuse_, &nrd_specular_, &nrd_shadow_}) {
            vkz::clear(cmd, texture->image, VkClearColorValue{}, {});
            vkz::barrier::push_and_flush(cmd, texture->image, {static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, 0, 1},
                vkz::pipeline_stage_transfer, vkz::pipeline_stage_all_commands,
                vkz::access_transfer_write, vkz::access_shader_read_write, VK_IMAGE_LAYOUT_GENERAL);
        }
        uploads.submit_and_wait(cmd);
    }

    void load_ddgi(bool offsetting) {
        acceleration_ = std::make_unique<vkz::mesh::bvh>(ray_allocator_);
        vkz::command_pool uploads{device_, queue_family_, vkz::command_pool_flags_transient, queue_};
        const auto cmd = uploads.create_command_buffer();
        acceleration_->build(cmd, scene_, {vertices_, indices_});
        uploads.submit_and_wait(cmd);

        ddgi_ = std::make_unique<vkz::ddgi>(device_, ray_allocator_, lights_, scene_depth_, scene_normal_, render_size_);
        vkz::ddgi::settings settings;
        settings.grid_position = {-10, 0.5f, -10};
        settings.half_resolution = !full_resolution_[0];
        settings.relocate_probes = offsetting;
        if (living_room_) {
            auto bounds = scene_.boxes.at(scene_.instances.front().mesh_id).get_transformed(scene_.instances.front().model);
            for (const auto &instance : scene_.instances) {
                const auto box = scene_.boxes.at(instance.mesh_id).get_transformed(instance.model);
                bounds.combine_point(box.min);
                bounds.combine_point(box.max);
            }

            settings.probe_grid.count = {7, 4, 7};
            settings.grid_position = bounds.min + glm::vec3(0.1f);
            settings.probe_grid.spacing = glm::max((bounds.get_size() - glm::vec3(0.2f))
                / glm::vec3(settings.probe_grid.count - glm::ivec3(1)), glm::vec3(0.1f));
            settings.probe_grid.inverse_spacing = 1.0f / settings.probe_grid.spacing;
            settings.self_shadow_bias = 0.05f;
            settings.relocate_probes = offsetting;
            settings.classify_probes = false;
        }

        probe_parameters_ = {settings.grid_position, settings.probe_grid.count, settings.probe_grid.spacing,
            settings.probe_grid.sphere_scale, int(settings.irradiance_side_length), uint32_t(offsetting)};
        ddgi_->configure(settings);
        ddgi_->init({scene_, {vertices_, indices_}, acceleration_->tlas().handle, queue_, queue_family_, textures_});
    }

    void load_shadows() {
        shadows_ = std::make_unique<vkz::shadow>(device_, ray_allocator_, lights_, scene_depth_, scene_normal_, render_size_, !full_resolution_[1]);
        shadows_->init({acceleration_->tlas().handle, queue_, queue_family_, &scene_, {vertices_, indices_}, textures_});
    }

    void load_reflections() {
        reflections_ = std::make_unique<vkz::reflection>(device_, ray_allocator_, lights_, scene_depth_, scene_normal_,
            scene_surface_, scene_mesh_id_, render_size_, *ddgi_, !full_resolution_[2]);
        reflections_->init({scene_, {vertices_, indices_}, acceleration_->tlas().handle, queue_, queue_family_, textures_});
    }

    void draw_scene(VkCommandBuffer cmd, bool gbuffer) {
        vkz::set_viewport(cmd, {0, 0, float(render_size_.x), float(render_size_.y), 0, 1});
        vkz::set_scissor(cmd, {{0, 0}, render_size_});
        vkz::bind_index_buffer<uint32_t>(cmd, indices_);

        auto order = draw_order_;
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            const auto &ia = scene_.instances[a], &ib = scene_.instances[b];
            const bool ba = scene_.materials[vkz::rtx::material_index(scene_, ia)].alpha_mode == vkz::mesh::alpha_mode::blend;
            const bool bb = scene_.materials[vkz::rtx::material_index(scene_, ib)].alpha_mode == vkz::mesh::alpha_mode::blend;
            if (ba != bb) return !ba;
            if (!ba) return false;
            const auto depth = [&](const auto &instance) {
                return -(camera_.view * instance.model * glm::vec4(scene_.boxes.at(instance.mesh_id).get_center(), 1)).z;
            };
            return depth(ia) > depth(ib);
        });

        for (const auto id : order) {
            const auto &instance = scene_.instances[id];
            const auto &mesh = scene_.meshes.at(instance.mesh_id);
            const auto material = vkz::rtx::material_index(scene_, instance);
            const auto pipeline_id = (scene_.materials[material].alpha_mode == vkz::mesh::alpha_mode::blend ? 2 : 0)
                + (glm::determinant(glm::mat3(instance.model)) < 0 ? 1 : 0);

            if (gbuffer && (pipeline_id & 2))
                continue;

            const auto &pipeline = gbuffer ? gbuffer_pipelines_[pipeline_id & 1] : pipelines_[pipeline_id];
            vkz::bind_pipeline(cmd, pipeline);
            vkz::bind_descriptor_sets(cmd, pipeline, {scene_set_, material_sets_[material], reflections_->tracing_resources().sets[1]});
            vkz::push_constants(cmd, pipeline, vkz::rtx::instance_uniform{instance.model, glm::transpose(instance.model_inverse)},
                vkz::shader_stage_vertex | vkz::shader_stage_fragment);
            vkz::draw_indexed(cmd, mesh.get_lod_indices_count(0), 1, mesh.index_offset + mesh.lod_offset[0], int32_t(mesh.vertex_offset), id);
        }
    }

    void write_buffer(vkz::buffer &buffer, const void *data, size_t size) {
        auto mapping = buffer.map();
        std::memcpy(mapping.as<void>(), data, size);
        buffer.flush();
        mapping.unmap();
    }

    vkz::buffer load_buffer(vkz::buffer_usage usage, const void *data, size_t size) {
        auto buffer =
            vkz::buffer::builder(ray_allocator_)
                .usage(usage)
                .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
                .size(size)
            .build();

        if (data)
            write_buffer(buffer, data, size);

        return buffer;
    }

    void load_buffers() {
        scene_buffer_ = load_buffer(vkz::buffer_usage_uniform, nullptr, sizeof(vkz::rtx::scene_uniform));
        const auto lights = vkz::rtx::make_light_uniforms(lights_);
        light_buffer_ = load_buffer(vkz::buffer_usage_storage, lights.data(), lights.size() * sizeof(vkz::rtx::light_uniform));
        for (const auto &material : scene_.materials) {
            const auto uniform = vkz::rtx::make_material_uniform(scene_, material);
            material_buffers_.push_back(load_buffer(vkz::buffer_usage_uniform, &uniform, sizeof(uniform)));
        }
    }

    void load_textures() {
        textures_.resize(1);
        auto &white = textures_.front();
        white.image =
            vkz::image::builder(allocator_)
                .format(VK_FORMAT_R8G8B8A8_UNORM)
                .extent(1, 1)
                .usage(vkz::image_usage_sampled_upload)
            .build();

        white.image_view =
            vkz::image_view::builder(device_)
                .image(white.image)
                .format(VK_FORMAT_R8G8B8A8_UNORM)
                .aspect_mask(vkz::image_aspect_color)
                .level_count(1)
                .layer_count(1)
            .build();

        white.sampler =
            vkz::sampler::builder(device_)
            .build();

        vkz::command_pool uploads{device_, queue_family_, vkz::command_pool_flags_transient, queue_};
        const auto cmd = uploads.create_command_buffer();
        vkz::clear(cmd, white.image, VkClearColorValue{{1, 1, 1, 1}}, {});
        vkz::barrier::push_and_flush(cmd, white.image, {static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, 0, 1},
            vkz::pipeline_stage_transfer, vkz::pipeline_stage_fragment_shader,
            vkz::access_transfer_write, vkz::access_shader_read, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        uploads.submit_and_wait(cmd);

        fallback_tv_ = white;
        fallback_tv_.image_view =
            vkz::image_view::builder(device_)
                .image(white.image)
                .view_type(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
                .format(VK_FORMAT_R8G8B8A8_UNORM)
                .subresource_range({static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, 0, 1})
            .build();

        for (size_t i = 0; i < scene_.textures.size(); ++i) {
            if (tv_light_ && i + 1 == scene_.textures.size()) {
                textures_.push_back(tv_light_->screen());
                continue;
            }

            const auto &texture = scene_.textures[i];
            const auto settings = texture.sampler_id < 0 ? vkz::mesh::sampler{} : scene_.samplers.at(texture.sampler_id);
            textures_.push_back(vkz::texture_from_ktx(scene_.images.at(texture.image_id).encoded_data,
                allocator_, queue_, queue_family_, settings));
        }
    }

    void load_descriptor_set_layouts() {
        layouts_[0] =
            vkz::descriptor_set_layout_builder{device_}
                .binding(0)
                    .descriptor_type(vkz::descriptor_type_uniform_buffer)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_vertex | vkz::shader_stage_fragment)
                .binding(1)
                    .descriptor_type(vkz::descriptor_type_storage_buffer)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_vertex)
                .binding(2)
                    .descriptor_type(vkz::descriptor_type_storage_buffer)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(3)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(4)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(5)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(6)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(7)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(8)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_vertex)
                .binding(9)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(10)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(11)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(12)
                    .descriptor_type(vkz::descriptor_type_uniform_buffer)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
            .create_layout();

        layouts_[1] =
            vkz::descriptor_set_layout_builder{device_}
                .binding(0)
                    .descriptor_type(vkz::descriptor_type_uniform_buffer)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_vertex | vkz::shader_stage_fragment)
                .binding(1)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(2)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(3)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(4)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(5)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(6)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(7)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(8)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(9)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(10)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(11)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(12)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(13)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(14)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(15)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(16)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
                .binding(17)
                    .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                    .descriptor_count(1)
                    .shader_stages(vkz::shader_stage_fragment)
            .create_layout();

    }

    void load_descriptor_pool() {
        const auto count = uint32_t(scene_.materials.size());
        descriptor_pool_ = vkz::descriptor_pool{device_, count + 1, {
            {static_cast<VkDescriptorType>(vkz::descriptor_type_uniform_buffer), count + 2},
            {static_cast<VkDescriptorType>(vkz::descriptor_type_storage_buffer), 2},
            {static_cast<VkDescriptorType>(vkz::descriptor_type_combined_image_sampler), count * uint32_t(vkz::mesh::material_texture_count) + 11}
        }};
    }

    void load_descriptors() {
        scene_set_ = descriptor_pool_.allocate(layouts_[0]);
        material_sets_ = descriptor_pool_.allocate_n(layouts_[1], scene_.materials.size());

        vkz::update_descriptor(device_, {
            .descriptor_set = scene_set_,
            .bindings = {
                vkz::make_descriptor<vkz::ubo_descriptor>(scene_buffer_, 0),
                vkz::make_descriptor<vkz::buffer_descriptor>(vertices_, 1),
                vkz::make_descriptor<vkz::buffer_descriptor>(light_buffer_, 2),
                vkz::make_descriptor<vkz::texture_descriptor>(ddgi_->indirect_light_texture(), 3),
                vkz::texture_descriptor{scene_color_.image_view, scene_color_.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 4},
                vkz::make_descriptor<vkz::texture_descriptor>(shadows_->visibility_texture(), 5),
                vkz::make_descriptor<vkz::texture_descriptor>(reflections_->reflection_texture(), 6),
                vkz::make_descriptor<vkz::texture_descriptor>(ddgi_->irradiance_atlas(), 7),
                vkz::make_descriptor<vkz::texture_descriptor>(ddgi_->sampling_resources().offset, 8)
            }
        });

        vkz::update_descriptor(device_, {.descriptor_set = scene_set_, .bindings = {
            vkz::make_descriptor<vkz::texture_descriptor>(tv_light_ ? tv_light_->filtered() : fallback_tv_, 9),
            vkz::make_descriptor<vkz::texture_descriptor>(tv_light_ ? tv_light_->ltc_matrix() : textures_.front(), 10),
            vkz::make_descriptor<vkz::texture_descriptor>(tv_light_ ? tv_light_->ltc_amplitude() : textures_.front(), 11),
            vkz::make_descriptor<vkz::ubo_descriptor>(tv_light_ ? tv_light_->parameters() : fallback_tv_parameters_, 12)}});

        for (size_t i = 0; i < scene_.materials.size(); ++i) {
            vkz::descriptor_bindings bindings{
                .descriptor_set = material_sets_[i],
                .bindings = {vkz::make_descriptor<vkz::ubo_descriptor>(material_buffers_[i], 0)}
            };

            const auto slots = vkz::mesh::texture_slots(scene_.materials[i]);

            for (uint32_t slot = 0; slot < slots.size(); ++slot) {
                const auto &texture = textures_.at(slots[slot]->index + 1);
                bindings.bindings.push_back(vkz::make_descriptor<vkz::texture_descriptor>(texture, slot + 1));
            }

            vkz::update_descriptor(device_, std::move(bindings));
        }
    }

    void load_gbuffer() {
        const auto create = [&](vkz::texture &target, VkFormat format, vkz::image_usage usage, vkz::image_aspect aspect, vkz::sample_count samples = vkz::sample_count_x1) {
            target.image =
                vkz::image::builder(allocator_)
                    .format(format)
                    .extent(render_size_.x, render_size_.y)
                    .samples(samples)
                    .usage(usage | (samples == vkz::sample_count_x1 ? vkz::image_usage_sampled : vkz::image_usage_none))
                .build();

            target.image_view =
                vkz::image_view::builder(device_)
                    .image(target.image)
                    .format(format)
                    .aspect_mask(aspect)
                    .level_count(1)
                    .layer_count(1)
                .build();
        };

        create(scene_depth_, VK_FORMAT_D32_SFLOAT, vkz::image_usage_depth_stencil, vkz::image_aspect_depth);
        create(scene_normal_, VK_FORMAT_R16G16_SFLOAT, vkz::image_usage_color, vkz::image_aspect_color);
        create(scene_surface_, VK_FORMAT_R16G16B16A16_SFLOAT, vkz::image_usage_color, vkz::image_aspect_color);
        create(scene_mesh_id_, VK_FORMAT_R32_UINT, vkz::image_usage_color, vkz::image_aspect_color);
        create(scene_color_, VK_FORMAT_R16G16B16A16_SFLOAT, vkz::image_usage_color_download, vkz::image_aspect_color);

        create(scene_color_msaa_, VK_FORMAT_R16G16B16A16_SFLOAT, vkz::image_usage_color, vkz::image_aspect_color, vkz::sample_count_x4);
        create(scene_depth_msaa_, VK_FORMAT_D32_SFLOAT, vkz::image_usage_depth_stencil, vkz::image_aspect_depth, vkz::sample_count_x4);

        scene_color_.sampler =
            vkz::sampler::builder(device_)
            .build();
    }

    void render_gbuffer(VkCommandBuffer cmd) {
        constexpr VkImageSubresourceRange depth_range{static_cast<VkImageAspectFlags>(vkz::image_aspect_depth), 0, 1, 0, 1};
        constexpr VkImageSubresourceRange normal_range{static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, 0, 1};

        vkz::barrier::push_and_flush(cmd, scene_depth_.image, depth_range,
            vkz::pipeline_stage_all_commands, vkz::pipeline_stage_early_fragment_tests | vkz::pipeline_stage_late_fragment_tests,
            vkz::access_memory_read_write, vkz::access_depth_stencil_attachment_read_write,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        vkz::barrier::push_and_flush(cmd, scene_normal_.image, normal_range,
            vkz::pipeline_stage_all_commands, vkz::pipeline_stage_color_attachment_output,
            vkz::access_memory_read_write, vkz::access_color_attachment_write,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        for (auto *value : {&scene_surface_, &scene_mesh_id_}) {
            vkz::barrier::push_and_flush(cmd, value->image, normal_range,
                vkz::pipeline_stage_all_commands, vkz::pipeline_stage_color_attachment_output,
                vkz::access_memory_read_write, vkz::access_color_attachment_write,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        vkz::render_info info{
            .color_attachments = {
                {scene_normal_.image_view, VK_FORMAT_R16G16_SFLOAT, {0, 0, 0, 0}},
                {scene_surface_.image_view, VK_FORMAT_R16G16B16A16_SFLOAT, {0, 0, 0, 1}},
                {scene_mesh_id_.image_view, VK_FORMAT_R32_UINT, {0, 0, 0, 0}}
            },
            .depth_attachment = vkz::depth_stencil_attachment{scene_depth_.image_view, VK_FORMAT_D32_SFLOAT},
            .render_area = render_size_
        };

        vkz::render(cmd, info, [&] { draw_scene(cmd, true); });

        vkz::barrier::push_and_flush(cmd, scene_depth_.image, depth_range,
            vkz::pipeline_stage_early_fragment_tests | vkz::pipeline_stage_late_fragment_tests, vkz::pipeline_stage_compute_shader | vkz::pipeline_stage_ray_tracing_shader,
            vkz::access_depth_stencil_attachment_write, vkz::access_shader_read, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkz::barrier::push_and_flush(cmd, scene_normal_.image, normal_range,
            vkz::pipeline_stage_color_attachment_output, vkz::pipeline_stage_compute_shader | vkz::pipeline_stage_ray_tracing_shader,
            vkz::access_color_attachment_write, vkz::access_shader_read, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for (auto *value : {&scene_surface_, &scene_mesh_id_}) {
            vkz::barrier::push_and_flush(cmd, value->image, normal_range,
                vkz::pipeline_stage_color_attachment_output,
                vkz::pipeline_stage_compute_shader | vkz::pipeline_stage_ray_tracing_shader,
                vkz::access_color_attachment_write, vkz::access_shader_read,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void render_lighting(VkCommandBuffer cmd) {
        constexpr VkImageSubresourceRange color_range{static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, 0, 1};
        constexpr VkImageSubresourceRange depth_range{static_cast<VkImageAspectFlags>(vkz::image_aspect_depth), 0, 1, 0, 1};

        vkz::barrier::push_and_flush(cmd, scene_color_.image, color_range,
            vkz::pipeline_stage_all_commands, vkz::pipeline_stage_color_attachment_output,
            vkz::access_memory_read_write, vkz::access_color_attachment_write,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        vkz::barrier::push_and_flush(cmd, scene_color_msaa_.image, color_range,
            vkz::pipeline_stage_all_commands, vkz::pipeline_stage_color_attachment_output,
            vkz::access_memory_read_write, vkz::access_color_attachment_write,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        vkz::barrier::push_and_flush(cmd, scene_depth_msaa_.image, depth_range,
            vkz::pipeline_stage_all_commands, vkz::pipeline_stage_early_fragment_tests | vkz::pipeline_stage_late_fragment_tests,
            vkz::access_memory_read_write, vkz::access_depth_stencil_attachment_read_write,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        vkz::render_info info{
            .color_attachments = {{scene_color_msaa_.image_view, VK_FORMAT_R16G16B16A16_SFLOAT, {0, 0, 0, 1}, scene_color_.image_view}},
            .depth_attachment = vkz::depth_stencil_attachment{scene_depth_msaa_.image_view, VK_FORMAT_D32_SFLOAT},
            .render_area = render_size_
        };

        if (visualize_probes_) {
            vkz::barrier::push_and_flush(cmd, vkz::pipeline_stage_all_commands,
                vkz::pipeline_stage_vertex_shader | vkz::pipeline_stage_fragment_shader,
                vkz::access_memory_write, vkz::access_shader_read);
        }

        vkz::render(cmd, info, [&] {
            draw_scene(cmd, false);
            if (visualize_probes_) {
                vkz::bind_pipeline(cmd, probe_pipeline_);
                vkz::bind_descriptor_sets(cmd, probe_pipeline_, {scene_set_});
                vkz::push_constants(cmd, probe_pipeline_, probe_parameters_, vkz::shader_stage_vertex | vkz::shader_stage_fragment);
                const auto count = probe_parameters_.counts;
                vkz::draw(cmd, 12 * 8 * 6, count.x * count.y * count.z);
            }
        });

        vkz::barrier::push_and_flush(cmd, scene_color_.image, color_range,
            vkz::pipeline_stage_color_attachment_output, vkz::pipeline_stage_fragment_shader,
            vkz::access_color_attachment_write, vkz::access_shader_read,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void load_pipelines() {
        const auto shader = [](const char *name) {
            const auto file = cmrc::sponza_lighting_shaders::get_filesystem().open(name);

            if (file.size() % sizeof(uint32_t))
                throw std::runtime_error("Invalid Sponza SPIR-V");

            std::vector<uint32_t> words(file.size() / sizeof(uint32_t));
            std::memcpy(words.data(), file.begin(), file.size());

            return words;
        };

        for (uint32_t i = 0; i < pipelines_.size() + gbuffer_pipelines_.size(); ++i) {
            const bool gbuffer = i >= pipelines_.size();
            vkz::graphics_pipeline_builder builder{device_};

            builder
                .shader_stage()
                    .vertex_shader(shader("sponza.vert.spv"))
                    .fragment_shader(shader(gbuffer ? "gbuffer.frag.spv" : "sponza.frag.spv"))
                        .add_specialization(uint32_t(textures_.size()), 0)
                .input_assembly_state()
                    .triangles()
                .viewport_state()
                    .viewport()
                        .origin(0, 0)
                        .dimension(render_size_.x, render_size_.y)
                        .min_depth(0)
                        .max_depth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(render_size_.x, render_size_.y)
                        .add()
                .dynamic_state()
                    .viewport()
                    .scissor()
                .rasterization_state()
                    .cull_none()
                    .polygon_mode_fill()
                .multisample_state()
                    .rasterization_samples(gbuffer ? vkz::sample_count_x1 : vkz::sample_count_x4)
                .depth_stencil_state()
                    .enable_depth_test()
                    .compare_op_less_or_equal();

            if (i & 1)
                builder.rasterization_state().front_face_clockwise();
            else
                builder.rasterization_state().front_face_counter_clockwise();

            if (!gbuffer && (i & 2)) {
                builder
                    .depth_stencil_state()
                        .disable_depth_write()
                    .color_blend_state()
                        .attachment()
                            .enable_blend()
                            .src_color_blend_factor()
                                .src_alpha()
                            .dst_color_blend_factor()
                                .one_minus_src_alpha()
                            .src_alpha_blend_factor()
                                .one()
                            .dst_alpha_blend_factor()
                                .one_minus_src_alpha();
            } else {
                builder.depth_stencil_state().enable_depth_write();
            }

            auto &pipeline = gbuffer ? gbuffer_pipelines_[i - pipelines_.size()] : pipelines_[i];

            builder
                .color_blend_state()
                    .attachment()
                        .add()
                .layout()
                    .add_descriptor_set_layouts(layouts_)
                    .add_descriptor_set_layout(reflections_->tracing_resources().layouts[1])
                    .add_push_constant_range(vkz::shader_stage_vertex | vkz::shader_stage_fragment, 0, sizeof(vkz::rtx::instance_uniform))
                .name(gbuffer ? "rtx_sponza_gbuffer" : "rtx_sponza");

            auto &rendering = builder.dynamic_render_pass();
            rendering
                .add_color_attachment(gbuffer ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT)
                .depth_attachment(VK_FORMAT_D32_SFLOAT);

            if (gbuffer) {
                builder
                    .color_blend_state()
                        .attachment()
                            .add()
                        .attachment()
                            .add();

                rendering
                    .add_color_attachment(VK_FORMAT_R16G16B16A16_SFLOAT)
                    .add_color_attachment(VK_FORMAT_R32_UINT);
            }

            pipeline = builder.build();
        }

        tone_mapping_pipeline_ =
            vkz::graphics_pipeline_builder{device_}
                .shader_stage()
                    .vertex_shader(shader("tone_mapping.vert.spv"))
                    .fragment_shader(shader("tone_mapping.frag.spv"))
                .input_assembly_state()
                    .triangles()
                .viewport_state()
                    .viewport()
                        .origin(0, 0)
                        .dimension(render_size_.x, render_size_.y)
                        .min_depth(0)
                        .max_depth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(render_size_.x, render_size_.y)
                        .add()
                .dynamic_state()
                    .viewport()
                    .scissor()
                .rasterization_state()
                    .cull_none()
                    .polygon_mode_fill()
                .multisample_state()
                    .rasterization_samples(vkz::sample_count_x1)
                .depth_stencil_state()
                    .disable_depth_test()
                    .disable_depth_write()
                .color_blend_state()
                    .attachment()
                        .add()
                .layout()
                    .add_descriptor_set_layout(layouts_[0])
                    .add_push_constant_range(vkz::shader_stage_fragment, 0, sizeof(uint32_t))
                .dynamic_render_pass()
                    .add_color_attachment(color_format_)
                .name("rtx_uncharted2_tone_mapping")
            .build();

        probe_pipeline_ =
            vkz::graphics_pipeline_builder{device_}
                .shader_stage()
                    .vertex_shader(shader("probes.vert.spv"))
                    .fragment_shader(shader("probes.frag.spv"))
                .input_assembly_state()
                    .triangles()
                .viewport_state()
                    .viewport()
                        .origin(0, 0)
                        .dimension(render_size_.x, render_size_.y)
                        .min_depth(0)
                        .max_depth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(render_size_.x, render_size_.y)
                        .add()
                .dynamic_state()
                    .viewport()
                    .scissor()
                .rasterization_state()
                    .cull_none()
                    .polygon_mode_fill()
                .multisample_state()
                    .rasterization_samples(vkz::sample_count_x4)
                .depth_stencil_state()
                    .enable_depth_test()
                    .compare_op_less_or_equal()
                    .disable_depth_write()
                .color_blend_state()
                    .attachment()
                        .add()
                .layout()
                    .add_descriptor_set_layout(layouts_[0])
                    .add_push_constant_range(vkz::shader_stage_vertex | vkz::shader_stage_fragment, 0, sizeof(probe_parameters))
                .dynamic_render_pass()
                    .add_color_attachment(VK_FORMAT_R16G16B16A16_SFLOAT)
                    .depth_attachment(VK_FORMAT_D32_SFLOAT)
                .name("rtx_probe_visualization")
            .build();
    }

    vkz::context &context_;
    std::unique_ptr<rtx::nrc_gi> nrc_;
    std::unique_ptr<rtx::nrd_denoiser> nrd_, nrd_reflection_;
    std::vector<std::unique_ptr<rtx::nrd_denoiser>> nrd_shadows_;
    vkz::texture nrd_diffuse_, nrd_specular_, nrd_shadow_;
    bool use_nrc_{};
    bool previous_nrd_{};
    bool previous_nrc_{};
    bool previous_reflections_{};
    bool nrc_frame_pending_{};
    vkz::vma_memory_allocator &allocator_;
    vkz::vma_memory_allocator &ray_allocator_;
    vkz::device device_;
    VkQueue queue_;
    uint32_t queue_family_;
    const vkz::mesh::data &scene_;
    const vkz::buffer &vertices_, &indices_;
    glm::uvec2 render_size_;
    VkFormat color_format_;
    std::array<bool, 3> full_resolution_;
    bool visualize_probes_;
    vkz::camera::camera camera_;
    std::vector<vkz::mesh::light> lights_;
    bool living_room_{};
    std::unique_ptr<vkz::mesh::bvh> acceleration_;
    std::unique_ptr<vkz::ddgi> ddgi_;
    std::unique_ptr<vkz::shadow> shadows_;
    std::unique_ptr<vkz::reflection> reflections_;
    vkz::texture scene_color_msaa_, scene_depth_msaa_;
    vkz::texture scene_depth_, scene_normal_, scene_color_, scene_surface_, scene_mesh_id_;
    rtx::tv_light *tv_light_{};
    vkz::texture fallback_tv_;
    vkz::buffer fallback_tv_parameters_;
    std::vector<vkz::texture> textures_;
    vkz::buffer scene_buffer_, light_buffer_;
    std::vector<vkz::buffer> material_buffers_;
    std::array<vkz::descriptor_set_layout, 2> layouts_;
    vkz::descriptor_pool descriptor_pool_;
    vkz::descriptor_set scene_set_;
    std::vector<vkz::descriptor_set> material_sets_;
    std::vector<uint32_t> draw_order_;
    std::array<vkz::pipeline, 4> pipelines_;
    std::array<vkz::pipeline, 2> gbuffer_pipelines_;
    vkz::pipeline tone_mapping_pipeline_, probe_pipeline_;
    struct probe_parameters {
        glm::vec3 grid_position;
        glm::ivec3 counts;
        glm::vec3 spacing;
        float radius;
        int irradiance_side_length;
        uint32_t offsetting_enabled;
    };
    static_assert(sizeof(probe_parameters) == 48);
    probe_parameters probe_parameters_;
};
