#pragma once

// Writing a validated body as ACIS SAT 21500 (internal)
//
// The body is numbered first (SatPlan), then each kind of record is written from the plan:
// the header and the body's own records in sat_writer.cpp, the topology in sat_topology.cpp,
// the analytic geometry in sat_geometry.cpp, the B-splines and pcurves in sat_spline.cpp, and
// the text (lines and the cipher) in sat_encoding.cpp.

#include "model.hpp"

#include <string>
#include <vector>

namespace hdxf::acis::detail {

// AutoCAD 2025's producer, as every body in the facit has it. These are the kernel's names, not
// values derived from the SAT version.
inline constexpr const char* kProductName = "Autodesk AutoCAD";
inline constexpr const char* kKernelName = "ASM 230.1.0.65535 NT";
inline constexpr const char* kKernelVersion = "230.1.0.65535";

// The version of the int_cur and spl_sur data AutoCAD 2025 writes in the subtypes
inline constexpr int kSplineDataVersion = 22800;

inline constexpr size_t kMaxLineLength = 254;

// ---------------------------------------------------------------------------
// One record's tokens
// ---------------------------------------------------------------------------

class RecordWriter {
public:
    explicit RecordWriter(const std::string& type) { tokens_.push_back(type); }

    RecordWriter& pointer(int record);              // "$n"; a negative record: "$-1"
    RecordWriter& integer(long long value);
    RecordWriter& real(double value);
    RecordWriter& triple(const Point3& value);      // a position or a vector: three reals
    RecordWriter& word(const std::string& value);   // an identifier: "forward", "nubs", "I" …
    RecordWriter& string(const std::string& value); // "@n text", one token
    RecordWriter& open();                           // "{"
    RecordWriter& close();                          // "}"
    RecordWriter& infinite_interval(int ends);      // "I" once per end

    // The tokens and the record's end, "#"
    std::vector<std::string> finish() const;

private:
    std::vector<std::string> tokens_;
};

using Record = std::vector<std::string>;

// A topology or geometry record's start: the type, the attribute pointer, the tag and the
// pattern pointer (no attributes, no tag, no pattern)
RecordWriter entity_record(const std::string& type);

// ---------------------------------------------------------------------------
// The numbering
// ---------------------------------------------------------------------------

constexpr int kNoRecord = -1;

// What is written, in the order it is written, and the SAT record number of each part. The
// vectors indexed by a builder index hold kNoRecord for what the body does not use.
struct SatPlan {
    const BuilderData* data = nullptr;
    BodyId body;
    const detail::BodyRecord* body_record = nullptr;
    BodyKind kind = BodyKind::Solid;

    std::vector<std::uint32_t> lumps;
    std::vector<std::uint32_t> shells;
    std::vector<std::uint32_t> faces;
    std::vector<std::uint32_t> loops;
    std::vector<std::uint32_t> coedges;
    std::vector<std::uint32_t> edges;
    std::vector<std::uint32_t> vertices;
    std::vector<std::uint32_t> surfaces;
    std::vector<std::uint32_t> curves;
    std::vector<std::uint32_t> pcurves;

    int asmheader_record = 0;
    int body_number = 1;
    int transform_record = kNoRecord;
    std::vector<int> lump_record;
    std::vector<int> shell_record;
    std::vector<int> face_record;
    std::vector<int> loop_record;
    std::vector<int> coedge_record;
    std::vector<int> edge_record;
    std::vector<int> vertex_record;
    std::vector<int> point_record;
    std::vector<int> surface_record;
    std::vector<int> curve_record;
    std::vector<int> pcurve_record;
    int record_count = 0;

    // The SAT's subtype definitions ("{ … }" not followed by "ref") are numbered in file order; a
    // pcurve refers to its B-spline surface's by that number
    std::vector<int> surface_subtype;
    int subtype_count = 0;

    // The edges as written: always forward, from the vertex at the lower parameter
    std::vector<std::uint32_t> edge_start_vertex;   // by builder edge index
    std::vector<std::uint32_t> edge_end_vertex;
    std::vector<std::uint32_t> edge_first_coedge;   // the first coedge written on the edge
    std::vector<int> coedge_partner;                // by builder coedge index: its partner's record
    std::vector<int> vertex_edge;                   // by builder vertex index: the edge it points at
    std::vector<int> vertex_end_flag;               // 0: the edge's start (or both), 1: its end
};

SatPlan make_plan(const BuilderData& data, BodyId body);

// ---------------------------------------------------------------------------
// The records
// ---------------------------------------------------------------------------

void write_body_records(const SatPlan& plan, std::vector<Record>& records);         // sat_writer.cpp
void write_topology_records(const SatPlan& plan, std::vector<Record>& records);     // sat_topology.cpp
void write_geometry_records(SatPlan& plan, std::vector<Record>& records);           // sat_geometry.cpp

// Geometry shared between the files
// The data of a surface after its record's "p t n": "forward_v I I I I" … (analytic surfaces)
void write_analytic_surface_fields(RecordWriter& out, const SurfaceGeometry& surface);
void write_spline_surface_record(SatPlan& plan, std::uint32_t surface_index, std::vector<Record>& records);
void write_intcurve_record(SatPlan& plan, std::uint32_t curve_index, std::vector<Record>& records);
void write_pcurve_record(SatPlan& plan, std::uint32_t coedge_index, std::vector<Record>& records);   // sat_spline.cpp

// ---------------------------------------------------------------------------
// The text
// ---------------------------------------------------------------------------

// The records as lines of at most kMaxLineLength characters: a record starts a line, and goes on
// in lines that start with a space
std::vector<std::string> layout_lines(const std::vector<Record>& records);

// The three header lines of the SAT
std::vector<std::string> header_lines(int record_count, const BuildOptions& options);

// 159 - c for each character but the space (AutoCAD's SAT cipher). An error names a character that
// is outside what the SAT holds (printable ASCII).
Result<std::vector<std::string>> cipher_lines(const std::vector<std::string>& lines);

Result<SatDocument> write_sat(const BuilderData& data, BodyId body);

} // namespace hdxf::acis::detail
