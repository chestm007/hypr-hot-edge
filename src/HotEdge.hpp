#pragma once

#include "EdgeConfig.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>

#include <string>
#include <chrono>
#include <array>
#include <vector>
#include <optional>
#include <memory>

class LegacyConfig;

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

    // Lua config API (hl.plugin.hyprhotedge.*). Called from the Lua config
    // context by Hyprland itself, on the main thread.
    static int luaAddEdge(lua_State* L);
    static int luaSetCornerMargin(lua_State* L);

    // Temporary compatibility path for plugin:hot-edge:* values. Kept behind
    // one adapter so removing hyprlang support does not disturb Lua parsing or
    // runtime edge handling.
    void registerLegacyConfig();

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
    // How much an edge must give up at each end to the corners configured on
    // this monitor. .first is the low end (top for vertical edges, left for
    // horizontal), .second the high end. Always {0,0} for a corner side.
    std::pair<double, double> cornerInsets(PHLMONITOR monitor, EdgeSide side);

    static int parseEdgeArg(const std::string& arg);
    bool hasPendingState() const;
    void armPendingTimer();

    std::array<EdgeConfig, MAX_EDGE_SLOTS> m_edges;
    std::array<EdgeState, MAX_EDGE_SLOTS> m_states;

    // Canonical edges collected from hl.plugin.hyprhotedge.add_edge(). Cleared
    // before Lua re-executes and copied directly into m_edges after reload.
    std::vector<EdgeConfig> m_luaEdges;

    // All hyprlang-specific value storage and conversion lives in this
    // temporary adapter. See docs/legacy-config-removal.md.
    std::unique_ptr<LegacyConfig> m_legacyConfig;

    // Single global, not per-slot: it is a feel setting for how far edges keep
    // clear of corners, and a different value per edge would just make the
    // layout inconsistent. Lua takes precedence over the temporary legacy
    // adapter; the compiled default applies when neither source sets it.
    std::optional<int> m_cornerMarginSetting;
    int m_cornerMargin = DEFAULT_CORNER_MARGIN;

    bool m_active = true;

    // mouse.move is the only tick source, so a dwell or hide delay armed by the
    // final motion event would never elapse while the cursor sits perfectly
    // still — the panel simply never appeared until you jiggled the mouse. This
    // re-ticks while any slot has something pending, and stays disarmed otherwise.
    SP<CEventLoopTimer> m_pendingTimer;

    // Event-bus listener handles. Stored as members so they unregister when
    // CHotEdge is destroyed (in PLUGIN_EXIT) — replaces the previous static-local
    // pattern that prevented clean plugin unload/reload.
    CHyprSignalListener m_mouseMoveCb;
    CHyprSignalListener m_activeWindowCb;
    CHyprSignalListener m_configPreReloadCb;
    CHyprSignalListener m_configReloadedCb;
    CHyprSignalListener m_monitorRemovedCb;
};

inline std::unique_ptr<CHotEdge> g_pHotEdge;
