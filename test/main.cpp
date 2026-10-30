#include "test_framework.hpp"
#include "hdxf.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// hdxf_tests [--shard=K/N] [--names=FILE] [--list] [filter …]: only the tests whose name contains one
// of the filters; with --shard, only every N-th of those from the K-th (0 … N-1) on; with --names, of
// those only the ones FILE names (one per line) instead; --list prints the names of the selected tests
int main(int argc, char** argv) {
    std::vector<std::string> filters;
    int shard_index = 0;
    int shard_count = 1;
    bool list = false;
    bool named = false;
    std::vector<std::string> names;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            list = true;
            continue;
        }
        if (arg.rfind("--names=", 0) == 0) {
            std::ifstream in(arg.substr(8));
            if (!in) {
                std::cerr << "cannot read " << arg.substr(8) << "\n";
                return 2;
            }
            named = true;
            for (std::string line; std::getline(in, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) names.push_back(line);
            }
            continue;
        }
        if (arg.rfind("--shard=", 0) == 0) {
            std::string spec = arg.substr(8);
            size_t slash = spec.find('/');
            if (slash != std::string::npos) {
                shard_index = std::atoi(spec.substr(0, slash).c_str());
                shard_count = std::atoi(spec.substr(slash + 1).c_str());
            }
            if (shard_count < 1 || shard_index < 0 || shard_index >= shard_count) {
                std::cerr << "--shard=K/N needs 0 <= K < N\n";
                return 2;
            }
            continue;
        }
        filters.push_back(arg);
    }
    if (list) {
        for (const auto& name : hdxf_test::selected_names(filters)) std::cout << name << "\n";
        return 0;
    }

    // A drawing HDxf changes is saved by the one logged in ($LASTSAVEDBY, as AutoCAD writes it); AutoCAD's facit
    // files were saved by "erikh", so the tests log in as that user on every machine
#ifdef _WIN32
    _putenv_s("USERNAME", "erikh");
#else
    setenv("USER", "erikh", 1);
#endif

    return hdxf_test::run_all_tests(filters, shard_index, shard_count, named ? &names : nullptr);
}
