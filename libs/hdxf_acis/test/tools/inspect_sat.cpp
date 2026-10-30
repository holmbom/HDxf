// hdxf_acis_inspect_sat: the records of the ACIS data in a DXF file (or a SAT text file), one line each,
// the pointers followed by the type of the record they point at, and the numbers that count the
// topology. Used to read AutoCAD's facit and the builder's output side by side.
//
//   hdxf_acis_inspect_sat <file.dxf | file.sat> [body number]
//

#include "acis.hpp"
#include "hdxf.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace {

using hdxf::internal::AcisFile;
using hdxf::internal::AcisRecord;
using hdxf::internal::AcisTokenType;

std::string token_text(const AcisFile& file, const hdxf::internal::AcisToken& t) {
    switch (t.type) {
        case AcisTokenType::Pointer: {
            std::ostringstream out;
            out << "$" << static_cast<long long>(t.number);
            if (t.number >= 0 && static_cast<size_t>(t.number) < file.records.size()) {
                out << "<" << file.records[static_cast<size_t>(t.number)].type << ">";
            }
            return out.str();
        }
        case AcisTokenType::String: return "@\"" + t.text + "\"";
        case AcisTokenType::SubtypeStart: return "{";
        case AcisTokenType::SubtypeEnd: return "}";
        case AcisTokenType::Number: return t.text.empty() ? std::to_string(t.number) : t.text;
        case AcisTokenType::Logical: return t.number != 0.0 ? "true" : "false";
        default: return t.text.empty() ? std::to_string(t.number) : t.text;
    }
}

void print_body(const AcisFile& file, const std::string& header) {
    std::cout << header;
    std::map<std::string, int> counts;
    for (size_t i = 0; i < file.records.size(); ++i) {
        const AcisRecord& r = file.records[i];
        ++counts[r.type];
        std::cout << i << "\t" << r.type;
        for (const auto& t : r.tokens) std::cout << " " << token_text(file, t);
        std::cout << "\n";
    }
    std::cout << "-- " << file.records.size() << " records, version " << file.version << ", " << file.subtypes.size()
              << " subtype definitions\n";
    for (const auto& [type, n] : counts) std::cout << "-- " << type << ": " << n << "\n";
    // Uses of each edge
    std::map<long long, int> uses;
    for (const auto& r : file.records) {
        if (r.type == "coedge" && r.tokens.size() > 6) ++uses[static_cast<long long>(r.tokens[6].number)];
    }
    std::map<int, int> histogram;
    for (const auto& [edge, n] : uses) {
        (void)edge;
        ++histogram[n];
    }
    for (const auto& [n, edges] : histogram) std::cout << "-- " << edges << " edges with " << n << " coedges\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: hdxf_acis_inspect_sat <file.dxf | file.sat> [body number]\n";
        return 2;
    }
    std::string path = argv[1];
    int wanted = argc > 2 ? std::atoi(argv[2]) : -1;

    if (path.size() > 4 && path.substr(path.size() - 4) == ".sat") {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        AcisFile file;
        if (!hdxf::internal::read_sat(text.str(), file)) {
            std::cerr << "not SAT\n";
            return 1;
        }
        print_body(file, "== " + path + "\n");
        return 0;
    }

    hdxf::Drawing drawing;
    if (!drawing.load(path)) {
        std::cerr << path << ": " << drawing.last_error() << "\n";
        return 1;
    }
    int number = 0;
    for (const hdxf::Entity& e : drawing.entities()) {
        if (e.type != hdxf::EntityType::Solid3D && e.type != hdxf::EntityType::Region && e.type != hdxf::EntityType::Body) continue;
        if (wanted >= 0 && number != wanted) {
            ++number;
            continue;
        }
        const hdxf::AcisData& d = e.as_acis();
        AcisFile file;
        bool ok = false;
        std::string how;
        if (!d.lines.empty()) {
            std::string text = hdxf::internal::acis_deciphered(d);
            ok = hdxf::internal::read_sat(text, file);
            how = "SAT";
            std::istringstream in(text);
            std::string line;
            std::string header;
            for (int i = 0; i < 3 && std::getline(in, line); ++i) header += "   " + line + "\n";
            if (ok) print_body(file, "== body " + std::to_string(number) + " (" + how + ")\n" + header);
        } else if (!d.sab.empty()) {
            ok = hdxf::internal::read_sab(d.sab, file);
            how = "SAB";
            if (ok) print_body(file, "== body " + std::to_string(number) + " (" + how + ")\n");
        }
        if (!ok) std::cout << "== body " << number << ": no ACIS data HDxf can read\n";
        ++number;
    }
    return 0;
}
