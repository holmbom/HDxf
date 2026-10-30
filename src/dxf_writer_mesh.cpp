#include "dxf_writer.hpp"
#include "mesh.hpp"

// MESH as AutoCAD writes it (plan phase 4, B48): the same groups in 2000, 2004, 2007 and 2010
// (ACadSharp's sample_AC1015 … AC1024, jscad's 3Dmesh01, LibreDWG's Surface.dxf) — AcDbSubDMesh
// with its cage, creases and overridden properties, its edges as proxy graphics.

namespace hdxf {
namespace internal {

void DxfWriter::write_mesh_r2000(const Entity& e, const std::string& owner) {
    const MeshData& d = e.as_mesh();
    const EntityProperties& p = e.props;
    const std::string what = "MESH " + p.handle;
    const int n = static_cast<int>(d.vertices.size());
    auto vertex = [&](int index) {
        if ((index < 0 || index >= n) && error_.empty()) {
            error_ = what + ": vertex index " + std::to_string(index) + " of " + std::to_string(n) + " vertices";
        }
    };
    for (const auto& f : d.faces) {
        for (int v : f) vertex(v);
    }
    for (const auto& edge : d.edges) {
        vertex(edge.start);
        vertex(edge.end);
    }
    // One crease per edge, as AutoCAD writes it; none given: 0 for each
    if (!d.creases.empty() && d.creases.size() != d.edges.size() && error_.empty()) {
        error_ = what + ": one crease per edge (" + std::to_string(d.creases.size()) + " for " +
                 std::to_string(d.edges.size()) + " edges)";
    }

    std::vector<uint8_t> proxy = proxy_graphics_ ? mesh_proxy(d) : std::vector<uint8_t>{};
    write_entity_head("MESH", p.handle, owner, p, &proxy);
    write_code(100, "AcDbSubDMesh");
    groups_.write_int(71, d.version);
    groups_.write_int(72, d.blend_crease ? 1 : 0);
    groups_.write_int(91, d.level);
    groups_.write_int(92, n);
    for (const auto& v : d.vertices) write_point(10, v);
    int64_t list = 0;
    for (const auto& f : d.faces) list += static_cast<int64_t>(f.size()) + 1;
    groups_.write_int(93, list);
    for (const auto& f : d.faces) {
        groups_.write_int(90, static_cast<int64_t>(f.size()));
        for (int v : f) groups_.write_int(90, v);
    }
    groups_.write_int(94, static_cast<int64_t>(d.edges.size()));
    for (const auto& edge : d.edges) {
        groups_.write_int(90, edge.start);
        groups_.write_int(90, edge.end);
    }
    groups_.write_int(95, static_cast<int64_t>(d.edges.size()));
    for (size_t i = 0; i < d.edges.size(); ++i) groups_.write_double(140, i < d.creases.size() ? d.creases[i] : 0.0);
    // The overridden properties as AutoCAD reads them and ODA writes them; a mapper's as read
    if (!d.overrides_unread.empty()) {
        for (const auto& g : d.overrides_unread) write_code_typed(g.code, g.value);
        return;
    }
    groups_.write_int(90, static_cast<int64_t>(d.overrides.size()));
    for (const auto& o : d.overrides) {
        groups_.write_int(91, o.marker);
        groups_.write_int(92, static_cast<int64_t>(o.properties.size()));
        for (const auto& prop : o.properties) {
            groups_.write_int(90, prop.type);
            switch (prop.type) {
                case 0:
                    groups_.write_int(63, prop.color.aci);
                    if (prop.color.has_rgb()) groups_.write_int(421, prop.color.rgb);
                    if (!prop.color.name.empty()) write_code(431, prop.color.name);
                    break;
                case 1: {
                    bool live = !prop.material_handle.empty() && (!filter_live_ || is_live(prop.material_handle));
                    write_code(330, live ? prop.material_handle : "0");
                    break;
                }
                case 2:
                    groups_.write_int(90, prop.transparency.raw);
                    break;
                default:
                    if (error_.empty()) {
                        error_ = what + ": property type " + std::to_string(prop.type) +
                                 " (a material mapper's form is not known: only one read from a file is written)";
                    }
                    break;
            }
        }
    }
}

} // namespace internal
} // namespace hdxf
