// hdxf_roundtrip: what HDxf writes of one DXF file, for the verification batch of step 9.S
// (tools/fas9_verify_batch.py; the plan's slutsteg 9.S, point 2).
//
//   hdxf_roundtrip <in.dxf> <outdir>
//
// Writes, in the file's own version (HDxf writes a drawing read from 2000 ... 2018 only as that version):
//
//   roundtrip__<name>__ascii.dxf / __binary.dxf   the file read and written again, nothing changed
//   anew__<name>__ascii.dxf     / __binary.dxf    every record HDxf types written from its typed values alone: the
//                                                  preserved record (RawRecord) of each is emptied first, the way
//                                                  the tests do it (test/test_regression_2018.cpp, drop_records)
//
// <name> is the input's file name without its extension. What cannot be read or written is a line in
// <outdir>/roundtrip_errors.txt ("<file>: <step>: <last_error()>") and a FAIL line on standard output; what a
// successful load or save had to change or could not do (Drawing::warnings()) is a WARN line. Nothing is skipped
// silently. Exit code: 0 when all four files were written, 1 when one was not, 2 on a wrong command line.
//
// The program writes what HDxf makes of a file; the tools that read it (AutoCAD's core, ODA) are the batch's.

#include "hdxf.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Every record HDxf types, written anew from its typed values: the preserved record emptied (as test_regression_2018's
// drop_records). A record HDxf does not type (EntityType::Unknown, ObjectType::Unknown) keeps its groups: they are all it has.
void forget_entities(std::vector<hdxf::Entity>& list) {
    for (hdxf::Entity& e : list) {
        if (e.type == hdxf::EntityType::Unknown) continue;
        e.raw = hdxf::RawRecord();
        if (e.type == hdxf::EntityType::Insert) {
            for (auto& a : e.as_insert().attributes) a.raw = hdxf::RawRecord();
            e.as_insert().seqend_raw = hdxf::RawRecord();
        }
        if (e.type == hdxf::EntityType::Polyline) {
            for (auto& v : e.as_polyline().vertices) v.raw = hdxf::RawRecord();
            e.as_polyline().seqend_raw = hdxf::RawRecord();
        }
    }
}

void forget_records(hdxf::Drawing& d) {
    forget_entities(d.entities());
    for (hdxf::Block& b : d.blocks()) {
        forget_entities(b.entities);
        b.raw = hdxf::RawRecord();
        b.endblk_raw = hdxf::RawRecord();
        b.record.raw = hdxf::RawRecord();
    }
    for (auto& x : d.layers()) x.raw = hdxf::RawRecord();
    for (auto& x : d.linetypes()) x.raw = hdxf::RawRecord();
    for (auto& x : d.text_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : d.dim_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : d.views()) x.raw = hdxf::RawRecord();
    for (auto& x : d.ucs_list()) x.raw = hdxf::RawRecord();
    for (auto& x : d.vports()) x.raw = hdxf::RawRecord();
    for (auto& x : d.app_ids()) x.raw = hdxf::RawRecord();
    for (hdxf::Object& o : d.objects()) {
        if (o.type != hdxf::ObjectType::Unknown) o.raw = hdxf::RawRecord();
    }
}

struct Run {
    fs::path out_dir;
    std::string stem;
    std::string input_name;
    std::vector<std::string> errors;

    void fail(const std::string& step, const std::string& message) {
        const std::string line = input_name + ": " + step + ": " + (message.empty() ? "(no last_error)" : message);
        errors.push_back(line);
        std::cout << "FAIL " << line << "\n";
    }

    void warn(const std::string& step, const hdxf::Drawing& d) {
        for (const std::string& w : d.warnings()) std::cout << "WARN " << input_name << ": " << step << ": " << w << "\n";
    }

    // The file as the drawing writes it in the format; false (and a FAIL) when nothing came out or the file cannot be written
    bool write(const std::string& kind, hdxf::Drawing& d, hdxf::DxfFormat format) {
        const char* label = format == hdxf::DxfFormat::Ascii ? "ascii" : "binary";
        const std::string step = kind + " " + label;
        std::vector<uint8_t> bytes = d.save_to_memory(format);
        if (bytes.empty()) {
            fail(step + ": save", d.last_error());
            return false;
        }
        warn(step, d);
        const fs::path path = out_dir / (kind + "__" + stem + "__" + label + ".dxf");
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fail(step + ": write " + hdxf::utf8_from_path(path), "the file could not be written");
            return false;
        }
        return true;
    }

    bool load(hdxf::Drawing& d, const fs::path& input, const std::string& step) {
        if (!d.load(input)) {
            fail(step + ": load", d.last_error());
            return false;
        }
        warn(step + " load", d);
        return true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hdxf_roundtrip <in.dxf> <outdir>\n";
        return 2;
    }
    const fs::path input = hdxf::path_from_utf8(argv[1]);
    Run run;
    run.out_dir = hdxf::path_from_utf8(argv[2]);
    run.stem = input.stem().string();
    run.input_name = input.filename().string();
    std::error_code ec;
    fs::create_directories(run.out_dir, ec);
    if (ec) {
        std::cerr << "cannot create " << argv[2] << ": " << ec.message() << "\n";
        return 2;
    }

    // unchanged, in the file's own version, both formats
    {
        hdxf::Drawing d;
        if (run.load(d, input, "roundtrip")) {
            run.write("roundtrip", d, hdxf::DxfFormat::Ascii);
            run.write("roundtrip", d, hdxf::DxfFormat::Binary);
        }
    }
    // every typed record from its typed values alone
    {
        hdxf::Drawing d;
        if (run.load(d, input, "anew")) {
            forget_records(d);
            run.write("anew", d, hdxf::DxfFormat::Ascii);
            run.write("anew", d, hdxf::DxfFormat::Binary);
        }
    }

    if (!run.errors.empty()) {
        std::ofstream log(run.out_dir / "roundtrip_errors.txt", std::ios::app);
        for (const std::string& line : run.errors) log << line << "\n";
    }
    return run.errors.empty() ? 0 : 1;
}
