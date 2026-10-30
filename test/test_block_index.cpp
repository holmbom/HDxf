#include "test_framework.hpp"
#include "hdxf.hpp"
#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// Name index (phase 0, step 0.8 — doc/plans/2026-10-22-fas-0-grund.md)
// ---------------------------------------------------------------------------

TEST(block_index_add_and_find) {
    hdxf::Drawing dwg;
    hdxf::Block& a = dwg.add_block("A", {1, 2, 3});
    dwg.add_block("B");
    hdxf_test::assert_true(dwg.find_block("A") == &dwg.blocks()[0], "A");
    hdxf_test::assert_true(dwg.find_block("B") == &dwg.blocks()[1], "B");
    hdxf_test::assert_true(dwg.find_block("C") == nullptr, "C");
    (void)a;

    // add_block() with a name that is there returns that block
    hdxf::Block& again = dwg.add_block("A", {4, 5, 6});
    hdxf_test::assert_eq(static_cast<int>(dwg.blocks().size()), 2, "no new block");
    hdxf_test::assert_true(&again == &dwg.blocks()[0], "same block");
    hdxf_test::assert_eq(dwg.blocks()[0].base_point.x, 4.0, "base point updated");
}

// A name changed through the pointer find_block() returns
TEST(block_index_rename_through_pointer) {
    hdxf::Drawing dwg;
    dwg.add_block("A");
    dwg.add_block("B");
    dwg.find_block("A")->name = "C";
    hdxf_test::assert_true(dwg.find_block("A") == nullptr, "old name");
    hdxf_test::assert_true(dwg.find_block("C") == &dwg.blocks()[0], "new name");
    hdxf_test::assert_true(dwg.find_block("B") == &dwg.blocks()[1], "other block");

    // A new block may now take the old name
    dwg.add_block("A");
    hdxf_test::assert_true(dwg.find_block("A") == &dwg.blocks()[2], "old name reused");
}

TEST(block_index_after_copy) {
    hdxf::Drawing dwg;
    dwg.add_block("A");
    dwg.add_block("B");
    hdxf::Drawing copy = dwg;
    hdxf_test::assert_true(copy.find_block("B") == &copy.blocks()[1], "copy's own block");
    copy.find_block("B")->name = "X";
    hdxf_test::assert_true(dwg.find_block("B") == &dwg.blocks()[1], "original unchanged");
    hdxf_test::assert_true(copy.find_block("X") == &copy.blocks()[1], "copy renamed");
}

TEST(block_index_after_load) {
    hdxf::Drawing dwg;
    dwg.add_block("A");
    dwg.add_block("B");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());

    hdxf::Drawing back;
    back.add_block("OLD");
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_true(back.find_block("OLD") == nullptr, "index from before load");
    const hdxf::Block* b = back.find_block("B");
    hdxf_test::assert_true(b != nullptr && b->name == "B", "B after load");
}

// Plan 0.8: 50 000 blocks and 1 000 000 LINE are read without O(n²). Before the index,
// 50 000 blocks and layers alone took 30 s (optimised build) — the reader searched the
// list for every new name. Now it is well under a second, plus the lines.
TEST(block_index_large_drawing_is_linear) {
    const int count = 50000;
    const int lines = 1000000;
    std::string dxf = "  0\nSECTION\n  2\nTABLES\n  0\nTABLE\n  2\nLAYER\n 70\n0\n";
    for (int i = 0; i < count; ++i) {
        dxf += "  0\nLAYER\n  2\nL" + std::to_string(i) + "\n 70\n0\n 62\n7\n  6\nCONTINUOUS\n";
    }
    dxf += "  0\nENDTAB\n  0\nENDSEC\n  0\nSECTION\n  2\nBLOCKS\n";
    for (int i = 0; i < count; ++i) {
        dxf += "  0\nBLOCK\n  8\n0\n  2\nB" + std::to_string(i) +
               "\n 70\n0\n 10\n0\n 20\n0\n 30\n0\n  0\nENDBLK\n  8\n0\n";
    }
    dxf += "  0\nENDSEC\n  0\nSECTION\n  2\nENTITIES\n";
    const std::string line = "  0\nLINE\n  8\n0\n 10\n1.5\n 20\n2.5\n 30\n0\n 11\n3.5\n 21\n4.5\n 31\n0\n";
    dxf.reserve(dxf.size() + line.size() * lines + 32);
    for (int i = 0; i < lines; ++i) dxf += line;
    dxf += "  0\nENDSEC\n  0\nEOF\n";

    auto start = std::chrono::steady_clock::now();
    hdxf::Drawing dwg;
    bool ok = dwg.load_from_memory(reinterpret_cast<const uint8_t*>(dxf.data()), dxf.size());
    const hdxf::Block* last = dwg.find_block("B" + std::to_string(count - 1));
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    hdxf_test::assert_true(ok, dwg.last_error());
    hdxf_test::assert_eq(static_cast<int>(dwg.blocks().size()), count, "blocks");
    hdxf_test::assert_eq(static_cast<int>(dwg.layers().size()), count, "layers");
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), lines, "lines");
    hdxf_test::assert_true(last != nullptr, "find_block");
    hdxf_test::assert_true(seconds < 20.0, "took " + std::to_string(seconds) + " s");
}
