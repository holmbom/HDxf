#pragma once

// Minimal test framework - no external dependencies

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

namespace hdxf_test {

// An ASCII DXF as text with LF line endings (AutoCAD writes CRLF, and so does HDxf unless
// the file it read had LF); binary DXF is returned as it is. For tests that look for
// a pattern of lines (zero, then LINE) to match.
inline std::string lf_text(const std::vector<uint8_t>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    if (text.compare(0, 18, "AutoCAD Binary DXF") == 0) return text;
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

struct TestCase {
    std::string name;
    std::function<void()> func;
};

struct TestResult {
    std::string name;
    bool passed;
    bool skipped;
    std::string message;
    double seconds = 0.0;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int register_test(const std::string& name, std::function<void()> func) {
    test_registry().push_back({name, std::move(func)});
    return 0;
}

struct AssertFailure {
    std::string message;
};

// Thrown by skip() — the test did not run (for example, a test file is missing).
struct SkipTest {
    std::string reason;
};

inline void skip(const std::string& reason) {
    throw SkipTest{reason};
}

// A save's warnings besides the fonts that were not found, which depend on the machine the
// tests run on (hdxf::font_paths())
inline std::vector<std::string> warnings_besides_fonts(const std::vector<std::string>& all) {
    std::vector<std::string> out;
    for (const auto& w : all) {
        bool font = (w.rfind("Text style \"", 0) == 0 && w.find("\" not found; ") != std::string::npos) ||
                    w.rfind("Shape file \"", 0) == 0 || w.rfind("MTEXT font \"", 0) == 0;
        if (!font) out.push_back(w);
    }
    return out;
}

inline void assert_true(bool condition, const std::string& msg = "") {
    if (!condition) {
        throw AssertFailure{msg.empty() ? "Expected true" : msg};
    }
}

inline void assert_false(bool condition, const std::string& msg = "") {
    if (condition) {
        throw AssertFailure{msg.empty() ? "Expected false" : msg};
    }
}

inline void assert_eq(const std::string& a, const std::string& b, const std::string& msg = "") {
    if (a != b) {
        throw AssertFailure{msg.empty() ? ("Expected \"" + a + "\" == \"" + b + "\"") : msg};
    }
}

inline void assert_eq(int a, int b, const std::string& msg = "") {
    if (a != b) {
        throw AssertFailure{msg.empty() ? ("Expected " + std::to_string(a) + " == " + std::to_string(b)) : msg};
    }
}

inline void assert_eq(double a, double b, const std::string& msg = "") {
    if (a != b) {
        throw AssertFailure{msg.empty() ? ("Expected " + std::to_string(a) + " == " + std::to_string(b)) : msg};
    }
}

// Whether a test runs: all when no filter is given, else those whose name contains one of
// the filters (the test program's arguments)
inline bool selected(const std::string& name, const std::vector<std::string>& filters) {
    if (filters.empty()) return true;
    return std::any_of(filters.begin(), filters.end(),
                       [&](const std::string& f) { return name.find(f) != std::string::npos; });
}

// The names of the tests the filters select, in the order they run
inline std::vector<std::string> selected_names(const std::vector<std::string>& filters) {
    std::vector<std::string> names;
    for (const auto& tc : test_registry()) {
        if (selected(tc.name, filters)) names.push_back(tc.name);
    }
    return names;
}

// Runs the tests the filters select; of those, shard `shard_index` of `shard_count` runs every
// shard_count-th one, or — when `names` is given — the ones named there (tools/run_tests.py runs
// the shards in parallel). HDXF_TEST_TIMES=1 lists the slowest twenty after the results;
// HDXF_TEST_TIMES_FILE=path writes every test's time there ("ms<TAB>name").
inline int run_all_tests(const std::vector<std::string>& filters = {}, int shard_index = 0, int shard_count = 1,
                         const std::vector<std::string>* names = nullptr) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    std::vector<TestResult> results;

    int position = 0;
    for (const auto& tc : test_registry()) {
        if (!selected(tc.name, filters)) continue;
        if (names) {
            if (std::find(names->begin(), names->end(), tc.name) == names->end()) continue;
        } else if (position++ % shard_count != shard_index) {
            continue;
        }
        auto start = std::chrono::steady_clock::now();
        try {
            tc.func();
            results.push_back({tc.name, true, false, ""});
            passed++;
        } catch (const SkipTest& e) {
            results.push_back({tc.name, true, true, e.reason});
            skipped++;
        } catch (const AssertFailure& e) {
            results.push_back({tc.name, false, false, e.message});
            failed++;
        } catch (const std::exception& e) {
            results.push_back({tc.name, false, false, std::string("Exception: ") + e.what()});
            failed++;
        }
        results.back().seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    std::cout << "\n=== HDXF Test Results ===\n";
    for (const auto& r : results) {
        const char* status = r.skipped ? "  SKIP" : (r.passed ? "  PASS" : "  FAIL");
        std::cout << status << "  " << r.name;
        if (!r.message.empty()) {
            std::cout << " - " << r.message;
        }
        std::cout << "\n";
    }
    if (const char* times = std::getenv("HDXF_TEST_TIMES"); times && std::string(times) == "1") {
        std::vector<TestResult> slowest = results;
        std::sort(slowest.begin(), slowest.end(),
                  [](const TestResult& a, const TestResult& b) { return a.seconds > b.seconds; });
        std::cout << "\n=== Slowest ===\n";
        for (size_t i = 0; i < slowest.size() && i < 20; ++i) {
            std::cout << "  " << static_cast<long>(slowest[i].seconds * 1000.0) << " ms  " << slowest[i].name << "\n";
        }
    }
    if (const char* path = std::getenv("HDXF_TEST_TIMES_FILE"); path && *path) {
        std::ofstream out(path, std::ios::binary);
        for (const auto& r : results) out << static_cast<long>(r.seconds * 1000.0) << "\t" << r.name << "\n";
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << skipped << " skipped, " << (passed + failed + skipped) << " total\n";

    return failed > 0 ? 1 : 0;
}

} // namespace hdxf_test

#define TEST(name) \
    static void test_##name(); \
    static int reg_##name = hdxf_test::register_test(#name, test_##name); \
    static void test_##name()
