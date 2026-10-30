#include "test_support.hpp"

#include <fstream>
#include <set>
#include <sstream>

using namespace acis_test;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// The model: handles, results, diagnostics, the builder's calls
// ---------------------------------------------------------------------------

TEST(acis_model_handles_are_numbered_in_creation_order_and_stay) {
    acis::BodyBuilder b;
    acis::VertexId v0 = b.add_vertex({0, 0, 0});
    acis::VertexId v1 = b.add_vertex({1, 0, 0});
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::VertexId v2 = b.add_vertex({2, 0, 0});
    assert_eq(static_cast<int>(v0.index), 0);
    assert_eq(static_cast<int>(v1.index), 1);
    assert_eq(static_cast<int>(v2.index), 2);
    assert_eq(static_cast<int>(body.index), 0);
    assert_true(v0.valid() && body.valid());
    assert_true(v0.builder == v2.builder, "one builder");
    // Adding more objects does not move the ones made
    for (int i = 0; i < 100; ++i) b.add_vertex({static_cast<double>(i), 1, 0});
    assert_eq(static_cast<int>(v1.index), 1);
    assert_false(acis::EdgeId{}.valid(), "a default handle is not valid");
}

TEST(acis_model_handle_of_another_builder_is_reported) {
    acis::BodyBuilder a;
    acis::BodyBuilder b;
    acis::BodyId foreign = a.create_body(acis::BodyKind::Solid);
    acis::BodyId own = b.create_body(acis::BodyKind::Solid);
    acis::LumpId lump = b.add_lump(foreign);
    assert_false(lump.valid(), "no lump for a body of another builder");
    acis::Result<acis::SatDocument> result = b.build_sat(own);
    assert_false(result.ok());
    assert_true(has_code(result.errors(), acis::ErrorCode::InvalidHandle), errors_text(result.errors()));
    // The body of another builder cannot be built here
    acis::Result<acis::SatDocument> other = b.build_sat(foreign);
    assert_true(has_code(other.errors(), acis::ErrorCode::InvalidHandle), errors_text(other.errors()));
}

TEST(acis_model_invalid_arguments_give_invalid_handles) {
    acis::BodyBuilder b;
    assert_false(b.add_lump(acis::BodyId{}).valid());
    assert_false(b.add_shell(acis::LumpId{}).valid());
    assert_false(b.add_loop(acis::FaceId{}, acis::LoopRole::Outer).valid());
    assert_false(b.add_coedge(acis::LoopId{}, acis::CoedgeDefinition{}).valid());
    assert_false(b.add_edge(acis::EdgeDefinition{}).valid());
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::Result<acis::SatDocument> result = b.build_sat(body);
    assert_true(has_code(result.errors(), acis::ErrorCode::InvalidHandle));
    assert_true(has_code(result.errors(), acis::ErrorCode::EmptyBody), "and the body has no lump");
}

TEST(acis_model_result_of_a_failure_has_no_value) {
    acis::Result<acis::SatDocument> failure = acis::Diagnostic{acis::ErrorCode::InvalidArgument, "x", "", "why"};
    assert_false(static_cast<bool>(failure));
    assert_eq(static_cast<int>(failure.errors().size()), 1);
    bool thrown = false;
    try {
        failure.value();
    } catch (const std::logic_error&) {
        thrown = true;
    }
    assert_true(thrown, "value() of a failure is a programming error");

    acis::Result<void> fine;
    assert_true(fine.ok());
    acis::Result<void> bad = acis::Diagnostic{acis::ErrorCode::InvalidHandle, "", "", "no"};
    assert_false(bad.ok());
    assert_eq(static_cast<int>(bad.errors().size()), 1);
}

TEST(acis_model_diagnostics_have_a_code_a_path_and_a_message) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.omit_face = 5;
    BoxModel m = build_box(b, 10, 10, 10, flags);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    assert_false(result.ok());
    bool found = false;
    for (const acis::Diagnostic& d : result.errors()) {
        if (d.code != acis::ErrorCode::EdgeUseCount) continue;
        found = true;
        assert_true(d.entity.rfind("edge[", 0) == 0, d.entity);
        assert_true(d.path.rfind("body[0]/lump[0]/shell[0]/edge[", 0) == 0, d.path);
        assert_true(!d.message.empty());
        std::string text = d.text();
        assert_true(text.rfind("edge_use_count: body[0]/lump[0]/shell[0]/edge[", 0) == 0, text);
        assert_true(text.find("found 1") != std::string::npos, text);
        break;
    }
    assert_true(found, errors_text(result.errors()));
}

TEST(acis_model_error_code_names_are_distinct) {
    std::set<std::string> names;
    for (int i = 0; i <= static_cast<int>(acis::ErrorCode::Encoding); ++i) {
        names.insert(acis::error_code_name(static_cast<acis::ErrorCode>(i)));
    }
    assert_eq(static_cast<int>(names.size()), static_cast<int>(acis::ErrorCode::Encoding) + 1);
    assert_true(names.count("edge_use_count") == 1 && names.count("partner_mismatch") == 1 &&
                names.count("pcurve_required") == 1 && names.count("curve_endpoint_mismatch") == 1);
}

TEST(acis_model_loop_order_is_set_by_the_caller) {
    acis::BodyBuilder b;
    BoxModel m = build_box(b, 10, 10, 10);
    const std::vector<acis::CoedgeId>& original = m.coedges[0];
    // The same loop started at another coedge is still a loop
    std::vector<acis::CoedgeId> rotated = {original[1], original[2], original[3], original[0]};
    require_ok(b.set_loop_order(m.loops[0], rotated), "rotated order");
    acis::Result<acis::SatDocument> ok = b.build_sat(m.body);
    require(ok, "rotated loop");
    // Left out or repeated coedges are refused
    std::vector<acis::CoedgeId> short_list = {original[0], original[1], original[2]};
    assert_false(b.set_loop_order(m.loops[0], short_list).ok());
    std::vector<acis::CoedgeId> repeated = {original[0], original[0], original[2], original[3]};
    assert_false(b.set_loop_order(m.loops[0], repeated).ok());
    // Coedges of another loop
    std::vector<acis::CoedgeId> foreign = m.coedges[1];
    assert_false(b.set_loop_order(m.loops[0], foreign).ok());
    // A scrambled order breaks the loop
    std::vector<acis::CoedgeId> scrambled = {original[0], original[2], original[1], original[3]};
    require_ok(b.set_loop_order(m.loops[0], scrambled), "scrambled order is accepted until it is built");
    require_failure(b.build_sat(m.body), acis::ErrorCode::LoopNotClosed, "scrambled loop");
}

TEST(acis_model_pairing_coedges_checks_its_arguments) {
    acis::BodyBuilder b;
    BoxModel m = build_box(b, 10, 10, 10);
    assert_false(b.pair_coedges(m.coedges[0][0], m.coedges[0][0]).ok(), "a coedge is not its own partner");
    assert_false(b.pair_coedges(m.coedges[0][0], acis::CoedgeId{}).ok());
    acis::BodyBuilder other;
    BoxModel o = build_box(other, 10, 10, 10);
    assert_false(b.pair_coedges(m.coedges[0][0], o.coedges[0][0]).ok(), "another builder's coedge");
}

TEST(acis_model_transform_is_checked) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::Transform scale_zero;
    scale_zero.scale = 0.0;
    assert_false(b.set_transform(body, scale_zero).ok());
    acis::Transform reflection;
    reflection.rotation[2][2] = -1.0;
    assert_false(b.set_transform(body, reflection).ok(), "a reflection");
    acis::Transform shear;
    shear.rotation[0][1] = 0.1;
    assert_false(b.set_transform(body, shear).ok(), "a shear");
    acis::Transform not_finite;
    not_finite.translation.x = std::nan("");
    assert_false(b.set_transform(body, not_finite).ok());
    acis::Transform fine;
    fine.translation = {1, 2, 3};
    fine.scale = 2.0;
    require_ok(b.set_transform(body, fine));
    assert_false(b.set_transform(acis::BodyId{}, fine).ok());
}

TEST(acis_model_transform_from_frame_has_the_axes_as_columns) {
    acis::Frame frame;
    frame.origin = {1, 2, 3};
    frame.z_axis = {1, 0, 0};
    frame.x_axis = {0, 1, 0};
    acis::Transform t = acis::transform_from_frame(frame);
    // x → (0,1,0), y = z × x = (1,0,0) × (0,1,0) = (0,0,1), z → (1,0,0)
    require_near(t.rotation[1][0], 1.0, 1e-15, "x column");
    require_near(t.rotation[2][1], 1.0, 1e-15, "y column");
    require_near(t.rotation[0][2], 1.0, 1e-15, "z column");
    require_near(t.translation.x, 1.0, 0.0, "translation");
    require_near(t.scale, 1.0, 0.0, "scale");
}

TEST(acis_model_unused_geometry_does_not_matter) {
    acis::BodyBuilder b;
    BoxModel m = build_box(b, 10, 10, 10);
    b.add_surface(acis::SphereGeometry{});
    b.add_curve(acis::LineGeometry{});
    b.add_vertex({100, 100, 100});
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    acis::SatDocument& document = require(result, "unused geometry");
    // Only what the body uses is written: six faces, twelve edges
    ParsedSat parsed = parse_document(document);
    assert_eq(count_records(parsed.file, "face"), 6);
    assert_eq(count_records(parsed.file, "sphere-surface"), 0);
    assert_eq(count_records(parsed.file, "vertex"), 8);
}

TEST(acis_model_a_builder_builds_one_body_at_a_time_from_several) {
    acis::BodyBuilder b;
    BoxModel first = build_box(b, 10, 10, 10);
    BoxPlacement place;
    place.origin = {50, 0, 0};
    BoxModel second = build_box(b, 20, 20, 20, BoxFlags{}, place);
    acis::Result<acis::SatDocument> a = b.build_sat(first.body);
    acis::Result<acis::SatDocument> c = b.build_sat(second.body);
    ParsedSat pa = parse_document(require(a, "first"));
    ParsedSat pc = parse_document(require(c, "second"));
    assert_eq(count_records(pa.file, "body"), 1);
    assert_eq(count_records(pc.file, "body"), 1);
    assert_eq(count_records(pa.file, "face"), 6);
    assert_eq(count_records(pc.file, "face"), 6);
}

TEST(acis_model_body_kind_decides_the_sidedness_of_the_faces) {
    auto face_words = [](acis::BodyKind kind) {
        acis::BodyBuilder b;
        BoxPlacement place;
        place.kind = kind;
        BoxFlags flags;
        if (kind == acis::BodyKind::OpenBody) flags.omit_face = 5;
        BoxModel m = build_box(b, 10, 10, 10, flags, place);
        acis::Result<acis::SatDocument> sat = b.build_sat(m.body);
        std::string words;
        for (const std::string& line : require(sat, "box").lines) {
            if (line.rfind("face ", 0) != 0) continue;
            words = line.substr(line.find(" forward") != std::string::npos ? line.find(" forward") : line.find(" reversed"));
            break;
        }
        return std::pair<std::string, hdxf::EntityType>(words, entity_type_of(kind));
    };
    assert_eq(face_words(acis::BodyKind::Solid).first, " forward single #");
    assert_eq(face_words(acis::BodyKind::ClosedBody).first, " forward single #");
    assert_eq(face_words(acis::BodyKind::OpenBody).first, " forward double out #");
    assert_true(face_words(acis::BodyKind::Solid).second == hdxf::EntityType::Solid3D);
    assert_true(face_words(acis::BodyKind::ClosedBody).second == hdxf::EntityType::Body);
    assert_true(face_words(acis::BodyKind::OpenBody).second == hdxf::EntityType::Body);
    acis::Result<acis::SatDocument> region = build_rectangle_region(0, 0, 3, 2);
    bool double_out = false;
    for (const std::string& line : require(region, "region").lines) {
        if (line.rfind("face ", 0) == 0) double_out = line.find(" double out #") != std::string::npos;
    }
    assert_true(double_out, "a REGION's face is double sided, outside");
}

TEST(acis_model_options_are_checked) {
    acis::BuildOptions options;
    options.header_unit = 0.0;
    acis::BodyBuilder b(options);
    BoxModel m = build_box(b, 10, 10, 10);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    assert_true(has_code(result.errors(), acis::ErrorCode::InvalidArgument), errors_text(result.errors()));
}

// ---------------------------------------------------------------------------
// The production library uses HDxf's public header only
// ---------------------------------------------------------------------------

TEST(acis_library_includes_no_internal_header_of_hdxf) {
    namespace fs = std::filesystem;
    fs::path root(HDXF_ACIS_SOURCE_DIR);
    // The library's own headers, and the public header of HDxf
    const std::set<std::string> allowed = {"hdxf.hpp",       "hdxf_acis.hpp", "format.hpp",    "geometry.hpp",
                                           "model.hpp",      "sat_writer.hpp", "validation.hpp"};
    int files = 0;
    for (const char* dir : {"src", "include"}) {
        for (const auto& entry : fs::directory_iterator(root / dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".hpp") continue;
            ++files;
            std::ifstream in(entry.path());
            std::string line;
            while (std::getline(in, line)) {
                size_t at = line.find("#include \"");
                if (at != 0) continue;
                size_t end = line.find('"', 10);
                std::string name = line.substr(10, end - 10);
                assert_true(allowed.count(name) == 1, entry.path().filename().string() + " includes " + name);
            }
        }
    }
    assert_true(files >= 16, "the library's sources were found");
}
