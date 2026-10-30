#include "DocumentDataTypes.hpp"

// OBJECT_PTR and VBA_PROJECT (phase 9, step 9.8; plan B100-B105) and the family's registration.
//
//   OBJECT_PTR:  the base record (5, {ACAD_REACTORS, 330) and `1001 DC015` XDATA (Autodesk's reference: "Begin ASE
//                xdata (DC015)"), no subclass of its own. What the ASE application stores in the XDATA is not documented
//                and no AutoCAD file in the corpus has an OBJECT_PTR, so the XDATA is kept as the XdataList it is
//                (Object::xdata) and nothing in it is read as a handle of this drawing.
//   VBA_PROJECT: S(AcDbVbaProject) 90 <number of bytes> (310)*: the project as the bytes AutoCAD wrote them; HDxf does
//                not run, read or change it. No AutoCAD file in the corpus has one (a drawing needs the VBA enabler and a
//                project to embed): the form is the reference's.

namespace hdxf {
namespace internal {

namespace {

std::string describe(const char* type, const Object& o) {
    return std::string(type) + " " + o.handle;
}

bool read_object_pointer(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (ctx.version < DxfVersion::R2000) {
        return ctx.fail(describe("OBJECT_PTR", o) + " in DXF R13/R14: no AutoCAD file shows the form there");
    }
    // The reference gives the head and the XDATA and nothing else: a record with a subclass is not that record
    for (const Subclass& s : view.subclasses()) {
        if (!s.name.empty() || !s.indices.empty()) {
            return ctx.fail(describe("OBJECT_PTR", o) + " has groups after its head: the reference gives only the head and the XDATA");
        }
    }
    o.data = ObjectPointerData{};
    return true;
}

bool read_vba_project(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (ctx.version < DxfVersion::R2000) {
        return ctx.fail(describe("VBA_PROJECT", o) + " in DXF R13/R14: no AutoCAD file shows the form there");
    }
    GroupCursor c(view, "AcDbVbaProject");
    if (!c.valid()) return ctx.fail(describe("VBA_PROJECT", o) + " has no AcDbVbaProject subclass");
    VbaProjectData d;
    int64_t length = 0;
    if (!c.require_integer(90, length)) return ctx.fail(describe("VBA_PROJECT", o) + ": " + c.error());
    if (length < 0) return ctx.fail(describe("VBA_PROJECT", o) + ": the number of bytes (90) is negative");
    c.bytes(310, d.project);
    if (!c.ok()) return ctx.fail(describe("VBA_PROJECT", o) + ": " + c.error());
    if (static_cast<int64_t>(d.project.size()) != length) {
        return ctx.fail(describe("VBA_PROJECT", o) + ": the number of bytes (90) is " + std::to_string(length) +
                        " but the 310 groups hold " + std::to_string(d.project.size()) + " bytes");
    }
    if (!c.at_end()) {
        size_t n = c.take_rest().size();
        ctx.warn(describe("VBA_PROJECT", o) + ": " + std::to_string(n) + " group(s) after the project are kept as they were read");
    }
    o.data = std::move(d);
    return true;
}

ClassSpec object_pointer_class() {
    // The reference's default class list: OBJECT_PTR, CAseDLPNTableRecord, proxy flags 1 (code 90), 280 0, 281 0.
    // The application name is not in the reference and no AutoCAD file has the record: "ObjectDBX Classes" is the
    // family's usual one (ej testat)
    ClassSpec c;
    c.cpp_name = "CAseDLPNTableRecord";
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 1;
    c.in_older = false;
    return c;
}

ClassSpec vba_project_class() {
    // No AutoCAD file and no entry in the reference's class list: the subclass marker is the C++ name, as in every
    // other class of AutoCAD's files; flags 0 (ej testat)
    ClassSpec c;
    c.cpp_name = "AcDbVbaProject";
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 0;
    c.in_older = false;
    return c;
}

// No AutoCAD file for either (F9.22): the form is Autodesk's reference, generated in 2000 … 2018 and not tested
// against AutoCAD; R12 has no OBJECTS section, R13 and R14 are not established
VersionMatrix reference_matrix() {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native);
        m.autocad(v, AutoCadStatus::NotTested);
    }
    return m;
}

ListSpec whole_subclass(const char* subclass) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

} // namespace

void register_object_pointer_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "OBJECT_PTR";
        c.type = ObjectType::ObjectPtr;
        c.read = read_object_pointer;
        c.write = write_object_pointer;
        c.cls = object_pointer_class();
        c.versions = reference_matrix();
        c.step = 8;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "VBA_PROJECT";
        c.type = ObjectType::VbaProject;
        c.read = read_vba_project;
        c.write = write_vba_project;
        c.cls = vba_project_class();
        c.versions = reference_matrix();
        c.lists = {whole_subclass("AcDbVbaProject")};
        c.step = 8;
        registry.add(std::move(c));
    }
}

void register_document_data_codecs(TypedRegistry& registry) {
    register_field_codecs(registry);
    register_data_table_codecs(registry);
    register_object_pointer_codecs(registry);
}

} // namespace internal
} // namespace hdxf
