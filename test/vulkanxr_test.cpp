#include <vxr/vxr.hpp>

#include <cassert>
#include <cmath>
#include <type_traits>
#include <vector>

namespace {

    [[nodiscard]] bool approximately_equal(float left, float right) {
        return std::abs(left - right) < 0.0001F;
    }

    void test_camera() {
        std::vector<XrView> views(2, vxr::make_struct<XrView>());
        views[0].pose.orientation.w = 1.0F;
        views[0].pose.position = {-0.03F, 1.6F, 0.0F};
        views[1].pose.orientation.w = 1.0F;
        views[1].pose.position = {0.03F, 1.6F, 0.0F};

        vxr::camera camera;
        camera.update(views);
        assert(camera.valid());
        assert(approximately_equal(camera.position().x, 0.0F));
        assert(approximately_equal(camera.position().y, 1.6F));

        camera.move({0.0F, 1.0F}, 2.0F);
        assert(approximately_equal(camera.position().z, -2.0F));
        camera.turn(vxr::radians(90.0F));
        assert(approximately_equal(camera.position().x, 0.0F));
        assert(approximately_equal(camera.position().z, -2.0F));

        const auto position_before_change = camera.position();
        for (auto &view : views) {
            view.pose.position.x += 1.0F;
        }
        auto event = vxr::make_struct<XrEventDataReferenceSpaceChangePending>();
        event.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        camera.reference_space_changed(event, views);
        camera.update(views);
        assert(approximately_equal(camera.position().x, position_before_change.x));
        assert(approximately_equal(camera.position().y, position_before_change.y));
        assert(approximately_equal(camera.position().z, position_before_change.z));
    }

} // namespace

int main() {
    static_assert(std::is_base_of_v<vkz::builder, vxr::context::builder>);
    static_assert(!std::is_base_of_v<vkz::context, vxr::context>);
    static_assert(!std::is_convertible_v<VkQueueFlags, vkz::queue>);
    auto builder = vxr::context::create();
    builder.add_queue(vkz::queue_compute | vkz::queue_transfer);
    builder.add_unique_queue(vkz::queue_video_decode);
    assert(has_flag(builder.queue_flags(), vkz::queue_graphics | vkz::queue_compute | vkz::queue_transfer));
    assert(builder.unique_queue_flags() == vkz::queue_video_decode);
    assert(vxr::project_name() == "VulkanXR");
    assert(vxr::vulkan_header_version() != 0);
    assert(vxr::openxr_api_version() >= XR_MAKE_VERSION(1, 1, 0));
    assert(vxr::version_to_string(XR_MAKE_VERSION(1, 2, 3)) == "1.2.3");
    test_camera();
    return 0;
}
