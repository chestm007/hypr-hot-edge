#pragma once

#include <cctype>
#include <string>
#include <string_view>

// Support up to 16 configurations (8 zones — 4 edges + 4 corners — × 2 monitors)
constexpr int MAX_EDGE_SLOTS = 16;
constexpr const char* EDGE_SLOT_NAMES[] = {"edge1", "edge2", "edge3", "edge4", "edge5", "edge6", "edge7", "edge8",
                                           "edge9", "edge10", "edge11", "edge12", "edge13", "edge14", "edge15", "edge16"};
constexpr const char* SIDE_NAMES[] = {"left", "right", "top", "bottom",
                                      "topleft", "topright", "bottomleft", "bottomright"};

enum class EdgeSide {
    LEFT = 0,
    RIGHT = 1,
    TOP = 2,
    BOTTOM = 3,
    TOP_LEFT = 4,
    TOP_RIGHT = 5,
    BOTTOM_LEFT = 6,
    BOTTOM_RIGHT = 7
};

inline bool isCornerSide(EdgeSide side) {
    return side >= EdgeSide::TOP_LEFT;
}

inline EdgeSide parseEdgeSide(std::string_view value) {
    std::string lower(value);
    for (auto& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower == "left" || lower == "l" || lower == "0")
        return EdgeSide::LEFT;
    if (lower == "right" || lower == "r" || lower == "1")
        return EdgeSide::RIGHT;
    if (lower == "top" || lower == "t" || lower == "2")
        return EdgeSide::TOP;
    if (lower == "bottom" || lower == "b" || lower == "3")
        return EdgeSide::BOTTOM;
    if (lower == "topleft" || lower == "top-left" || lower == "tl" || lower == "4")
        return EdgeSide::TOP_LEFT;
    if (lower == "topright" || lower == "top-right" || lower == "tr" || lower == "5")
        return EdgeSide::TOP_RIGHT;
    if (lower == "bottomleft" || lower == "bottom-left" || lower == "bl" || lower == "6")
        return EdgeSide::BOTTOM_LEFT;
    if (lower == "bottomright" || lower == "bottom-right" || lower == "br" || lower == "7")
        return EdgeSide::BOTTOM_RIGHT;

    return EdgeSide::RIGHT;
}

// A corner zone would otherwise sit inside both of its composing edge zones, so
// sliding toward the corner clips the edge first and opens the wrong panel.
// Each configured corner instead carves its own width plus this margin out of
// both neighbouring edges, leaving a gap the edges never trigger in.
constexpr int DEFAULT_CORNER_MARGIN = 10;

struct EdgeConfig {
    EdgeSide side = EdgeSide::RIGHT;
    int triggerWidth = 15;
    int dwellTime = 150;
    std::string specialWorkspace;
    std::string targetMonitor;
    bool enabled = false;
    bool hideOnLeave = true;
};

// Both config frontends produce this canonical runtime model. Keep only
// source-independent normalisation here: each frontend retains its existing
// validation contract while shared defaults cannot drift.
inline EdgeConfig normalizeEdgeConfig(EdgeConfig config) {
    if (config.targetMonitor.empty())
        config.targetMonitor = "*";
    return config;
}
