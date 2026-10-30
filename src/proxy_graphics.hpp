#pragma once

#include "hdxf.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Proxy graphics: what an entity draws, as AutoCAD writes it for programs that do not know
// the entity (92/160 and 310 in AcDbEntity). The format is AcGi's worldDraw() as a stream:
// the total size and the number of commands, then each command as its size, its number
// and its data, little-endian (Open Design Alliance's "Open Design Specification for .dwg
// files", chapter "Proxy entity graphics", checked against AutoCAD's MULTILEADERs in
// ACadSharp's samples and in the files of test/data/README.md).

namespace hdxf {
namespace internal {

// A line of text as AcGi draws it (TEXT2, 11, and its Unicode form, 38): from where its pen
// starts, along dir in the plane of normal
struct ProxyText {
    Point3 position;
    Point3 normal = {0.0, 0.0, 1.0};
    Point3 direction = {1.0, 0.0, 0.0};
    std::string text;                   // UTF-8, the characters drawn
    double height = 0.0;
    double width_factor = 1.0;
    double oblique = 0.0;               // radians
    double tracking = 1.0;
    bool backward = false;
    bool upside_down = false;
    bool vertical = false;
    bool underline = false;
    bool overline = false;
    // The font: a TrueType face by name (Unicode form only) with its bold, italic and
    // pitch and family, or a font file; and a big font file
    bool bold = false;
    bool italic = false;
    int charset = 1;
    int pitch_and_family = 0;
    std::string face;
    std::string font;
    std::string big_font;
};

// An LWPOLYLINE as AcGi's pline() records it (33): its data as in a DWG file (ODA's
// specification, LWPLINE) — flags, constant width, elevation, thickness, normal, the points,
// their bulges and widths
std::vector<uint8_t> lwpline_data(const LwPolylineData& d, double thickness, const Point3& normal);

class ProxyGraphics {
public:
    void circle(const Point3& center, double radius, const Point3& normal);              // 2
    void arc(const Point3& center, double radius, const Point3& normal, const Point3& start,
             double sweep, int arc_type);                                                // 4
    // A circle through three points (3) and a circular arc from its first point through its second to its third
    // (5, then its arc type 0): AutoCAD's 2000 and 2004 surfaces draw their circles and arcs so (plan 9.16)
    void circle3(const Point3& a, const Point3& b, const Point3& c);                    // 3
    void arc3(const Point3& a, const Point3& b, const Point3& c);                       // 5
    void polyline(const std::vector<Point3>& points);                                   // 6
    void polygon(const std::vector<Point3>& points);                                    // 7
    // A mesh of rows x columns vertices (row by row), then its traits as AcGi writes them:
    // the edges' flags and data, the faces' flags and data (8)
    void mesh(int rows, int columns, const std::vector<Point3>& vertices, const std::vector<int32_t>& traits);
    // A shell: its vertices, its face list (each face: its vertex count — negative for a
    // hole — and the vertices' indices), then its traits: the edges', the faces' and the
    // vertices' flags and data (9)
    void shell(const std::vector<Point3>& vertices, const std::vector<int32_t>& faces,
               const std::vector<int32_t>& traits);
    void xline(const Point3& base, const Point3& through);                              // 12
    void ray(const Point3& base, const Point3& through);                                // 13
    void thickness(double thickness);                                                    // 25
    // A polyline in the plane of normal (32)
    void polyline_normal(const std::vector<Point3>& points, const Point3& normal);
    // lwpline_data()'s bytes (33): their count, the bytes and three zero bytes, as ODA's
    // File Converter writes it (no AutoCAD file has one)
    void lwpolyline(const std::vector<uint8_t>& data);
    // An elliptical arc (44, not in ODA's specification; AutoCAD's 2004 surfaces): its center
    // and normal, its radii, its start and end parameters (radians) and the major axis's angle
    // from the normal's OCS x axis
    void ellipse_arc(const Point3& center, const Point3& normal, double major_radius, double minor_radius,
                     double start, double end, double angle, int arc_type);
    // 11 (code_page: the drawing's, what the text is written in) or 38 (unicode)
    void text(const ProxyText& t, bool unicode, int code_page);
    void color(int aci);                                                                 // 14
    void layer(int index);                                                               // 16
    void linetype(int index);                                                            // 18
    void marker(int id);                                                                 // 19
    void fill(bool on);                                                                  // 20
    void true_color(int32_t raw);                                                        // 22
    void lineweight(int32_t lineweight);                                                 // 23
    void push_transform(const std::array<double, 16>& matrix);                           // 29
    void pop_transform();                                                                // 31
    // 51: AutoCAD writes it, with 0, around a MULTILEADER's parts; not in the
    // specification
    void command_51();
    // --- 9.5: the traits AutoCAD records before each entity inside an INSERT of a table's block (observed in its
    // files, not in ODA's specification): the linetype scale (24), 26 (a real, 0 in every file), 34 (a 64-bit
    // number, 0x96 in every file) and 51 with a value (1 for a text, 0 for the rest)
    void linetype_scale(double scale);                                                   // 24
    void command_26(double value);                                                       // 26
    void command_34(int64_t value);                                                      // 34
    void command_51(int value);                                                          // 51

    // The commands of another stream (an entity's own proxy graphics) after these
    void append(const std::vector<uint8_t>& stream);

    bool empty() const { return count_ == 0; }
    // The whole stream: total size, number of commands, the commands
    std::vector<uint8_t> bytes() const;

private:
    void begin(int type);
    void end();
    void put_int(int32_t v);
    void put_double(double v);
    void put_point(const Point3& p);
    void put_string(const std::string& s);          // as given, 0-terminated, padded to 4
    void put_wide_string(const std::string& utf8);  // UTF-16, 0-terminated, padded to 4

    std::vector<uint8_t> data_;
    size_t open_ = 0;
    int count_ = 0;
};

} // namespace internal
} // namespace hdxf
