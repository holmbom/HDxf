#include "test_framework.hpp"
#include "hdxf.hpp"

// hdxf::Version is the project's version in CMakeLists.txt (HDXF_PROJECT_VERSION, test/CMakeLists.txt)
TEST(version_string) {
    hdxf_test::assert_eq(hdxf::Version::string(), std::string(HDXF_PROJECT_VERSION), "hdxf::Version is CMakeLists.txt's version");
}

TEST(version_components) {
    hdxf_test::assert_eq(std::to_string(hdxf::Version::major) + "." + std::to_string(hdxf::Version::minor) + "." +
                             std::to_string(hdxf::Version::patch),
                         std::string(HDXF_PROJECT_VERSION));
}
