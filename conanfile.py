from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class VulkanXRConan(ConanFile):
    name = "vulkanxr"
    version = "0.1.0"
    package_type = "static-library"

    license = "MIT"
    description = "C++ VulkanXR project template"
    topics = ("vulkan", "openxr", "graphics")

    settings = "os", "compiler", "build_type", "arch"
    options = {"with_nsight_aftermath": [True, False]}
    default_options = {"with_nsight_aftermath": False}
    exports_sources = "CMakeLists.txt", "src/*", "shaders/*", "example/*", "openxr_info/*", "test/*"

    def validate(self):
        check_min_cppstd(self, "20")

    def requirements(self):
        # Versions match the Vista project.
        self.requires(
            "vulkanizer/1.0.1",
            options={"with_nsight_aftermath": self.options.with_nsight_aftermath},
        )
        self.requires("glm/1.0.1")
        self.requires("spdlog/1.15.3")
        self.requires("fmt/11.0.2", override=True)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_TESTING"] = True
        toolchain.variables["VULKANXR_BUILD_EXAMPLES"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "VulkanXR")
        self.cpp_info.set_property("cmake_target_name", "VulkanXR::VulkanXR")
        self.cpp_info.libs = ["vulkanxr"]
