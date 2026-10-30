#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// THUMBNAILIMAGE, the preview image (2000+) — phase 2, step 2.5 (plan B26)
// ---------------------------------------------------------------------------

namespace {

using Record = std::vector<std::pair<int, std::string>>;

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    }
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

// The THUMBNAILIMAGE section's record (from 0 SECTION), empty if there is none
Record thumbnail_section(const std::vector<Record>& recs) {
    for (const auto& r : recs) {
        if (r.size() > 1 && r[0].second == "SECTION" && r[1].second == "THUMBNAILIMAGE") return r;
    }
    return {};
}

// The test file's image: a 4x2 24-bit DIB (test/data/README.md)
std::vector<uint8_t> expected_image() {
    std::vector<uint8_t> b = {0x28, 0, 0, 0, 4, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0,
                              24, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) { b.push_back(255); b.push_back(0); b.push_back(0); }
    for (int i = 0; i < 2; ++i) { b.push_back(0); b.push_back(0); b.push_back(255); }
    for (int i = 0; i < 6; ++i) b.push_back(255);
    return b;
}

std::vector<uint8_t> image(size_t n) {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(i * 7 + 3);
    return b;
}

} // namespace

// The test file's bytes, and the section written back as it was (32-byte lines kept)
TEST(thumbnail_read_and_kept) {
    std::vector<uint8_t> in = read_bytes(synthetic("r2004_thumbnail.dxf"));
    hdxf::Drawing dwg = load(in);
    hdxf_test::assert_true(dwg.thumbnail() == expected_image(), "the image's bytes");
    hdxf_test::assert_true(dwg.warnings().empty(), "no warning");
    for (const auto& s : dwg.raw_sections()) hdxf_test::assert_true(s.name != "THUMBNAILIMAGE", "not in raw_sections()");
    std::vector<Record> before = records_of(in);
    std::vector<Record> after = records_of(save(dwg));
    hdxf_test::assert_true(after == before, "the file as it was");
    hdxf_test::assert_true(thumbnail_section(after) == thumbnail_section(before), "the section as it was");

    // In binary and back: the same bytes
    hdxf::Drawing binary = load(save(dwg, hdxf::DxfFormat::Binary));
    hdxf_test::assert_true(binary.thumbnail() == expected_image(), "through binary");
    hdxf::Drawing ascii = load(save(binary, hdxf::DxfFormat::Ascii));
    hdxf_test::assert_true(ascii.thumbnail() == expected_image(), "and back");
}

// Changed: written anew (90 and 128-byte lines); removed: the section goes
TEST(thumbnail_changed_and_removed) {
    hdxf::Drawing dwg = load(read_bytes(synthetic("r2004_thumbnail.dxf")));
    dwg.set_thumbnail(image(300));
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "thumbnail changed");
    Record section = thumbnail_section(records_of(out));
    hdxf_test::assert_eq(static_cast<int>(section.size()), 2 + 1 + 3, "90 and three 310");
    hdxf_test::assert_eq(section[2].first, 90);
    hdxf_test::assert_eq(section[2].second, "300");
    hdxf_test::assert_eq(static_cast<int>(section[3].second.size()), 254, "127 bytes, as AutoCAD");
    hdxf_test::assert_eq(static_cast<int>(section[5].second.size()), 92, "the last 46");
    // In binary too: no chunk over 127 bytes
    for (const auto& r : records_of(save(dwg, hdxf::DxfFormat::Binary))) {
        for (const auto& [code, v] : r) {
            if (code == 310) hdxf_test::assert_true(v.size() <= 254, "binary chunk of " + std::to_string(v.size() / 2));
        }
    }
    hdxf_test::assert_true(load(out).thumbnail() == image(300), "read back");

    dwg.set_thumbnail({});
    std::vector<Record> without = records_of(save(dwg));
    hdxf_test::assert_true(thumbnail_section(without).empty(), "no section");
    hdxf_test::assert_eq(without.back()[0].second, "EOF");
}

// Set in a drawing in memory: after OBJECTS in 2000 and 2004, none in R12
TEST(thumbnail_written) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004}) {
        hdxf::Drawing dwg(v);
        dwg.add_line({0, 0, 0}, {10, 10, 0});
        dwg.set_thumbnail(expected_image());
        for (hdxf::DxfFormat f : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            std::vector<uint8_t> out = save(dwg, f);
            std::vector<Record> recs = records_of(out);
            size_t objects_end = 0, thumb = 0;
            for (size_t i = 0; i < recs.size(); ++i) {
                if (recs[i].size() > 1 && recs[i][0].second == "SECTION" && recs[i][1].second == "OBJECTS") objects_end = i;
                if (recs[i].size() > 1 && recs[i][0].second == "SECTION" && recs[i][1].second == "THUMBNAILIMAGE") thumb = i;
            }
            hdxf_test::assert_true(thumb > objects_end && objects_end > 0, "after OBJECTS");
            hdxf_test::assert_eq(recs[thumb][2].second, "64", "90");
            if (f == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "thumbnail");
            hdxf_test::assert_true(load(out).thumbnail() == expected_image(), "read back");
        }
    }
    hdxf::Drawing r12;
    r12.set_thumbnail(expected_image());
    std::vector<Record> recs = records_of(save(r12));
    hdxf_test::assert_true(thumbnail_section(recs).empty(), "R12 has none");
}

// A 90 that does not match the data: the data is taken, with a warning; written back as
// it was while unchanged, with the right 90 when the image changes
TEST(thumbnail_wrong_count) {
    std::vector<uint8_t> in = read_bytes(synthetic("r2004_thumbnail.dxf"));
    std::string text(in.begin(), in.end());
    const std::string count = " 90\r\n       64\r\n";
    size_t at = text.find(count);
    hdxf_test::assert_true(at != std::string::npos, "the 90");
    text.replace(at, count.size(), " 90\r\n       70\r\n");
    std::vector<uint8_t> wrong(text.begin(), text.end());
    hdxf::Drawing dwg = load(wrong);
    hdxf_test::assert_true(dwg.thumbnail() == expected_image(), "the data");
    hdxf_test::assert_eq(static_cast<int>(dwg.warnings().size()), 1, "a warning");
    hdxf_test::assert_eq(dwg.warnings()[0], "THUMBNAILIMAGE: 90 says 70 bytes, the data has 64");
    hdxf_test::assert_true(thumbnail_section(records_of(save(dwg))) == thumbnail_section(records_of(wrong)),
                           "unchanged: as it was");
    std::vector<uint8_t> changed = expected_image();
    changed.back() = 0;
    dwg.set_thumbnail(changed);
    hdxf_test::assert_eq(thumbnail_section(records_of(save(dwg)))[2].second, "64", "the right 90");
}

// A 310 line longer than 127 bytes in an ASCII file becomes several chunks in binary (TrueView
// rejects a binary chunk of 128), and the data stays the same
TEST(thumbnail_long_line_to_binary) {
    std::vector<uint8_t> in = read_bytes(synthetic("r2004_thumbnail.dxf"));
    std::string text(in.begin(), in.end());
    // Join the two 32-byte lines into one, and repeat it to 160 bytes
    hdxf::Drawing dwg = load(in);
    std::vector<uint8_t> big;
    const std::vector<uint8_t> image32 = expected_image();
    for (int i = 0; i < 5; ++i) big.insert(big.end(), image32.begin(), image32.begin() + 32);
    std::string hex;
    static const char digits[] = "0123456789ABCDEF";
    for (uint8_t b : big) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    size_t from = text.find("310\r\n");
    size_t to = text.find("  0\r\nENDSEC", from);
    text.replace(from, to - from, "310\r\n" + hex + "\r\n");
    const std::string count = " 90\r\n       64\r\n";
    text.replace(text.find(count), count.size(), " 90\r\n      160\r\n");
    std::vector<uint8_t> edited(text.begin(), text.end());
    hdxf::Drawing one_line = load(edited);
    hdxf_test::assert_true(one_line.thumbnail() == big, "160 bytes on one line");
    std::vector<uint8_t> bin = save(one_line, hdxf::DxfFormat::Binary);
    int chunks = 0;
    for (const auto& r : records_of(bin)) {
        for (const auto& [code, v] : r) {
            if (code != 310) continue;
            ++chunks;
            hdxf_test::assert_true(v.size() <= 254, "at most 127 bytes");
        }
    }
    hdxf_test::assert_eq(chunks, 2, "127 + 33");
    hdxf_test::assert_true(load(bin).thumbnail() == big, "the same bytes");
    (void)dwg;
}
