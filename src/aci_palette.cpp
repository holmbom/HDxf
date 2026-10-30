#include "hdxf.hpp"
#include <array>

// AutoCAD's ACI palette (plan phase 2, B21). It follows a pattern: 1-9 the basic colors,
// 10-249 24 hues 15 degrees apart, each at the brightness 255/204/153/127/76 with full
// (even index) and half (odd) saturation, the values truncated; 250-255 greys. Checked
// against every pair 62/420 and 63/421 AutoCAD wrote in ACadSharp's 2004-2018 files: the
// nearest index by distance in RGB is AutoCAD's choice for all 18.

namespace hdxf {

namespace {

std::array<int, 256> make_palette() {
    std::array<int, 256> p{};
    auto rgb = [](int r, int g, int b) { return (r << 16) | (g << 8) | b; };
    const int basic[10][3] = {{0, 0, 0},     {255, 0, 0},   {255, 255, 0},   {0, 255, 0},
                              {0, 255, 255}, {0, 0, 255},   {255, 0, 255},   {255, 255, 255},
                              {128, 128, 128}, {192, 192, 192}};
    for (int i = 0; i < 10; ++i) p[i] = rgb(basic[i][0], basic[i][1], basic[i][2]);
    const int brightness[5] = {255, 204, 153, 127, 76};
    for (int i = 10; i < 250; ++i) {
        int hue = (i - 10) / 10 * 15;
        double v = brightness[(i % 10) / 2];
        double low = i % 2 ? v / 2.0 : 0.0;
        double f = (hue % 60) / 60.0;
        double up = low + (v - low) * f;
        double down = low + (v - low) * (1.0 - f);
        double r = 0, g = 0, b = 0;
        switch (hue / 60) {
            case 0: r = v;    g = up;   b = low;  break;
            case 1: r = down; g = v;    b = low;  break;
            case 2: r = low;  g = v;    b = up;   break;
            case 3: r = low;  g = down; b = v;    break;
            case 4: r = up;   g = low;  b = v;    break;
            default: r = v;   g = low;  b = down; break;
        }
        p[i] = rgb(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
    }
    const int grey[6] = {51, 80, 105, 130, 190, 255};
    for (int i = 0; i < 6; ++i) p[250 + i] = rgb(grey[i], grey[i], grey[i]);
    return p;
}

const std::array<int, 256>& palette() {
    static const std::array<int, 256> p = make_palette();
    return p;
}

} // namespace

int aci_rgb(int aci) {
    if (aci < 1 || aci > 255) return -1;    // ByBlock, ByLayer: no color of their own
    return palette()[static_cast<size_t>(aci)];
}

int nearest_aci(int rgb) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int best = 1;
    long best_distance = -1;
    for (int i = 1; i < 256; ++i) {
        int c = palette()[static_cast<size_t>(i)];
        long dr = ((c >> 16) & 0xFF) - r, dg = ((c >> 8) & 0xFF) - g, db = (c & 0xFF) - b;
        long d = dr * dr + dg * dg + db * db;
        if (best_distance < 0 || d < best_distance) {
            best = i;
            best_distance = d;
        }
    }
    return best;
}

} // namespace hdxf
