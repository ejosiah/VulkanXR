#pragma once

#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <array>
#include <memory>

class checkerboard_renderer final : public vxr::renderer {
  public:
    using renderer::renderer;

    void init() override;

    void paused(const vxr::frame_info &frame, vxr::layers &layers) override;

    void render(const vxr::frame_info &frame, vxr::layers &layers) override;

    [[nodiscard]] const char *name() const noexcept override;

    [[nodiscard]] static std::unique_ptr<vxr::renderer> create(vxr::context &xr_context, vxr::session::context &session_context);

    [[nodiscard]] static vxr::session::config session_config();

  private:
    std::array<XrCompositionLayerQuad, 2> layers_{};
};
