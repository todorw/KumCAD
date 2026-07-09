#include "core/io/DxfColors.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace lcad {

namespace {

Color fromHsv(double hueDegrees, double saturation, double value) {
    const double c = value * saturation;
    const double hp = hueDegrees / 60.0;
    const double x = c * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    const double m = value - c;
    auto channel = [m](double v) { return static_cast<std::uint8_t>(std::lround((v + m) * 255.0)); };
    return Color{channel(r), channel(g), channel(b)};
}

std::array<Color, 256> buildAciTable() {
    std::array<Color, 256> table{};
    table[0] = Color{255, 255, 255}; // 0 = BYBLOCK; render as white
    table[1] = Color{255, 0, 0};
    table[2] = Color{255, 255, 0};
    table[3] = Color{0, 255, 0};
    table[4] = Color{0, 255, 255};
    table[5] = Color{0, 0, 255};
    table[6] = Color{255, 0, 255};
    table[7] = Color{255, 255, 255};
    table[8] = Color{128, 128, 128};
    table[9] = Color{192, 192, 192};
    // 10-249: 24 hues x 5 brightness levels x {full, half} saturation.
    // Index layout: hue advances every 10 indices; within a group of 10,
    // even offsets step down in brightness and odd offsets are the pastel
    // (half-saturation) variant of the preceding even one.
    constexpr double kValueLevels[5] = {1.0, 0.8, 0.6, 0.5, 0.3};
    for (int i = 10; i <= 249; ++i) {
        const int offset = i - 10;
        const int hueGroup = offset / 10;
        const double hue = hueGroup * 15.0;
        const int shade = (offset % 10) / 2;
        const bool pastel = (offset % 2) != 0;
        table[i] = fromHsv(hue, pastel ? 0.5 : 1.0, kValueLevels[shade]);
    }
    // 250-255: dark-to-light grays.
    constexpr std::uint8_t kGrays[6] = {51, 91, 132, 173, 214, 255};
    for (int i = 250; i <= 255; ++i) {
        const std::uint8_t v = kGrays[i - 250];
        table[i] = Color{v, v, v};
    }
    return table;
}

const std::array<Color, 256>& aciTable() {
    static const std::array<Color, 256> table = buildAciTable();
    return table;
}

} // namespace

Color aciToColor(int aci) {
    if (aci < 1 || aci > 255) return Color{255, 255, 255};
    return aciTable()[static_cast<std::size_t>(aci)];
}

int colorToAci(const Color& color) {
    int best = 7;
    long bestDist = -1;
    for (int i = 1; i <= 255; ++i) {
        const Color& c = aciTable()[static_cast<std::size_t>(i)];
        const long dr = static_cast<long>(c.r) - color.r;
        const long dg = static_cast<long>(c.g) - color.g;
        const long db = static_cast<long>(c.b) - color.b;
        const long dist = dr * dr + dg * dg + db * db;
        if (bestDist < 0 || dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

} // namespace lcad
