#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>

#include <string>
#include <chrono>
#include <array>

enum class EdgeSide {
    LEFT = 0,
    RIGHT = 1,
    TOP = 2,
    BOTTOM = 3
};

// Support up to 8 edge configurations (enough for 4 edges × 2 monitors)
constexpr int MAX_EDGE_SLOTS = 8;
constexpr const char* EDGE_SLOT_NAMES[] = {"edge1", "edge2", "edge3", "edge4", "edge5", "edge6", "edge7", "edge8"};
constexpr const char* SIDE_NAMES[] = {"left", "right", "top", "bottom"};

struct EdgeConfig {
    EdgeSide side = EdgeSide::RIGHT;
    int triggerWidth = 15;          // pixels from edge to trigger
    int dwellTime = 150;            // ms to wait before triggering
    std::string specialWorkspace;   // name of special workspace to show
    std::string targetMonitor;      // monitor name ("*" = all, "DP-1" = specific)
    bool enabled = false;
};

// Handles returned by addConfigValueV2. Hyprland 0.56 deprecated the
// name-lookup API (getConfigValue), which returns nullptr under the Lua config
// manager — dereferencing that is what SIGSEGV'd the compositor. Holding the
// value objects instead removes the lookup, so there is nothing to be null.
struct EdgeConfigValues {
    SP<Config::Values::Int>    enabled;
    SP<Config::Values::String> side;
    SP<Config::Values::Int>    triggerWidth;
    SP<Config::Values::Int>    dwellTime;
    SP<Config::Values::String> specialWorkspace;
    SP<Config::Values::String> targetMonitor;
};

struct EdgeState {
    bool bCursorInZone = false;
    bool bDwelling = false;
    std::chrono::steady_clock::time_point dwellStart;
    bool bHideDelaying = false;
    std::chrono::steady_clock::time_point hideDelayStart;

    // Track which monitor the overlay was opened on
    std::string activeMonitorName;

    // Grace period after keyboard toggle (prevents immediate auto-hide)
    bool bJustShown = false;
    std::chrono::steady_clock::time_point showTime;
};

class CHotEdge {
public:
    CHotEdge();
    ~CHotEdge();

    void init();
    void registerConfigValues();   // must run inside pluginInit, before init()
    void registerCallbacks();
    void onTick();
    void onActiveWindowChange(PHLWINDOW pWindow);
    void onMonitorRemoved(PHLMONITOR pMonitor);

    // Dispatchers - args can be empty (all edges), edge slot ("edge1"), or side name ("right")
    static SDispatchResult dispatchToggle(std::string args);
    static SDispatchResult dispatchShow(std::string args);
    static SDispatchResult dispatchHide(std::string args);

    // Runtime activation - gates cursor-driven automation only.
    // Explicit dispatchers (toggle/show/hide) and existing visible panels are untouched.
    static SDispatchResult dispatchEnable(std::string args);
    static SDispatchResult dispatchDisable(std::string args);
    static SDispatchResult dispatchToggleActive(std::string args);

    // Configuration
    void reloadConfig();

    // State
    bool isOverlayVisible(int slotIndex) const;
    bool isOverlayVisibleOnMonitor(int slotIndex, PHLMONITOR monitor) const;
    bool isAnyOverlayVisible() const;

private:
    void processEdge(int slotIndex);
    bool isCursorInEdgeZone(int slotIndex, PHLMONITOR monitor);
    bool isCursorInPanelArea(int slotIndex, PHLMONITOR monitor);
    bool isCursorAtScreenEdge(int slotIndex, PHLMONITOR monitor);
    bool isEdgeEnabledForMonitor(int slotIndex, PHLMONITOR monitor);
    void showOverlay(int slotIndex);
    void hideOverlay(int slotIndex);
    PHLMONITOR getCurrentMonitor();
    PHLMONITOR getMonitorByName(const std::string& name);
    CBox getEdgeZone(PHLMONITOR monitor, EdgeSide side, int triggerWidth);
    CBox getPanelArea(PHLMONITOR monitor, EdgeSide side);

    static int parseEdgeArg(const std::string& arg);
    static EdgeSide parseSide(const std::string& str);

    std::array<EdgeConfig, MAX_EDGE_SLOTS> m_edges;
    std::array<EdgeState, MAX_EDGE_SLOTS> m_states;
    std::array<EdgeConfigValues, MAX_EDGE_SLOTS> m_configValues;

    bool m_active = true;

    // Event-bus listener handles. Stored as members so they unregister when
    // CHotEdge is destroyed (in PLUGIN_EXIT) — replaces the previous static-local
    // pattern that prevented clean plugin unload/reload.
    CHyprSignalListener m_mouseMoveCb;
    CHyprSignalListener m_activeWindowCb;
    CHyprSignalListener m_configReloadedCb;
    CHyprSignalListener m_monitorRemovedCb;
};

inline std::unique_ptr<CHotEdge> g_pHotEdge;
