// Fixed-grid DWDM channel plan helpers (ITU-T G.694.1, 50 GHz C-band grid).
#pragma once

#include <cstdint>
#include <optional>

namespace onsim {

// Channel indices 1..96 map onto the 50 GHz C-band grid starting at 191.35 THz.
class WavelengthGrid {
public:
    static constexpr int kFirstChannel = 1;
    static constexpr int kLastChannel = 96;
    static constexpr double kFirstCenterThz = 191.35;
    static constexpr double kSpacingThz = 0.05;  // 50 GHz

    static bool isValidChannel(int ch) {
        return ch >= kFirstChannel && ch <= kLastChannel;
    }

    // Center frequency in THz, or nullopt for an invalid channel.
    static std::optional<double> centerFrequencyThz(int ch) {
        if (!isValidChannel(ch)) return std::nullopt;
        return kFirstCenterThz + (ch - kFirstChannel) * kSpacingThz;
    }
};

}  // namespace onsim
