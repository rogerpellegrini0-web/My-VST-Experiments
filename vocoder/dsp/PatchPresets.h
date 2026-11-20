#pragma once
// Cross-patches from the Bode patch sheets and the Moog owner's manual.
// patch[j] = analyzer output feeding synthesizer input j (0-based), or -1 for no cord.

#include "Circuit.h"
#include <array>
#include <vector>

namespace vocoder
{
struct PatchPreset
{
    const char* name;
    const char* description;
    std::array<int, kNumChannels> patch;
};

inline std::vector<PatchPreset> patchPresets()
{
    std::vector<PatchPreset> v;
    auto make = [] (auto fn)
    {
        std::array<int, kNumChannels> a {};
        for (int j = 0; j < kNumChannels; ++j) a[(size_t) j] = fn (j);
        return a;
    };

    v.push_back ({ "Straight (1:1)", "Every analyzer output to its own synthesizer input",
                   make ([] (int j) { return j; }) });
    v.push_back ({ "Chipmunk / Donald Duck", "2-step diagonal, raises the formants (Bode sheet 1, Moog p. 9)",
                   make ([] (int j) { return j >= 2 ? j - 2 : -1; }) });
    v.push_back ({ "Formants Down", "2-step right-to-left diagonal, lowers the formants (Bode sheet 1)",
                   make ([] (int j) { return j + 2 < kNumChannels ? j + 2 : -1; }) });

    std::array<int, kNumChannels> robot {};
    robot.fill (-1);
    for (int a = 0; a < 7; ++a) robot[(size_t) a + 9] = a;      // 50..800 Hz -> 1270..5080 Hz
    robot[0] = 7; robot[1] = 8; robot[3] = 9; robot[4] = 10;    // 800..1600 Hz -> the bottom
    v.push_back ({ "Space Robot", "Moog manual p. 8", robot });

    v.push_back ({ "Single Cluster X", "Swaps the low and high halves: speech scrambling close to an instrumental sound (Bode sheet 1)",
                   make ([] (int j) { return (j + 8) % 16; }) });
    v.push_back ({ "Double Cluster X", "Swaps quarters within each half (Bode sheet 1)",
                   make ([] (int j) { return (j / 8) * 8 + ((j % 8) + 4) % 8; }) });
    v.push_back ({ "Single Cluster Inverter", "Mirror image of the spectrum (Bode sheet 2)",
                   make ([] (int j) { return 15 - j; }) });
    v.push_back ({ "Double Cluster Inverter", "Each half mirrored (Bode sheet 2)",
                   make ([] (int j) { return (j / 8) * 8 + 7 - (j % 8); }) });
    v.push_back ({ "No Cords", "Remove all patch cords",
                   make ([] (int) { return -1; }) });
    return v;
}
} // namespace vocoder
