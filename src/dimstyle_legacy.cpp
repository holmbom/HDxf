#include "dimstyle_legacy.hpp"
#include "header_defaults.hpp"

namespace hdxf {
namespace internal {

std::optional<UnitFormat> modern_from_dimunit(int dimunit) {
    switch (dimunit) {
        case 1: return UnitFormat{1, 0};
        case 2: return UnitFormat{2, 0};
        case 3: return UnitFormat{3, 0};
        case 4: return UnitFormat{4, 0};
        case 5: return UnitFormat{5, 0};
        case 6: return UnitFormat{4, 2};
        case 7: return UnitFormat{5, 2};
        case 8: return UnitFormat{6, 0};
        default: return std::nullopt;
    }
}

std::optional<FitRule> modern_from_dimfit(int dimfit) {
    if (dimfit >= 0 && dimfit <= 3) return FitRule{dimfit, 0};
    if (dimfit == 4) return FitRule{3, 1};
    if (dimfit == 5) return FitRule{3, 2};
    return std::nullopt;
}

std::optional<int> legacy_dimunit(int dimlunit, int dimfrac) {
    switch (dimlunit) {
        case 1: case 2: case 3: return dimlunit;      // DIMFRAC means nothing to them
        case 6: return 8;
        case 4: case 5:
            if (dimfrac == 0) return dimlunit;
            if (dimfrac == 2) return dimlunit + 2;    // 6 and 7: not stacked
            return std::nullopt;                      // 1 diagonal: DIMUNIT cannot say it
        default: return std::nullopt;
    }
}

std::optional<int> legacy_dimfit(int dimatfit, int dimtmove) {
    if (dimatfit < 0 || dimatfit > 3) return std::nullopt;
    if (dimtmove == 0) return dimatfit;
    if (dimatfit == 3 && dimtmove == 1) return 4;
    if (dimatfit == 3 && dimtmove == 2) return 5;
    return std::nullopt;
}

std::optional<int> dimunit_of(const DimStyle& style) {
    if (style.dimunit) {
        std::optional<UnitFormat> read = modern_from_dimunit(*style.dimunit);
        const DimStyle d0 = autocad_dim_style();
        // A value outside 1 … 8 left DIMLUNIT and DIMFRAC alone: it is still the value while they are
        bool same = read ? read->dimlunit == style.dimlunit && read->dimfrac == style.dimfrac
                         : style.dimlunit == d0.dimlunit && style.dimfrac == d0.dimfrac;
        if (same) return style.dimunit;
    }
    return legacy_dimunit(style.dimlunit, style.dimfrac);
}

std::optional<int> dimfit_of(const DimStyle& style) {
    if (style.dimfit) {
        std::optional<FitRule> read = modern_from_dimfit(*style.dimfit);
        const DimStyle d0 = autocad_dim_style();
        bool same = read ? read->dimatfit == style.dimatfit && read->dimtmove == style.dimtmove
                         : style.dimatfit == d0.dimatfit && style.dimtmove == d0.dimtmove;
        if (same) return style.dimfit;
    }
    return legacy_dimfit(style.dimatfit, style.dimtmove);
}

void set_dimunit(DimStyle& style, int dimunit) {
    style.dimunit = dimunit;
    if (std::optional<UnitFormat> f = modern_from_dimunit(dimunit)) {
        style.dimlunit = f->dimlunit;
        style.dimfrac = f->dimfrac;
    }
}

void set_dimfit(DimStyle& style, int dimfit) {
    style.dimfit = dimfit;
    if (std::optional<FitRule> r = modern_from_dimfit(dimfit)) {
        style.dimatfit = r->dimatfit;
        style.dimtmove = r->dimtmove;
    }
}

std::string dimstyle_older_error(const DimStyle& s) {
    const DimStyle d0 = autocad_dim_style();
    const std::string where = "Dimension style \"" + s.name + "\": ";
    auto later = [&](const char* what) { return where + what + " requires DXF 2000 or later"; };

    // The variables 2000 added that DIMSTYLE has no group for in R13 and R14 (their headers have
    // none of them either): any value but AutoCAD's own
    if (s.dimaltrnd != d0.dimaltrnd) return later("DIMALTRND");
    if (s.dimazin != d0.dimazin) return later("DIMAZIN");
    if (s.dimadec != d0.dimadec) return later("DIMADEC");
    if (s.dimdsep != d0.dimdsep) return later("DIMDSEP");
    if (!s.dimldrblk.empty()) return later("DIMLDRBLK");
    if (s.dimlwd != d0.dimlwd) return later("DIMLWD");
    if (s.dimlwe != d0.dimlwe) return later("DIMLWE");
    if (s.dimtfill != d0.dimtfill) return later("DIMTFILL");
    if (s.dimtfillclr.aci != d0.dimtfillclr.aci || s.dimtfillclr.rgb != d0.dimtfillclr.rgb ||
        !s.dimtfillclr.name.empty()) return later("DIMTFILLCLR");
    if (!s.dimltype.empty()) return later("DIMLTYPE");
    if (!s.dimltex1.empty()) return later("DIMLTEX1");
    if (!s.dimltex2.empty()) return later("DIMLTEX2");
    if (s.dimfxlon != d0.dimfxlon) return later("DIMFXLON");
    if (s.dimfxl != d0.dimfxl) return later("DIMFXL");
    if (s.dimjogang != d0.dimjogang) return later("DIMJOGANG");
    if (s.dimtxtdirection != d0.dimtxtdirection) return later("DIMTXTDIRECTION");
    if (s.dimarcsym != d0.dimarcsym) return later("DIMARCSYM");

    // DIMLUNIT and DIMFRAC as DIMUNIT, DIMATFIT and DIMTMOVE as DIMFIT
    if (!dimunit_of(s)) {
        if ((s.dimlunit == 4 || s.dimlunit == 5) && s.dimfrac == 1) {
            return later("DIMFRAC (diagonal stacking)");
        }
        return where + "DIMLUNIT " + std::to_string(s.dimlunit) + " with DIMFRAC " +
               std::to_string(s.dimfrac) + " has no DIMUNIT in DXF R13 or R14";
    }
    if (!dimfit_of(s)) {
        if (s.dimatfit >= 0 && s.dimatfit <= 3) return later("DIMTMOVE (without DIMATFIT 3)");
        return where + "DIMATFIT " + std::to_string(s.dimatfit) + " has no DIMFIT in DXF R13 or R14";
    }
    return {};
}

std::vector<LegacyGroup> legacy_dimstyle_groups(const DimStyle& s, const std::string& text_style_handle,
                                                std::string& error) {
    std::vector<LegacyGroup> out;
    error = dimstyle_older_error(s);
    if (!error.empty()) return out;

    auto str = [&](int code, const std::string& v) {
        LegacyGroup g;
        g.code = code;
        g.kind = LegacyGroup::Kind::String;
        g.text = v;
        out.push_back(std::move(g));
    };
    auto num = [&](int code, int v) {
        LegacyGroup g;
        g.code = code;
        g.kind = LegacyGroup::Kind::Int;
        g.integer = v;
        out.push_back(std::move(g));
    };
    auto dbl = [&](int code, double v) {
        LegacyGroup g;
        g.code = code;
        g.kind = LegacyGroup::Kind::Double;
        g.real = v;
        out.push_back(std::move(g));
    };

    str(2, s.name);
    num(70, s.flags);
    str(3, s.dimpost);
    str(4, s.dimapost);
    str(5, s.dimblk);       // the block's name, NOT a handle (5 is the handle of every other record)
    str(6, s.dimblk1);
    str(7, s.dimblk2);
    dbl(40, s.dimscale);
    dbl(41, s.dimasz);
    dbl(42, s.dimexo);
    dbl(43, s.dimdli);
    dbl(44, s.dimexe);
    dbl(45, s.dimrnd);
    dbl(46, s.dimdle);
    dbl(47, s.dimtp);
    dbl(48, s.dimtm);
    dbl(140, s.dimtxt);
    dbl(141, s.dimcen);
    dbl(142, s.dimtsz);
    dbl(143, s.dimaltf);
    dbl(144, s.dimlfac);
    dbl(145, s.dimtvp);
    dbl(146, s.dimtfac);
    dbl(147, s.dimgap);
    num(71, s.dimtol);
    num(72, s.dimlim);
    num(73, s.dimtih);
    num(74, s.dimtoh);
    num(75, s.dimse1);
    num(76, s.dimse2);
    num(77, s.dimtad);
    num(78, s.dimzin);
    num(170, s.dimalt);
    num(171, s.dimaltd);
    num(172, s.dimtofl);
    num(173, s.dimsah);
    num(174, s.dimtix);
    num(175, s.dimsoxd);
    num(176, s.dimclrd);
    num(177, s.dimclre);
    num(178, s.dimclrt);
    num(270, *dimunit_of(s));
    num(271, s.dimdec);
    num(272, s.dimtdec);
    num(273, s.dimaltu);
    num(274, s.dimalttd);
    str(340, text_style_handle);
    num(275, s.dimaunit);
    num(280, s.dimjust);
    num(281, s.dimsd1);
    num(282, s.dimsd2);
    num(283, s.dimtolj);
    num(284, s.dimtzin);
    num(285, s.dimaltz);
    num(286, s.dimalttz);
    num(287, *dimfit_of(s));
    num(288, s.dimupt);
    return out;
}

} // namespace internal
} // namespace hdxf
