#include "test_framework.hpp"
#include "hdxf.hpp"

// hdxf_acis_tests [filter …]: only the tests whose name contains one of the filters.
int main(int argc, char** argv) {
    std::vector<std::string> filters(argv + 1, argv + argc);

    return hdxf_test::run_all_tests(filters) != 0 ? 1 : 0;
}
