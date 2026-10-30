#pragma once

#include "hdxf.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace hdxf {
namespace internal {

inline std::vector<HeaderVar> default_r12_headers() {
    auto str = [](const std::string& name, int code, const std::string& val) -> HeaderVar {
        return {name, {{code, val}}};
    };
    auto i = [](const std::string& name, int code, int val) -> HeaderVar {
        return {name, {{code, std::to_string(val)}}};
    };
    auto d = [](const std::string& name, int code, double val) -> HeaderVar {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", val);
        return {name, {{code, buf}}};
    };
    auto p3 = [](const std::string& name, double x, double y, double z) -> HeaderVar {
        char bx[64], by[64], bz[64];
        std::snprintf(bx, sizeof(bx), "%.17g", x);
        std::snprintf(by, sizeof(by), "%.17g", y);
        std::snprintf(bz, sizeof(bz), "%.17g", z);
        return {name, {{10, bx}, {20, by}, {30, bz}}};
    };
    auto p2 = [](const std::string& name, double x, double y) -> HeaderVar {
        char bx[64], by[64];
        std::snprintf(bx, sizeof(bx), "%.17g", x);
        std::snprintf(by, sizeof(by), "%.17g", y);
        return {name, {{10, bx}, {20, by}}};
    };

    return {
        str("$ACADVER", 1, "AC1009"),
        p3("$INSBASE", 0.0, 0.0, 0.0),
        p3("$EXTMIN", 0.0, 0.0, 0.0),
        p3("$EXTMAX", 1.0, 1.0, 0.0),
        p2("$LIMMIN", 0.0, 0.0),
        p2("$LIMMAX", 12.0, 9.0),
        i("$ORTHOMODE", 70, 0),
        i("$REGENMODE", 70, 1),
        i("$FILLMODE", 70, 1),
        i("$QTEXTMODE", 70, 0),
        i("$MIRRTEXT", 70, 0),
        d("$LTSCALE", 40, 1.0),
        i("$ATTMODE", 70, 1),
        d("$TEXTSIZE", 40, 2.5),
        str("$TEXTSTYLE", 7, "Standard"),
        str("$CLAYER", 8, "0"),
        str("$CELTYPE", 6, "BYLAYER"),
        i("$CECOLOR", 62, 256),
        str("$DIMSTYLE", 2, "Standard"),
        d("$DIMSCALE", 40, 1.0),
        d("$DIMASZ", 40, 2.5),
        d("$DIMEXO", 40, 0.625),
        d("$DIMDLI", 40, 3.75),
        d("$DIMEXE", 40, 0.625),
        d("$DIMRND", 40, 0.0),
        d("$DIMDLE", 40, 0.0),
        d("$DIMTP", 40, 0.0),
        d("$DIMTM", 40, 0.0),
        d("$DIMTXT", 40, 2.5),
        d("$DIMCEN", 40, 2.5),
        d("$DIMTSZ", 40, 0.0),
        d("$DIMALTF", 40, 25.4),
        d("$DIMLFAC", 40, 1.0),
        d("$DIMTVP", 40, 0.0),
        d("$DIMTFAC", 40, 1.0),
        d("$DIMGAP", 40, 0.625),
        i("$DIMTOL", 70, 0),
        i("$DIMLIM", 70, 0),
        i("$DIMTIH", 70, 0),
        i("$DIMTOH", 70, 0),
        i("$DIMSE1", 70, 0),
        i("$DIMSE2", 70, 0),
        i("$DIMTAD", 70, 1),
        i("$DIMZIN", 70, 0),
        i("$DIMALT", 70, 0),
        i("$DIMALTD", 70, 2),
        i("$DIMTOFL", 70, 0),
        i("$DIMSAH", 70, 0),
        i("$DIMTIX", 70, 0),
        i("$DIMSOXD", 70, 0),
        // Empty, as AutoCAD writes them in R12 (sample_AC1009_ascii.dxf)
        str("$DIMPOST", 1, ""),
        str("$DIMAPOST", 1, ""),
        str("$DIMBLK", 1, ""),           // the R12 reference's 2 is a misprint: AutoCAD writes 1, as
                                         // for $DIMBLK1 and $DIMBLK2
        str("$DIMBLK1", 1, ""),
        str("$DIMBLK2", 1, ""),
        i("$DIMCLRD", 70, 0),
        i("$DIMCLRE", 70, 0),
        i("$DIMCLRT", 70, 0),
        i("$DIMASO", 70, 1),
        i("$DIMSHO", 70, 1),
        i("$LUNITS", 70, 2),
        i("$LUPREC", 70, 4),
        i("$AUNITS", 70, 0),
        i("$AUPREC", 70, 0),
        d("$ANGBASE", 50, 0.0),
        i("$ANGDIR", 70, 0),
        i("$PDMODE", 70, 3),
        d("$PDSIZE", 40, 0.0),
        d("$PLINEWID", 40, 0.0),
        i("$COORDS", 70, 1),
        i("$SPLFRAME", 70, 0),
        i("$SPLINETYPE", 70, 6),
        i("$SPLINESEGS", 70, 8),
        i("$ATTDIA", 70, 0),
        i("$ATTREQ", 70, 1),
        i("$SURFTAB1", 70, 6),
        i("$SURFTAB2", 70, 6),
        i("$SURFTYPE", 70, 6),
        i("$SURFU", 70, 6),
        i("$SURFV", 70, 6),
        i("$UNITMODE", 70, 0),
        i("$MAXACTVP", 70, 16),
        d("$ELEVATION", 40, 0.0),
        d("$THICKNESS", 40, 0.0),
        i("$LIMCHECK", 70, 0),
        i("$BLIPMODE", 70, 1),
        d("$CHAMFERA", 40, 0.0),
        d("$CHAMFERB", 40, 0.0),
        i("$SKPOLY", 70, 0),
        d("$SKETCHINC", 40, 0.1),
        i("$TILEMODE", 70, 1),
        d("$VIEWSIZE", 40, 10.0),
        p2("$VIEWCTR", 0.0, 0.0),
        p3("$VIEWDIR", 0.0, 0.0, 1.0),
        i("$WORLDVIEW", 70, 1),
        i("$SHADEDGE", 70, 3),
        i("$SHADEDIF", 70, 70),
        i("$VISRETAIN", 70, 0),
        i("$DRAGMODE", 70, 2),
        i("$SNAPMODE", 70, 0),
        i("$SNAPSTYLE", 70, 0),
        i("$SNAPISOPAIR", 70, 0),
        d("$SNAPANG", 50, 0.0),
        p2("$SNAPBASE", 0.0, 0.0),
        p2("$SNAPUNIT", 10.0, 10.0),
        p2("$GRIDUNIT", 10.0, 10.0),
        i("$GRIDMODE", 70, 0),
        i("$OSMODE", 70, 0),
        i("$FASTZOOM", 70, 1),
        str("$UCSNAME", 2, ""),
        p3("$UCSORG", 0.0, 0.0, 0.0),
        p3("$UCSXDIR", 1.0, 0.0, 0.0),
        p3("$UCSYDIR", 0.0, 1.0, 0.0),
        str("$PUCSNAME", 2, ""),
        p3("$PUCSORG", 0.0, 0.0, 0.0),
        p3("$PUCSXDIR", 1.0, 0.0, 0.0),
        p3("$PUCSYDIR", 0.0, 1.0, 0.0),
        d("$USRTIMER", 70, 1),
        str("$MENU", 1, "acad"),
        p3("$PEXTMIN", 0.0, 0.0, 0.0),
        p3("$PEXTMAX", 0.0, 0.0, 0.0),
        p2("$PLIMMIN", 0.0, 0.0),
        p2("$PLIMMAX", 12.0, 9.0),
        d("$PELEVATION", 40, 0.0),
        d("$FILLETRAD", 40, 0.0),
        i("$HANDLING", 70, 0),
        str("$HANDSEED", 5, "1"),
        i("$PLINEGEN", 70, 0),
        i("$PLIMCHECK", 70, 0),
        i("$PSLTSCALE", 70, 1),
        d("$TRACEWID", 40, 0.0),
        d("$TDCREATE", 40, 0.0),
        d("$TDINDWG", 40, 0.0),
        d("$TDUPDATE", 40, 0.0),
        d("$TDUSRTIMER", 40, 0.0),
        i("$USERI1", 70, 0),
        i("$USERI2", 70, 0),
        i("$USERI3", 70, 0),
        i("$USERI4", 70, 0),
        i("$USERI5", 70, 0),
        d("$USERR1", 40, 0.0),
        d("$USERR2", 40, 0.0),
        d("$USERR3", 40, 0.0),
        d("$USERR4", 40, 0.0),
        d("$USERR5", 40, 0.0),
        str("$DWGCODEPAGE", 3, "ANSI_1252"),
    };
}

// AutoCAD's default dimension style. A DIMSTYLE in an R13+ file has only the values that
// differ from these (Standard is just 2, 70 and 340); HDxf's own DimStyle defaults are
// the metric ones R12 output has always used.
inline DimStyle autocad_dim_style() {
    DimStyle d;
    d.dimasz = 0.18;
    d.dimexo = 0.0625;
    d.dimdli = 0.38;
    d.dimexe = 0.18;
    d.dimtxt = 0.18;
    d.dimcen = 0.09;
    d.dimgap = 0.09;
    d.dimtih = 1;
    d.dimtoh = 1;
    d.dimtad = 0;
    d.dimaltd = 2;
    d.dimaltf = 25.4;
    return d;
}

// The header of a DXF R13 file (AC1012): the 162 variables of AutoCAD's own R13 file
// (libre/r13_v.dxf, saved by AutoCAD; the other six AutoCAD R13 files have the same names in the same
// order) in its order, $BLIPMODE after $LIMCHECK and $SAVEIMAGES last (plan phase 7, format profile
// section 3, D2). A value that is the same in every AutoCAD R13/R14 file is that value ($MIRRTEXT 1,
// $PDMODE 0, ...); one that varies with the template is HDxf's 2000 new drawing's (metric); view,
// dates and timers are not defaults (empty extents, HDxf's 0.0 for the dates, the writer sets the
// handle seed and the extents). No $ACADMAINTVER: AutoCAD's R13 files have none.
inline std::vector<HeaderVar> default_r13_headers() {
    auto str = [](const std::string& name, int code, const std::string& val) -> HeaderVar {
        return {name, {{code, val}}};
    };
    auto i = [](const std::string& name, int code, int val) -> HeaderVar {
        return {name, {{code, std::to_string(val)}}};
    };
    auto d = [](const std::string& name, int code, double val) -> HeaderVar {
        return {name, {{code, format_double(val, NumberStyle::AutoCad)}}};
    };
    auto p3 = [](const std::string& name, double x, double y, double z) -> HeaderVar {
        return {name, {{10, format_double(x, NumberStyle::AutoCad)},
                       {20, format_double(y, NumberStyle::AutoCad)},
                       {30, format_double(z, NumberStyle::AutoCad)}}};
    };
    auto p2 = [](const std::string& name, double x, double y) -> HeaderVar {
        return {name, {{10, format_double(x, NumberStyle::AutoCad)},
                       {20, format_double(y, NumberStyle::AutoCad)}}};
    };

    return {
        str("$ACADVER", 1, "AC1012"),
        str("$DWGCODEPAGE", 3, "ANSI_1252"),
        p3("$INSBASE", 0.0, 0.0, 0.0),
        p3("$EXTMIN", 1.0e20, 1.0e20, 1.0e20),
        p3("$EXTMAX", -1.0e20, -1.0e20, -1.0e20),
        p2("$LIMMIN", 0.0, 0.0),
        p2("$LIMMAX", 12.0, 9.0),
        i("$ORTHOMODE", 70, 0),
        i("$REGENMODE", 70, 1),
        i("$FILLMODE", 70, 1),
        i("$QTEXTMODE", 70, 0),
        i("$MIRRTEXT", 70, 1),
        i("$DRAGMODE", 70, 2),
        d("$LTSCALE", 40, 1.0),
        i("$OSMODE", 70, 0),
        i("$ATTMODE", 70, 1),
        d("$TEXTSIZE", 40, 2.5),
        d("$TRACEWID", 40, 0.0),
        str("$TEXTSTYLE", 7, "STANDARD"),
        str("$CLAYER", 8, "0"),
        str("$CELTYPE", 6, "BYLAYER"),
        i("$CECOLOR", 62, 256),
        d("$CELTSCALE", 40, 1.0),
        i("$DELOBJ", 70, 1),
        i("$DISPSILH", 70, 0),
        d("$DIMSCALE", 40, 1.0),
        d("$DIMASZ", 40, 2.5),
        d("$DIMEXO", 40, 0.625),
        d("$DIMDLI", 40, 3.75),
        d("$DIMRND", 40, 0.0),
        d("$DIMDLE", 40, 0.0),
        d("$DIMEXE", 40, 0.625),
        d("$DIMTP", 40, 0.0),
        d("$DIMTM", 40, 0.0),
        d("$DIMTXT", 40, 2.5),
        d("$DIMCEN", 40, 2.5),
        d("$DIMTSZ", 40, 0.0),
        i("$DIMTOL", 70, 0),
        i("$DIMLIM", 70, 0),
        i("$DIMTIH", 70, 0),
        i("$DIMTOH", 70, 0),
        i("$DIMSE1", 70, 0),
        i("$DIMSE2", 70, 0),
        i("$DIMTAD", 70, 1),
        i("$DIMZIN", 70, 0),
        str("$DIMBLK", 1, ""),
        i("$DIMASO", 70, 1),
        i("$DIMSHO", 70, 1),
        str("$DIMPOST", 1, ""),
        str("$DIMAPOST", 1, ""),
        i("$DIMALT", 70, 0),
        i("$DIMALTD", 70, 2),
        d("$DIMALTF", 40, 25.4),
        d("$DIMLFAC", 40, 1.0),
        i("$DIMTOFL", 70, 0),
        d("$DIMTVP", 40, 0.0),
        i("$DIMTIX", 70, 0),
        i("$DIMSOXD", 70, 0),
        i("$DIMSAH", 70, 0),
        str("$DIMBLK1", 1, ""),
        str("$DIMBLK2", 1, ""),
        str("$DIMSTYLE", 2, "STANDARD"),
        i("$DIMCLRD", 70, 0),
        i("$DIMCLRE", 70, 0),
        i("$DIMCLRT", 70, 0),
        d("$DIMTFAC", 40, 1.0),
        d("$DIMGAP", 40, 0.625),
        i("$DIMJUST", 70, 0),
        i("$DIMSD1", 70, 0),
        i("$DIMSD2", 70, 0),
        i("$DIMTOLJ", 70, 1),
        i("$DIMTZIN", 70, 0),
        i("$DIMALTZ", 70, 0),
        i("$DIMALTTZ", 70, 0),
        i("$DIMFIT", 70, 3),
        i("$DIMUPT", 70, 0),
        i("$DIMUNIT", 70, 2),
        i("$DIMDEC", 70, 4),
        i("$DIMTDEC", 70, 4),
        i("$DIMALTU", 70, 2),
        i("$DIMALTTD", 70, 2),
        str("$DIMTXSTY", 7, "STANDARD"),
        i("$DIMAUNIT", 70, 0),
        i("$LUNITS", 70, 2),
        i("$LUPREC", 70, 4),
        d("$SKETCHINC", 40, 0.1),
        d("$FILLETRAD", 40, 0.0),
        i("$AUNITS", 70, 0),
        i("$AUPREC", 70, 0),
        str("$MENU", 1, "."),
        d("$ELEVATION", 40, 0.0),
        d("$PELEVATION", 40, 0.0),
        d("$THICKNESS", 40, 0.0),
        i("$LIMCHECK", 70, 0),
        i("$BLIPMODE", 70, 0),
        d("$CHAMFERA", 40, 0.0),
        d("$CHAMFERB", 40, 0.0),
        d("$CHAMFERC", 40, 0.0),
        d("$CHAMFERD", 40, 0.0),
        i("$SKPOLY", 70, 0),
        d("$TDCREATE", 40, 0.0),
        d("$TDUPDATE", 40, 0.0),
        d("$TDINDWG", 40, 0.0),
        d("$TDUSRTIMER", 40, 0.0),
        i("$USRTIMER", 70, 1),
        d("$ANGBASE", 50, 0.0),
        i("$ANGDIR", 70, 0),
        i("$PDMODE", 70, 0),
        d("$PDSIZE", 40, 0.0),
        d("$PLINEWID", 40, 0.0),
        i("$COORDS", 70, 1),
        i("$SPLFRAME", 70, 0),
        i("$SPLINETYPE", 70, 6),
        i("$SPLINESEGS", 70, 8),
        i("$ATTDIA", 70, 0),
        i("$ATTREQ", 70, 1),
        i("$HANDLING", 70, 1),
        str("$HANDSEED", 5, "1"),
        i("$SURFTAB1", 70, 6),
        i("$SURFTAB2", 70, 6),
        i("$SURFTYPE", 70, 6),
        i("$SURFU", 70, 6),
        i("$SURFV", 70, 6),
        str("$UCSNAME", 2, ""),
        p3("$UCSORG", 0.0, 0.0, 0.0),
        p3("$UCSXDIR", 1.0, 0.0, 0.0),
        p3("$UCSYDIR", 0.0, 1.0, 0.0),
        str("$PUCSNAME", 2, ""),
        p3("$PUCSORG", 0.0, 0.0, 0.0),
        p3("$PUCSXDIR", 1.0, 0.0, 0.0),
        p3("$PUCSYDIR", 0.0, 1.0, 0.0),
        i("$USERI1", 70, 0),
        i("$USERI2", 70, 0),
        i("$USERI3", 70, 0),
        i("$USERI4", 70, 0),
        i("$USERI5", 70, 0),
        d("$USERR1", 40, 0.0),
        d("$USERR2", 40, 0.0),
        d("$USERR3", 40, 0.0),
        d("$USERR4", 40, 0.0),
        d("$USERR5", 40, 0.0),
        i("$WORLDVIEW", 70, 1),
        i("$SHADEDGE", 70, 3),
        i("$SHADEDIF", 70, 70),
        i("$TILEMODE", 70, 1),
        i("$MAXACTVP", 70, 64),
        p3("$PINSBASE", 0.0, 0.0, 0.0),
        i("$PLIMCHECK", 70, 0),
        p3("$PEXTMIN", 1.0e20, 1.0e20, 1.0e20),
        p3("$PEXTMAX", -1.0e20, -1.0e20, -1.0e20),
        p2("$PLIMMIN", 0.0, 0.0),
        p2("$PLIMMAX", 12.0, 9.0),
        i("$UNITMODE", 70, 0),
        i("$VISRETAIN", 70, 1),
        i("$PLINEGEN", 70, 0),
        i("$PSLTSCALE", 70, 1),
        i("$TREEDEPTH", 70, 3020),
        i("$PICKSTYLE", 70, 1),
        str("$CMLSTYLE", 2, "STANDARD"),
        i("$CMLJUST", 70, 0),
        d("$CMLSCALE", 40, 1.0),
        i("$SAVEIMAGES", 70, 1),
    };
}

// The header of a DXF R14 file (AC1014): R13's list as AutoCAD's own R14 file has it
// (spatialauxetic/HEAD.DXF, 164 variables): $ACADMAINTVER 70 straight after $ACADVER (0 in a new
// drawing, as the AutoCAD that wrote the form HDxf writes: format profile D3), no $SAVEIMAGES, and
// $PROXYGRAPHICS and $MEASUREMENT last
inline std::vector<HeaderVar> default_r14_headers() {
    std::vector<HeaderVar> vars = default_r13_headers();
    vars.erase(std::remove_if(vars.begin(), vars.end(),
                              [](const HeaderVar& v) { return v.name == "$SAVEIMAGES"; }),
               vars.end());
    for (auto& v : vars) {
        if (v.name == "$ACADVER") v.values = {{1, "AC1014"}};
    }
    vars.insert(vars.begin() + 1, HeaderVar{"$ACADMAINTVER", {{70, "0"}}});
    vars.push_back(HeaderVar{"$PROXYGRAPHICS", {{70, "1"}}});
    vars.push_back(HeaderVar{"$MEASUREMENT", {{70, "1"}}});
    return vars;
}

// The header of a DXF 2000 file: the 199 variables AutoCAD writes for AC1015, in
// AutoCAD's order (ACadSharp's sample_AC1015_ascii.dxf), empty strings included as
// AutoCAD writes them. Values that R12 also has are the same as in default_r12_headers().
inline std::vector<HeaderVar> default_r2000_headers() {
    auto str = [](const std::string& name, int code, const std::string& val) -> HeaderVar {
        return {name, {{code, val}}};
    };
    auto i = [](const std::string& name, int code, int val) -> HeaderVar {
        return {name, {{code, std::to_string(val)}}};
    };
    auto d = [](const std::string& name, int code, double val) -> HeaderVar {
        return {name, {{code, format_double(val, NumberStyle::AutoCad)}}};
    };
    auto p3 = [](const std::string& name, double x, double y, double z) -> HeaderVar {
        return {name, {{10, format_double(x, NumberStyle::AutoCad)},
                       {20, format_double(y, NumberStyle::AutoCad)},
                       {30, format_double(z, NumberStyle::AutoCad)}}};
    };
    auto p2 = [](const std::string& name, double x, double y) -> HeaderVar {
        return {name, {{10, format_double(x, NumberStyle::AutoCad)},
                       {20, format_double(y, NumberStyle::AutoCad)}}};
    };

    return {
        str("$ACADVER", 1, "AC1015"),
        i("$ACADMAINTVER", 70, 20),
        str("$DWGCODEPAGE", 3, "ANSI_1252"),
        p3("$INSBASE", 0.0, 0.0, 0.0),
        p3("$EXTMIN", 0.0, 0.0, 0.0),
        p3("$EXTMAX", 1.0, 1.0, 0.0),
        p2("$LIMMIN", 0.0, 0.0),
        p2("$LIMMAX", 12.0, 9.0),
        i("$ORTHOMODE", 70, 0),
        i("$REGENMODE", 70, 1),
        i("$FILLMODE", 70, 1),
        i("$QTEXTMODE", 70, 0),
        i("$MIRRTEXT", 70, 0),
        d("$LTSCALE", 40, 1.0),
        i("$ATTMODE", 70, 1),
        d("$TEXTSIZE", 40, 2.5),
        d("$TRACEWID", 40, 0.0),
        str("$TEXTSTYLE", 7, "Standard"),
        str("$CLAYER", 8, "0"),
        str("$CELTYPE", 6, "BYLAYER"),
        i("$CECOLOR", 62, 256),
        d("$CELTSCALE", 40, 1.0),
        i("$DISPSILH", 70, 0),
        d("$DIMSCALE", 40, 1.0),
        d("$DIMASZ", 40, 2.5),
        d("$DIMEXO", 40, 0.625),
        d("$DIMDLI", 40, 3.75),
        d("$DIMRND", 40, 0.0),
        d("$DIMDLE", 40, 0.0),
        d("$DIMEXE", 40, 0.625),
        d("$DIMTP", 40, 0.0),
        d("$DIMTM", 40, 0.0),
        d("$DIMTXT", 40, 2.5),
        d("$DIMCEN", 40, 2.5),
        d("$DIMTSZ", 40, 0.0),
        i("$DIMTOL", 70, 0),
        i("$DIMLIM", 70, 0),
        i("$DIMTIH", 70, 0),
        i("$DIMTOH", 70, 0),
        i("$DIMSE1", 70, 0),
        i("$DIMSE2", 70, 0),
        i("$DIMTAD", 70, 1),
        i("$DIMZIN", 70, 0),
        str("$DIMBLK", 1, ""),
        i("$DIMASO", 70, 1),
        i("$DIMSHO", 70, 1),
        str("$DIMPOST", 1, ""),
        str("$DIMAPOST", 1, ""),
        i("$DIMALT", 70, 0),
        i("$DIMALTD", 70, 2),
        d("$DIMALTF", 40, 25.4),
        d("$DIMLFAC", 40, 1.0),
        i("$DIMTOFL", 70, 0),
        d("$DIMTVP", 40, 0.0),
        i("$DIMTIX", 70, 0),
        i("$DIMSOXD", 70, 0),
        i("$DIMSAH", 70, 0),
        str("$DIMBLK1", 1, ""),
        str("$DIMBLK2", 1, ""),
        str("$DIMSTYLE", 2, "Standard"),
        i("$DIMCLRD", 70, 0),
        i("$DIMCLRE", 70, 0),
        i("$DIMCLRT", 70, 0),
        d("$DIMTFAC", 40, 1.0),
        d("$DIMGAP", 40, 0.625),
        i("$DIMJUST", 70, 0),
        i("$DIMSD1", 70, 0),
        i("$DIMSD2", 70, 0),
        i("$DIMTOLJ", 70, 1),
        i("$DIMTZIN", 70, 0),
        i("$DIMALTZ", 70, 0),
        i("$DIMALTTZ", 70, 0),
        i("$DIMUPT", 70, 0),
        i("$DIMDEC", 70, 4),
        i("$DIMTDEC", 70, 4),
        i("$DIMALTU", 70, 2),
        i("$DIMALTTD", 70, 2),
        str("$DIMTXSTY", 7, "Standard"),
        i("$DIMAUNIT", 70, 0),
        i("$DIMADEC", 70, 0),
        d("$DIMALTRND", 40, 0.0),
        i("$DIMAZIN", 70, 0),
        i("$DIMDSEP", 70, 46),
        i("$DIMATFIT", 70, 3),
        i("$DIMFRAC", 70, 0),
        str("$DIMLDRBLK", 1, ""),
        i("$DIMLUNIT", 70, 2),
        i("$DIMLWD", 70, -2),
        i("$DIMLWE", 70, -2),
        i("$DIMTMOVE", 70, 0),
        i("$LUNITS", 70, 2),
        i("$LUPREC", 70, 4),
        d("$SKETCHINC", 40, 0.1),
        d("$FILLETRAD", 40, 0.0),
        i("$AUNITS", 70, 0),
        i("$AUPREC", 70, 0),
        str("$MENU", 1, "acad"),
        d("$ELEVATION", 40, 0.0),
        d("$PELEVATION", 40, 0.0),
        d("$THICKNESS", 40, 0.0),
        i("$LIMCHECK", 70, 0),
        d("$CHAMFERA", 40, 0.0),
        d("$CHAMFERB", 40, 0.0),
        d("$CHAMFERC", 40, 0.0),
        d("$CHAMFERD", 40, 0.0),
        i("$SKPOLY", 70, 0),
        d("$TDCREATE", 40, 0.0),
        d("$TDUCREATE", 40, 0.0),
        d("$TDUPDATE", 40, 0.0),
        d("$TDUUPDATE", 40, 0.0),
        d("$TDINDWG", 40, 0.0),
        d("$TDUSRTIMER", 40, 0.0),
        i("$USRTIMER", 70, 1),
        d("$ANGBASE", 50, 0.0),
        i("$ANGDIR", 70, 0),
        i("$PDMODE", 70, 3),
        d("$PDSIZE", 40, 0.0),
        d("$PLINEWID", 40, 0.0),
        i("$SPLFRAME", 70, 0),
        i("$SPLINETYPE", 70, 6),
        i("$SPLINESEGS", 70, 8),
        str("$HANDSEED", 5, "1"),
        i("$SURFTAB1", 70, 6),
        i("$SURFTAB2", 70, 6),
        i("$SURFTYPE", 70, 6),
        i("$SURFU", 70, 6),
        i("$SURFV", 70, 6),
        str("$UCSBASE", 2, ""),
        str("$UCSNAME", 2, ""),
        p3("$UCSORG", 0.0, 0.0, 0.0),
        p3("$UCSXDIR", 1.0, 0.0, 0.0),
        p3("$UCSYDIR", 0.0, 1.0, 0.0),
        str("$UCSORTHOREF", 2, ""),
        i("$UCSORTHOVIEW", 70, 0),
        p3("$UCSORGTOP", 0.0, 0.0, 0.0),
        p3("$UCSORGBOTTOM", 0.0, 0.0, 0.0),
        p3("$UCSORGLEFT", 0.0, 0.0, 0.0),
        p3("$UCSORGRIGHT", 0.0, 0.0, 0.0),
        p3("$UCSORGFRONT", 0.0, 0.0, 0.0),
        p3("$UCSORGBACK", 0.0, 0.0, 0.0),
        str("$PUCSBASE", 2, ""),
        str("$PUCSNAME", 2, ""),
        p3("$PUCSORG", 0.0, 0.0, 0.0),
        p3("$PUCSXDIR", 1.0, 0.0, 0.0),
        p3("$PUCSYDIR", 0.0, 1.0, 0.0),
        str("$PUCSORTHOREF", 2, ""),
        i("$PUCSORTHOVIEW", 70, 0),
        p3("$PUCSORGTOP", 0.0, 0.0, 0.0),
        p3("$PUCSORGBOTTOM", 0.0, 0.0, 0.0),
        p3("$PUCSORGLEFT", 0.0, 0.0, 0.0),
        p3("$PUCSORGRIGHT", 0.0, 0.0, 0.0),
        p3("$PUCSORGFRONT", 0.0, 0.0, 0.0),
        p3("$PUCSORGBACK", 0.0, 0.0, 0.0),
        i("$USERI1", 70, 0),
        i("$USERI2", 70, 0),
        i("$USERI3", 70, 0),
        i("$USERI4", 70, 0),
        i("$USERI5", 70, 0),
        d("$USERR1", 40, 0.0),
        d("$USERR2", 40, 0.0),
        d("$USERR3", 40, 0.0),
        d("$USERR4", 40, 0.0),
        d("$USERR5", 40, 0.0),
        i("$WORLDVIEW", 70, 1),
        i("$SHADEDGE", 70, 3),
        i("$SHADEDIF", 70, 70),
        i("$TILEMODE", 70, 1),
        i("$MAXACTVP", 70, 64),
        p3("$PINSBASE", 0.0, 0.0, 0.0),
        i("$PLIMCHECK", 70, 0),
        p3("$PEXTMIN", 0.0, 0.0, 0.0),
        p3("$PEXTMAX", 0.0, 0.0, 0.0),
        p2("$PLIMMIN", 0.0, 0.0),
        p2("$PLIMMAX", 12.0, 9.0),
        i("$UNITMODE", 70, 0),
        i("$VISRETAIN", 70, 1),
        i("$PLINEGEN", 70, 0),
        i("$PSLTSCALE", 70, 1),
        i("$TREEDEPTH", 70, 3020),
        str("$CMLSTYLE", 2, "Standard"),
        i("$CMLJUST", 70, 0),
        d("$CMLSCALE", 40, 1.0),
        i("$PROXYGRAPHICS", 70, 1),
        i("$MEASUREMENT", 70, 1),
        i("$CELWEIGHT", 370, -1),
        i("$ENDCAPS", 280, 0),
        i("$JOINSTYLE", 280, 0),
        i("$LWDISPLAY", 290, 0),
        i("$INSUNITS", 70, 0),
        str("$HYPERLINKBASE", 1, ""),
        str("$STYLESHEET", 1, ""),
        i("$XEDIT", 290, 1),
        i("$CEPSNTYPE", 380, 0),
        i("$PSTYLEMODE", 290, 1),
        // Set by ensure_r2000_structure(); $VERSIONGUID changes when the drawing does
        str("$FINGERPRINTGUID", 2, ""),
        str("$VERSIONGUID", 2, ""),
        i("$EXTNAMES", 290, 1),
        d("$PSVPSCALE", 40, 0.0),
        i("$OLESTARTUP", 290, 0),
    };
}

// The header of a DXF 2004 file: the 2000 list with the twelve variables 2004 adds, where
// and as AutoCAD writes them (sample_AC1018_ascii.dxf and AutoCAD 2016's new drawings
// saved as 2004: 211 variables). $HIDETEXT and $INTERSECTIONDISPLAY are 280 in AutoCAD's
// files (the reference says 290). $LASTSAVEDBY is empty here; the writer puts the login
// name in it, as AutoCAD does at every save (rättningsfas R.4).
inline std::vector<HeaderVar> default_r2004_headers() {
    auto str = [](const std::string& name, int code, const std::string& val) -> HeaderVar {
        return {name, {{code, val}}};
    };
    auto i = [](const std::string& name, int code, int val) -> HeaderVar {
        return {name, {{code, std::to_string(val)}}};
    };
    std::vector<HeaderVar> vars = default_r2000_headers();
    for (auto& v : vars) {
        if (v.name == "$ACADVER") v.values = {{1, "AC1018"}};
        if (v.name == "$ACADMAINTVER") v.values = {{70, "104"}};
    }
    auto after = [&](const std::string& name, std::vector<HeaderVar> added) {
        auto it = std::find_if(vars.begin(), vars.end(),
                               [&](const HeaderVar& v) { return v.name == name; });
        vars.insert(it + 1, added.begin(), added.end());
    };
    after("$DWGCODEPAGE", {str("$LASTSAVEDBY", 1, "")});
    after("$OLESTARTUP", {
        i("$SORTENTS", 280, 127),
        i("$INDEXCTL", 280, 0),
        i("$HIDETEXT", 280, 1),
        i("$XCLIPFRAME", 290, 0),
        i("$HALOGAP", 280, 0),
        i("$OBSCOLOR", 70, 257),
        i("$OBSLTYPE", 280, 0),
        i("$INTERSECTIONDISPLAY", 280, 0),
        i("$INTERSECTIONCOLOR", 70, 257),
        i("$DIMASSOC", 280, 2),
        str("$PROJECTNAME", 1, ""),
    });
    return vars;
}

// The header of a DXF 2007 file: the 2004 list with the 40 variables 2007 adds, where and as
// AutoCAD writes them (251 in the same order in ACadSharp's sample_AC1021, AutoCAD 2016's and
// AutoCAD 2017's new drawings saved as 2007; the values are jscad's empty2007.dxf, plan phase
// 3, B32). $CMATERIAL, $INTERFEREOBJVS and $INTERFEREVPVS are handles of the material ByLayer
// and the visual styles Realistic and Wireframe: empty here, set with the new drawing's
// objects (B34).
inline std::vector<HeaderVar> default_r2007_headers() {
    auto str = [](const std::string& name, int code, const std::string& val) -> HeaderVar {
        return {name, {{code, val}}};
    };
    auto i = [](const std::string& name, int code, int val) -> HeaderVar {
        return {name, {{code, std::to_string(val)}}};
    };
    auto d = [](const std::string& name, int code, double val) -> HeaderVar {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", val);
        return {name, {{code, buf}}};
    };
    std::vector<HeaderVar> vars = default_r2004_headers();
    for (auto& v : vars) {
        if (v.name == "$ACADVER") v.values = {{1, "AC1021"}};
        if (v.name == "$ACADMAINTVER") v.values = {{70, "50"}};
    }
    auto after = [&](const std::string& name, std::vector<HeaderVar> added) {
        auto it = std::find_if(vars.begin(), vars.end(),
                               [&](const HeaderVar& v) { return v.name == name; });
        vars.insert(it + 1, added.begin(), added.end());
    };
    after("$DIMTMOVE", {
        d("$DIMFXL", 40, 1.0),
        i("$DIMFXLON", 70, 0),
        d("$DIMJOGANG", 40, 0.7853981633974483),
        i("$DIMTFILL", 70, 0),
        i("$DIMTFILLCLR", 70, 0),
        i("$DIMARCSYM", 70, 0),
        str("$DIMLTYPE", 6, ""),
        str("$DIMLTEX1", 6, ""),
        str("$DIMLTEX2", 6, ""),
    });
    after("$PROJECTNAME", {
        i("$CAMERADISPLAY", 290, 0),
        d("$LENSLENGTH", 40, 50.0),
        d("$CAMERAHEIGHT", 40, 0.0),
        d("$STEPSPERSEC", 40, 2.0),
        d("$STEPSIZE", 40, 6.0),
        d("$3DDWFPREC", 40, 2.0),
        d("$PSOLWIDTH", 40, 0.25),
        d("$PSOLHEIGHT", 40, 4.0),
        d("$LOFTANG1", 40, 1.570796326794896),
        d("$LOFTANG2", 40, 1.570796326794896),
        d("$LOFTMAG1", 40, 0.0),
        d("$LOFTMAG2", 40, 0.0),
        i("$LOFTPARAM", 70, 7),
        i("$LOFTNORMALS", 280, 1),
        d("$LATITUDE", 40, 37.795),
        d("$LONGITUDE", 40, -122.394),
        d("$NORTHDIRECTION", 40, 0.0),
        i("$TIMEZONE", 70, -8000),
        i("$LIGHTGLYPHDISPLAY", 280, 1),
        i("$TILEMODELIGHTSYNCH", 280, 1),
        str("$CMATERIAL", 347, ""),
        i("$SOLIDHIST", 280, 1),
        i("$SHOWHIST", 280, 1),
        i("$DWFFRAME", 280, 2),
        i("$DGNFRAME", 280, 0),
        i("$REALWORLDSCALE", 290, 1),
        i("$INTERFERECOLOR", 62, 1),
        str("$INTERFEREOBJVS", 345, ""),
        str("$INTERFEREVPVS", 346, ""),
        i("$CSHADOW", 280, 0),
        d("$SHADOWPLANELOCATION", 40, 0.0),
    });
    return vars;
}

// The header of a DXF 2010 file: the 2007 list with $DIMTXTDIRECTION after $DIMLTEX2 and
// $XCLIPFRAME as 280 (0, 1 or 2; 290 in 2004/2007) — 252 variables in the same order in
// ACadSharp's sample_AC1024 and AutoCAD 2017's new drawing saved as 2010 (jscad's empty2010.dxf,
// whose $ACADMAINTVER 225 this is; later AutoCADs write 226 — plan phase 4, B43)
inline std::vector<HeaderVar> default_r2010_headers() {
    std::vector<HeaderVar> vars = default_r2007_headers();
    for (auto& v : vars) {
        if (v.name == "$ACADVER") v.values = {{1, "AC1024"}};
        if (v.name == "$ACADMAINTVER") v.values = {{70, "225"}};
        if (v.name == "$XCLIPFRAME") v.values = {{280, "0"}};
    }
    auto it = std::find_if(vars.begin(), vars.end(), [](const HeaderVar& v) { return v.name == "$DIMLTEX2"; });
    vars.insert(it + 1, HeaderVar{"$DIMTXTDIRECTION", {{70, "0"}}});
    return vars;
}

// DXF 2013 (plan phase 5, B57): 2010's variables, $ACADMAINTVER 125 and $REQUIREDVERSIONS (160 0)
// after $LASTSAVEDBY, as AutoCAD writes them (ACadSharp's sample_AC1027, jscad's empty2013.dxf)
inline std::vector<HeaderVar> default_r2013_headers() {
    std::vector<HeaderVar> vars = default_r2010_headers();
    for (auto& v : vars) {
        if (v.name == "$ACADVER") v.values = {{1, "AC1027"}};
        if (v.name == "$ACADMAINTVER") v.values = {{70, "125"}};
    }
    auto it = std::find_if(vars.begin(), vars.end(), [](const HeaderVar& v) { return v.name == "$LASTSAVEDBY"; });
    vars.insert(it + 1, HeaderVar{"$REQUIREDVERSIONS", {{160, "0"}}});
    return vars;
}

// DXF 2018 (plan phase 6, B70): the same 253 variables in the same order as 2013's, $ACADVER
// AC1032 and $ACADMAINTVER as 90 (a 70 until 2013) — 329, ACadSharp's sample_AC1032 (the
// saver's build number: a file read keeps its own)
inline std::vector<HeaderVar> default_r2018_headers() {
    std::vector<HeaderVar> vars = default_r2013_headers();
    for (auto& v : vars) {
        if (v.name == "$ACADVER") v.values = {{1, "AC1032"}};
        if (v.name == "$ACADMAINTVER") v.values = {{90, "329"}};
    }
    return vars;
}

// The header of a new R12 drawing: the variables of AutoCAD 2025's R12 file (doc/acad_r12_dxf.dxf),
// in its order. R12 files of older AutoCAD also have $DWGCODEPAGE, $SNAPMODE, $GRIDMODE,
// $VIEWCTR ... (the VPORT's values); a file read keeps whichever it had.
inline std::vector<HeaderVar> new_drawing_r12_headers() {
    static const char* const order[] = {
        "$ACADVER",
        "$INSBASE",
        "$EXTMIN",
        "$EXTMAX",
        "$LIMMIN",
        "$LIMMAX",
        "$ORTHOMODE",
        "$REGENMODE",
        "$FILLMODE",
        "$QTEXTMODE",
        "$MIRRTEXT",
        "$DRAGMODE",
        "$LTSCALE",
        "$OSMODE",
        "$ATTMODE",
        "$TEXTSIZE",
        "$TRACEWID",
        "$TEXTSTYLE",
        "$CLAYER",
        "$CELTYPE",
        "$CECOLOR",
        "$DIMSCALE",
        "$DIMASZ",
        "$DIMEXO",
        "$DIMDLI",
        "$DIMRND",
        "$DIMDLE",
        "$DIMEXE",
        "$DIMTP",
        "$DIMTM",
        "$DIMTXT",
        "$DIMCEN",
        "$DIMTSZ",
        "$DIMTOL",
        "$DIMLIM",
        "$DIMTIH",
        "$DIMTOH",
        "$DIMSE1",
        "$DIMSE2",
        "$DIMTAD",
        "$DIMZIN",
        "$DIMBLK",
        "$DIMASO",
        "$DIMSHO",
        "$DIMPOST",
        "$DIMAPOST",
        "$DIMALT",
        "$DIMALTD",
        "$DIMALTF",
        "$DIMLFAC",
        "$DIMTOFL",
        "$DIMTVP",
        "$DIMTIX",
        "$DIMSOXD",
        "$DIMSAH",
        "$DIMBLK1",
        "$DIMBLK2",
        "$DIMSTYLE",
        "$DIMCLRD",
        "$DIMCLRE",
        "$DIMCLRT",
        "$DIMTFAC",
        "$DIMGAP",
        "$LUNITS",
        "$LUPREC",
        "$SKETCHINC",
        "$FILLETRAD",
        "$AUNITS",
        "$AUPREC",
        "$MENU",
        "$ELEVATION",
        "$PELEVATION",
        "$THICKNESS",
        "$LIMCHECK",
        "$BLIPMODE",
        "$CHAMFERA",
        "$CHAMFERB",
        "$SKPOLY",
        "$TDCREATE",
        "$TDUPDATE",
        "$TDINDWG",
        "$TDUSRTIMER",
        "$USRTIMER",
        "$ANGBASE",
        "$ANGDIR",
        "$PDMODE",
        "$PDSIZE",
        "$PLINEWID",
        "$COORDS",
        "$SPLFRAME",
        "$SPLINETYPE",
        "$SPLINESEGS",
        "$ATTDIA",
        "$ATTREQ",
        "$HANDLING",
        "$HANDSEED",
        "$SURFTAB1",
        "$SURFTAB2",
        "$SURFTYPE",
        "$SURFU",
        "$SURFV",
        "$UCSNAME",
        "$UCSORG",
        "$UCSXDIR",
        "$UCSYDIR",
        "$PUCSNAME",
        "$PUCSORG",
        "$PUCSXDIR",
        "$PUCSYDIR",
        "$USERI1",
        "$USERI2",
        "$USERI3",
        "$USERI4",
        "$USERI5",
        "$USERR1",
        "$USERR2",
        "$USERR3",
        "$USERR4",
        "$USERR5",
        "$WORLDVIEW",
        "$SHADEDGE",
        "$SHADEDIF",
        "$TILEMODE",
        "$MAXACTVP",
        "$PLIMCHECK",
        "$PEXTMIN",
        "$PEXTMAX",
        "$PLIMMIN",
        "$PLIMMAX",
        "$UNITMODE",
        "$VISRETAIN",
        "$PLINEGEN",
        "$PSLTSCALE",
        "$MODEL_SPACE",
        "$MODEL_SPACE",
        "$PAPER_SPACE",
        "$PAPER_SPACE",
    };
    std::vector<HeaderVar> all = default_r12_headers();
    std::vector<HeaderVar> vars;
    for (const char* name : order) {
        for (auto& v : all) {
            if (v.name == name) {
                vars.push_back(v);
                break;
            }
        }
    }
    return vars;
}

} // namespace internal
} // namespace hdxf
