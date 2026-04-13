#include "HotEdge.hpp"
#include "globals.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprutils/math/Box.hpp>

using namespace std::chrono;

CHotEdge::CHotEdge() {
    Log::logger->log(Log::DEBUG, "[HotEdge] Initializing...");
}

CHotEdge::~CHotEdge() {
    Log::logger->log(Log::DEBUG, "[HotEdge] Destroying...");
}

void CHotEdge::init() {
    reloadConfig();

    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        if (m_edges[i].enabled) {
            Log::logger->log(Log::DEBUG, "[HotEdge] Slot {} enabled: side={}, trigger={}px, dwell={}ms, workspace={}, monitor={}",
                EDGE_SLOT_NAMES[i], SIDE_NAMES[static_cast<int>(m_edges[i].side)],
                m_edges[i].triggerWidth, m_edges[i].dwellTime,
                m_edges[i].specialWorkspace, m_edges[i].targetMonitor);
        }
    }
}

EdgeSide CHotEdge::parseSide(const std::string& str) {
    std::string lower = str;
    for (auto& c : lower) c = std::tolower(c);

    if (lower == "left" || lower == "l" || lower == "0")
        return EdgeSide::LEFT;
    if (lower == "right" || lower == "r" || lower == "1")
        return EdgeSide::RIGHT;
    if (lower == "top" || lower == "t" || lower == "2")
        return EdgeSide::TOP;
    if (lower == "bottom" || lower == "b" || lower == "3")
        return EdgeSide::BOTTOM;

    return EdgeSide::RIGHT;  // Default
}

void CHotEdge::reloadConfig() {
    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        std::string prefix = std::string("plugin:hot-edge:") + EDGE_SLOT_NAMES[i] + ":";

        static std::array<Hyprlang::INT* const*, MAX_EDGE_SLOTS> pEnabled;
        static std::array<Hyprlang::STRING const*, MAX_EDGE_SLOTS> pSide;
        static std::array<Hyprlang::INT* const*, MAX_EDGE_SLOTS> pWidth;
        static std::array<Hyprlang::INT* const*, MAX_EDGE_SLOTS> pDwell;
        static std::array<Hyprlang::STRING const*, MAX_EDGE_SLOTS> pWorkspace;
        static std::array<Hyprlang::STRING const*, MAX_EDGE_SLOTS> pMonitor;

        pEnabled[i] = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, (prefix + "enabled").c_str())->getDataStaticPtr();
        pSide[i] = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, (prefix + "side").c_str())->getDataStaticPtr();
        pWidth[i] = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, (prefix + "trigger_width").c_str())->getDataStaticPtr();
        pDwell[i] = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, (prefix + "dwell_time").c_str())->getDataStaticPtr();
        pWorkspace[i] = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, (prefix + "special_workspace").c_str())->getDataStaticPtr();
        pMonitor[i] = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, (prefix + "target_monitor").c_str())->getDataStaticPtr();

        m_edges[i].enabled = **pEnabled[i];
        m_edges[i].side = parseSide(*pSide[i]);
        m_edges[i].triggerWidth = **pWidth[i];
        m_edges[i].dwellTime = **pDwell[i];
        m_edges[i].specialWorkspace = *pWorkspace[i];
        m_edges[i].targetMonitor = *pMonitor[i];

        // Default to "*" (all monitors) if not specified
        if (m_edges[i].targetMonitor.empty())
            m_edges[i].targetMonitor = "*";
    }
}

PHLMONITOR CHotEdge::getCurrentMonitor() {
    return g_pCompositor->getMonitorFromCursor();
}

PHLMONITOR CHotEdge::getMonitorByName(const std::string& name) {
    for (auto& m : g_pCompositor->m_monitors) {
        if (m->m_name == name)
            return m;
    }
    return nullptr;
}

bool CHotEdge::isEdgeEnabledForMonitor(int slotIndex, PHLMONITOR monitor) {
    if (!monitor || slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return false;

    const auto& config = m_edges[slotIndex];
    if (!config.enabled)
        return false;

    // "*" means all monitors
    if (config.targetMonitor == "*")
        return true;

    // Check if monitor name matches
    return config.targetMonitor == monitor->m_name;
}

bool CHotEdge::isOverlayVisible(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return false;

    auto monitor = g_pCompositor->getMonitorFromCursor();
    if (!monitor || !monitor->m_activeSpecialWorkspace)
        return false;

    std::string specialName = "special:" + m_edges[slotIndex].specialWorkspace;
    return monitor->m_activeSpecialWorkspace->m_name == specialName;
}

bool CHotEdge::isOverlayVisibleOnMonitor(int slotIndex, PHLMONITOR monitor) const {
    if (slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS || !monitor)
        return false;

    if (!monitor->m_activeSpecialWorkspace)
        return false;

    std::string specialName = "special:" + m_edges[slotIndex].specialWorkspace;
    return monitor->m_activeSpecialWorkspace->m_name == specialName;
}

bool CHotEdge::isAnyOverlayVisible() const {
    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        if (m_edges[i].enabled && isOverlayVisible(i))
            return true;
    }
    return false;
}

CBox CHotEdge::getEdgeZone(PHLMONITOR monitor, EdgeSide side, int triggerWidth) {
    if (!monitor)
        return CBox{0, 0, 0, 0};

    const auto& pos = monitor->m_position;
    const auto& size = monitor->m_size;

    switch (side) {
        case EdgeSide::LEFT:
            return CBox{pos.x, pos.y, (double)triggerWidth, size.y};
        case EdgeSide::RIGHT:
            return CBox{pos.x + size.x - triggerWidth, pos.y, (double)triggerWidth, size.y};
        case EdgeSide::TOP:
            return CBox{pos.x, pos.y, size.x, (double)triggerWidth};
        case EdgeSide::BOTTOM:
            return CBox{pos.x, pos.y + size.y - triggerWidth, size.x, (double)triggerWidth};
    }
    return CBox{0, 0, 0, 0};
}

CBox CHotEdge::getPanelArea(PHLMONITOR monitor, EdgeSide side) {
    if (!monitor)
        return CBox{0, 0, 0, 0};

    const auto& pos = monitor->m_position;
    const auto& size = monitor->m_size;

    // Panel takes up 1/3 of screen from the edge side
    double panelWidth = size.x / 3.0;
    double panelHeight = size.y / 3.0;

    switch (side) {
        case EdgeSide::LEFT:
            return CBox{pos.x, pos.y, panelWidth, size.y};
        case EdgeSide::RIGHT:
            return CBox{pos.x + size.x - panelWidth, pos.y, panelWidth, size.y};
        case EdgeSide::TOP:
            return CBox{pos.x, pos.y, size.x, panelHeight};
        case EdgeSide::BOTTOM:
            return CBox{pos.x, pos.y + size.y - panelHeight, size.x, panelHeight};
    }
    return CBox{0, 0, 0, 0};
}

bool CHotEdge::isCursorInPanelArea(int slotIndex, PHLMONITOR monitor) {
    if (!monitor || slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return false;

    const auto cursorPos = g_pInputManager->getMouseCoordsInternal();
    const auto panelArea = getPanelArea(monitor, m_edges[slotIndex].side);

    return panelArea.containsPoint(cursorPos);
}

bool CHotEdge::isCursorInEdgeZone(int slotIndex, PHLMONITOR monitor) {
    if (!monitor || slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return false;

    const auto cursorPos = g_pInputManager->getMouseCoordsInternal();
    const auto zone = getEdgeZone(monitor, m_edges[slotIndex].side, m_edges[slotIndex].triggerWidth);

    return zone.containsPoint(cursorPos);
}

bool CHotEdge::isCursorAtScreenEdge(int slotIndex, PHLMONITOR monitor) {
    if (!monitor || slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return false;

    const auto cursorPos = g_pInputManager->getMouseCoordsInternal();
    const auto& pos = monitor->m_position;
    const auto& size = monitor->m_size;

    // Check if cursor is at the absolute screen boundary (within 2-3 pixels)
    switch (m_edges[slotIndex].side) {
        case EdgeSide::LEFT:
            return cursorPos.x <= pos.x + 2;
        case EdgeSide::RIGHT:
            return cursorPos.x >= pos.x + size.x - 3;
        case EdgeSide::TOP:
            return cursorPos.y <= pos.y + 2;
        case EdgeSide::BOTTOM:
            return cursorPos.y >= pos.y + size.y - 3;
    }
    return false;
}

void CHotEdge::onTick() {
    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        if (m_edges[i].enabled)
            processEdge(i);
    }
}

void CHotEdge::processEdge(int slotIndex) {
    auto& config = m_edges[slotIndex];
    auto& state = m_states[slotIndex];

    auto cursorMonitor = getCurrentMonitor();
    if (!cursorMonitor)
        return;

    // Check if this edge is enabled for the current monitor
    bool enabledForThisMonitor = isEdgeEnabledForMonitor(slotIndex, cursorMonitor);

    // Get the monitor where the overlay was opened (if any)
    PHLMONITOR activeMonitor = nullptr;
    if (!state.activeMonitorName.empty()) {
        activeMonitor = getMonitorByName(state.activeMonitorName);
    }

    // Check visibility on the active monitor (where overlay was opened)
    bool visible = false;
    if (activeMonitor) {
        visible = isOverlayVisibleOnMonitor(slotIndex, activeMonitor);
    } else {
        visible = isOverlayVisible(slotIndex);
    }

    // If overlay is visible but cursor moved to a different monitor, close it
    if (visible && activeMonitor && cursorMonitor != activeMonitor) {
        // Cursor left the monitor where overlay is shown - hide immediately
        Log::logger->log(Log::DEBUG, "[HotEdge] Cursor left monitor {}, hiding {} overlay",
            activeMonitor->m_name, EDGE_SLOT_NAMES[slotIndex]);
        hideOverlay(slotIndex);
        state.activeMonitorName.clear();
        state.bCursorInZone = false;
        state.bDwelling = false;
        state.bHideDelaying = false;
        return;
    }

    // Skip edge detection if this edge is not enabled for current monitor
    if (!enabledForThisMonitor) {
        // Reset state if we were tracking this edge
        if (state.bCursorInZone || state.bDwelling) {
            state.bCursorInZone = false;
            state.bDwelling = false;
        }
        return;
    }

    bool inZone = isCursorInEdgeZone(slotIndex, cursorMonitor);
    bool atEdge = isCursorAtScreenEdge(slotIndex, cursorMonitor);
    bool inPanel = isCursorInPanelArea(slotIndex, cursorMonitor);

    // If cursor is at the absolute screen edge, trigger immediately (no dwell)
    if (atEdge && !visible) {
        showOverlay(slotIndex);
        state.activeMonitorName = cursorMonitor->m_name;
        state.bDwelling = false;
        state.bCursorInZone = true;
        return;
    }

    if (inZone && !state.bCursorInZone) {
        // Just entered the edge zone
        if (!visible) {
            state.bDwelling = true;
            state.dwellStart = steady_clock::now();
        }
        state.bCursorInZone = true;
    } else if (!inZone && !atEdge && state.bCursorInZone) {
        // Left the edge zone
        state.bDwelling = false;
        state.bCursorInZone = false;
    }

    // Check dwell time - only show if not already visible
    if (state.bDwelling && !visible) {
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - state.dwellStart).count();
        if (elapsed >= config.dwellTime) {
            showOverlay(slotIndex);
            state.activeMonitorName = cursorMonitor->m_name;
            state.bDwelling = false;
        }
    }

    // Check grace period - skip auto-hide if overlay was just shown (keyboard toggle)
    if (state.bJustShown) {
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - state.showTime).count();
        if (elapsed >= 800) {  // 800ms grace period
            state.bJustShown = false;
        } else if (inPanel) {
            // Cursor entered panel, clear grace period early
            state.bJustShown = false;
        } else {
            // Still in grace period, skip auto-hide
            return;
        }
    }

    // Auto-hide when cursor leaves panel area (but not if at edge)
    // Use a small delay to allow animation and prevent accidental closes
    if (visible && !inPanel && !inZone && !atEdge) {
        if (!state.bHideDelaying) {
            state.bHideDelaying = true;
            state.hideDelayStart = steady_clock::now();
        } else {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - state.hideDelayStart).count();
            if (elapsed >= 150) {  // 150ms delay before hiding
                hideOverlay(slotIndex);
                state.activeMonitorName.clear();
                state.bHideDelaying = false;
            }
        }
    } else {
        // Cursor back in panel, cancel hide delay
        state.bHideDelaying = false;
    }
}

void CHotEdge::onActiveWindowChange(PHLWINDOW pWindow) {
    // Check each enabled edge
    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        if (!m_edges[i].enabled)
            continue;

        // Skip if in grace period (keyboard toggle)
        if (m_states[i].bJustShown) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - m_states[i].showTime).count();
            if (elapsed < 800)
                continue;
            m_states[i].bJustShown = false;
        }

        // Only auto-hide if overlay is visible and we're not in the edge zone
        if (!isOverlayVisible(i) || m_states[i].bCursorInZone)
            continue;

        // If no window (focus lost), hide
        if (!pWindow) {
            hideOverlay(i);
            m_states[i].activeMonitorName.clear();
            continue;
        }

        // Check if the active window is in our special workspace
        auto workspace = pWindow->m_workspace;
        if (!workspace)
            continue;

        // If the window is not in our special workspace, hide the overlay
        std::string specialName = "special:" + m_edges[i].specialWorkspace;
        if (workspace->m_name != specialName) {
            Log::logger->log(Log::DEBUG, "[HotEdge] Focus left special workspace {}, hiding overlay", EDGE_SLOT_NAMES[i]);
            hideOverlay(i);
            m_states[i].activeMonitorName.clear();
        }
    }
}

void CHotEdge::showOverlay(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return;

    if (isOverlayVisible(slotIndex))
        return;

    Log::logger->log(Log::DEBUG, "[HotEdge] Showing overlay: {} (slot {})",
        m_edges[slotIndex].specialWorkspace, EDGE_SLOT_NAMES[slotIndex]);

    // Set grace period to prevent immediate auto-hide (for keyboard toggles)
    m_states[slotIndex].bJustShown = true;
    m_states[slotIndex].showTime = steady_clock::now();

    // Use the dispatcher to toggle the special workspace
    auto it = g_pKeybindManager->m_dispatchers.find("togglespecialworkspace");
    if (it != g_pKeybindManager->m_dispatchers.end()) {
        it->second(m_edges[slotIndex].specialWorkspace);
    } else {
        Log::logger->log(Log::ERR, "[HotEdge] togglespecialworkspace dispatcher not found");
    }
}

void CHotEdge::hideOverlay(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
        return;

    if (!isOverlayVisible(slotIndex))
        return;

    Log::logger->log(Log::DEBUG, "[HotEdge] Hiding overlay: {} (slot {})",
        m_edges[slotIndex].specialWorkspace, EDGE_SLOT_NAMES[slotIndex]);

    // Toggle it off
    auto it = g_pKeybindManager->m_dispatchers.find("togglespecialworkspace");
    if (it != g_pKeybindManager->m_dispatchers.end())
        it->second(m_edges[slotIndex].specialWorkspace);
}

int CHotEdge::parseEdgeArg(const std::string& arg) {
    std::string lower = arg;
    for (auto& c : lower) c = std::tolower(c);

    // Trim whitespace
    size_t start = lower.find_first_not_of(" \t");
    size_t end = lower.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos)
        lower = lower.substr(start, end - start + 1);

    // Check for slot names (edge1, edge2, etc.)
    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        if (lower == EDGE_SLOT_NAMES[i])
            return i;
    }

    // Check for numeric index
    if (lower.size() == 1 && lower[0] >= '1' && lower[0] <= '8')
        return lower[0] - '1';

    // Check for side names - find slot matching side AND current monitor
    EdgeSide targetSide = EdgeSide::RIGHT;
    bool isSideName = false;

    if (lower == "left" || lower == "l") {
        targetSide = EdgeSide::LEFT;
        isSideName = true;
    } else if (lower == "right" || lower == "r") {
        targetSide = EdgeSide::RIGHT;
        isSideName = true;
    } else if (lower == "top" || lower == "t") {
        targetSide = EdgeSide::TOP;
        isSideName = true;
    } else if (lower == "bottom" || lower == "b") {
        targetSide = EdgeSide::BOTTOM;
        isSideName = true;
    }

    if (isSideName && g_pHotEdge) {
        // Find the slot that matches this side AND the current monitor
        auto cursorMonitor = g_pCompositor->getMonitorFromCursor();
        if (cursorMonitor) {
            for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
                if (g_pHotEdge->m_edges[i].enabled &&
                    g_pHotEdge->m_edges[i].side == targetSide &&
                    (g_pHotEdge->m_edges[i].targetMonitor == "*" ||
                     g_pHotEdge->m_edges[i].targetMonitor == cursorMonitor->m_name)) {
                    return i;
                }
            }
        }
    }

    return -1;  // Invalid or empty - means "all enabled edges"
}

// Static dispatcher functions
SDispatchResult CHotEdge::dispatchToggle(std::string args) {
    if (!g_pHotEdge)
        return {.success = false, .error = "HotEdge not initialized"};

    int slotIndex = parseEdgeArg(args);

    if (slotIndex >= 0) {
        // Specific slot
        if (g_pHotEdge->isOverlayVisible(slotIndex))
            g_pHotEdge->hideOverlay(slotIndex);
        else
            g_pHotEdge->showOverlay(slotIndex);
    } else {
        // Toggle first enabled slot that's visible, or show first enabled slot
        bool didSomething = false;
        for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
            if (g_pHotEdge->m_edges[i].enabled) {
                if (g_pHotEdge->isOverlayVisible(i)) {
                    g_pHotEdge->hideOverlay(i);
                    didSomething = true;
                    break;
                }
            }
        }
        if (!didSomething) {
            // Show first enabled slot
            for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
                if (g_pHotEdge->m_edges[i].enabled) {
                    g_pHotEdge->showOverlay(i);
                    break;
                }
            }
        }
    }
    return {};
}

SDispatchResult CHotEdge::dispatchShow(std::string args) {
    if (!g_pHotEdge)
        return {.success = false, .error = "HotEdge not initialized"};

    int slotIndex = parseEdgeArg(args);

    if (slotIndex >= 0) {
        g_pHotEdge->showOverlay(slotIndex);
    } else {
        // Show all enabled slots
        for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
            if (g_pHotEdge->m_edges[i].enabled)
                g_pHotEdge->showOverlay(i);
        }
    }
    return {};
}

SDispatchResult CHotEdge::dispatchHide(std::string args) {
    if (!g_pHotEdge)
        return {.success = false, .error = "HotEdge not initialized"};

    int slotIndex = parseEdgeArg(args);

    if (slotIndex >= 0) {
        g_pHotEdge->hideOverlay(slotIndex);
    } else {
        // Hide all
        for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
            if (g_pHotEdge->m_edges[i].enabled)
                g_pHotEdge->hideOverlay(i);
        }
    }
    return {};
}
