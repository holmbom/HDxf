#include "test_framework.hpp"
#include "hdxf.hpp"

TEST(version_string) {
    std::string v = hdxf::Version::string();
    hdxf_test::assert_eq(v, "1.3.0");
}

TEST(version_components) {
    hdxf_test::assert_eq(hdxf::Version::major, 1);
    hdxf_test::assert_eq(hdxf::Version::minor, 3);
    hdxf_test::assert_eq(hdxf::Version::patch, 0);
}
