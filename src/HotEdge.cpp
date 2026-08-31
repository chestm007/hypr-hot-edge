#include "HotEdge.hpp"
#include "globals.hpp"

#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/config/lua/types/LuaConfigBool.hpp>
#include <hyprland/src/config/lua/types/LuaConfigInt.hpp>
#include <hyprland/src/config/lua/types/LuaConfigString.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include <algorithm>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

using namespace std::chrono;

// Hyprland 0.56 removed CCompositor::getMonitorFromCursor() and
// CCompositor::m_monitors; monitor lookup now goes through the
// State::monitorState() query builder.
static PHLMONITOR monitorAtCursor() {
  return State::monitorState()
      ->query()
      .vec(g_pInputManager->getMouseCoordsInternal())
      .run();
}

CHotEdge::CHotEdge() {
  Log::logger->log(Log::DEBUG, "[HotEdge] Initializing...");
}

CHotEdge::~CHotEdge() {
  Log::logger->log(Log::DEBUG, "[HotEdge] Destroying...");

  // Drop the timer explicitly rather than relying on the SP going out of
  // scope — the event loop must not hold a callback into a plugin that is
  // about to be dlclose'd.
  if (m_pendingTimer && g_pEventLoopManager) {
    m_pendingTimer->cancel();
    g_pEventLoopManager->removeTimer(m_pendingTimer);
    m_pendingTimer.reset();
  }
}

void CHotEdge::registerCallbacks() {
  // Listeners stored as members; they unregister automatically when this
  // CHotEdge instance is destroyed in PLUGIN_EXIT, allowing clean plugin
  // reload.
  m_mouseMoveCb = Event::bus()->m_events.input.mouse.move.listen(
      [](const Vector2D &, Event::SCallbackInfo &) {
        if (g_pHotEdge)
          g_pHotEdge->onTick();
      });

  m_activeWindowCb = Event::bus()->m_events.window.active.listen(
      [](const PHLWINDOW &pWindow, Desktop::eFocusReason) {
        if (g_pHotEdge)
          g_pHotEdge->onActiveWindowChange(pWindow);
      });

  m_configPreReloadCb = Event::bus()->m_events.config.preReload.listen([]() {
    if (g_pHotEdge) {
      // The Lua config is about to re-execute from scratch. Drop the
      // definitions collected by the previous pass so the reloaded
      // event below rebuilds from a clean list instead of appending
      // to it.
      g_pHotEdge->m_definitions.clear();
      g_pHotEdge->m_cornerMarginSetting.reset();
    }
  });

  m_configReloadedCb = Event::bus()->m_events.config.reloaded.listen([]() {
    if (g_pHotEdge)
      g_pHotEdge->reloadConfig();
  });

  m_monitorRemovedCb = Event::bus()->m_events.monitor.preRemoved.listen(
      [](const PHLMONITOR &pMonitor) {
        if (g_pHotEdge)
          g_pHotEdge->onMonitorRemoved(pMonitor);
      });

  // Created disarmed; onTick arms it only while a dwell or hide delay is
  // outstanding, so a still cursor still gets its timers to fire.
  m_pendingTimer = makeShared<CEventLoopTimer>(
      std::nullopt,
      [](SP<CEventLoopTimer>, void *) {
        if (g_pHotEdge)
          g_pHotEdge->onTick();
      },
      nullptr);
  g_pEventLoopManager->addTimer(m_pendingTimer);
}

bool CHotEdge::hasPendingState() const {
  for (const auto &s : m_states) {
    // bJustShown counts: processEdge returns early for the whole 800ms
    // grace period, so without it the timer disarms, and a cursor that
    // stopped moving inside that window never gets the tick that would
    // start the auto-hide. The panel then stayed open indefinitely.
    if (s.bDwelling || s.bHideDelaying || s.bJustShown)
      return true;
  }
  return false;
}

// Must run on EVERY exit path out of onTick, including the throttled one: the
// timer that just fired is disarmed by definition, so an early return without
// re-arming would strand a pending dwell exactly as the original bug did.
void CHotEdge::armPendingTimer() {
  if (!m_pendingTimer || !g_pEventLoopManager)
    return;

  // Arm from an idle callback rather than inline: CEventLoopTimer::call()
  // resets the expiry around dispatching, so re-arming from inside the timer's
  // own callback is racy and intermittently left the timer dead — which showed
  // up as the panel appearing on some approaches but not others.
  // Captures an SP copy, never `this`, so a late fire cannot touch a destroyed
  // CHotEdge.
  const bool pending = hasPendingState();
  auto timer = m_pendingTimer;
  g_pEventLoopManager->doLater([timer, pending]() {
    if (pending)
      timer->updateTimeout(std::chrono::milliseconds(16));
    else
      timer->updateTimeout(std::nullopt);
  });
}

EdgeSide CHotEdge::parseSide(const std::string &str) {
  std::string lower = str;
  for (auto &c : lower)
    c = std::tolower(c);

  if (lower == "left" || lower == "l" || lower == "0")
    return EdgeSide::LEFT;
  if (lower == "right" || lower == "r" || lower == "1")
    return EdgeSide::RIGHT;
  if (lower == "top" || lower == "t" || lower == "2")
    return EdgeSide::TOP;
  if (lower == "bottom" || lower == "b" || lower == "3")
    return EdgeSide::BOTTOM;
  if (lower == "topleft" || lower == "top-left" || lower == "tl" ||
      lower == "4")
    return EdgeSide::TOP_LEFT;
  if (lower == "topright" || lower == "top-right" || lower == "tr" ||
      lower == "5")
    return EdgeSide::TOP_RIGHT;
  if (lower == "bottomleft" || lower == "bottom-left" || lower == "bl" ||
      lower == "6")
    return EdgeSide::BOTTOM_LEFT;
  if (lower == "bottomright" || lower == "bottom-right" || lower == "br" ||
      lower == "7")
    return EdgeSide::BOTTOM_RIGHT;

  return EdgeSide::RIGHT; // Default
}

// The corners sitting at each end of an edge, low end first (top for vertical
// edges, left for horizontal ones).
static std::array<EdgeSide, 2> edgeCorners(EdgeSide s) {
  switch (s) {
  case EdgeSide::LEFT:
    return {EdgeSide::TOP_LEFT, EdgeSide::BOTTOM_LEFT};
  case EdgeSide::RIGHT:
    return {EdgeSide::TOP_RIGHT, EdgeSide::BOTTOM_RIGHT};
  case EdgeSide::TOP:
    return {EdgeSide::TOP_LEFT, EdgeSide::TOP_RIGHT};
  case EdgeSide::BOTTOM:
    return {EdgeSide::BOTTOM_LEFT, EdgeSide::BOTTOM_RIGHT};
  default:
    return {s, s};
  }
}

// ---------------------------------------------------------------------------
// Lua config API: hl.plugin.hyprhotedge.*
//
// These run in the Lua config context (Hyprland executes hyprland.lua on the
// main thread). On every reload the config re-runs from scratch and
// preReload has cleared m_definitions first, so add_edge() appends to a clean
// list and the reloaded event rebuilds m_edges from it.
// ---------------------------------------------------------------------------

// Read and parse a single table field, hyprbars-style. Each field gets its own
// scope: lua_getfield pushes exactly one slot (the value, or nil when the key
// is absent), and the CScopeGuard pops exactly that one slot at block end.
// parse() reads the top of the stack without popping it, so the guard keeps
// the stack balanced whether or not the key was present. On a parse error the
// function early-returns; the guard still pops first, and the error
// propagates out to Hyprland as a config error.
//   present -> out is set to the parsed value
//   absent  -> out keeps its default
static int parseIntField(lua_State *L, int tableIdx, const char *key, int def,
                         int min, int max, int &out) {
  Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
  lua_getfield(L, tableIdx, key);
  if (!lua_isnil(L, -1)) {
    Config::Lua::CLuaConfigInt parser(def, min, max);
    const auto err = parser.parse(L);
    if (err.errorCode != Config::Lua::PARSE_ERROR_OK)
      return Config::Lua::Bindings::Internal::configError(L, "add_edge: {}: {}",
                                                          key, err.message);
    out = parser.parsed();
  }
  return 0;
}

static int parseStrField(lua_State *L, int tableIdx, const char *key,
                         const std::string &def, std::string &out) {
  Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
  lua_getfield(L, tableIdx, key);
  if (!lua_isnil(L, -1)) {
    Config::Lua::CLuaConfigString parser(def);
    const auto err = parser.parse(L);
    if (err.errorCode != Config::Lua::PARSE_ERROR_OK)
      return Config::Lua::Bindings::Internal::configError(L, "add_edge: {}: {}",
                                                          key, err.message);
    out = parser.parsed();
  }
  return 0;
}

static int parseBoolField(lua_State *L, int tableIdx, const char *key,
                          bool def, bool &out) {
  Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
  lua_getfield(L, tableIdx, key);
  if (!lua_isnil(L, -1)) {
    Config::Lua::CLuaConfigBool parser(def);
    const auto err = parser.parse(L);
    if (err.errorCode != Config::Lua::PARSE_ERROR_OK)
      return Config::Lua::Bindings::Internal::configError(L, "add_edge: {}: {}",
                                                          key, err.message);
    out = parser.parsed();
  }
  return 0;
}

int CHotEdge::luaAddEdge(lua_State *L) {
  if (!g_pHotEdge)
    return Config::Lua::Bindings::Internal::configError(
        L, "add_edge: plugin not initialized");

  if (!lua_istable(L, 1))
    return Config::Lua::Bindings::Internal::configError(
        L,
        "add_edge: expected a table { side, special_workspace, trigger_width, "
        "dwell_time, target_monitor, hide_on_leave, enabled }");

  const int tableIdx = lua_absindex(L, 1);

  if (g_pHotEdge->m_definitions.size() >= MAX_EDGE_SLOTS)
    return Config::Lua::Bindings::Internal::configError(
        L, "add_edge: maximum of {} edges reached", MAX_EDGE_SLOTS);

  EdgeDefinition def;

  {
    Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
    lua_getfield(L, tableIdx, "side");
    if (lua_isnil(L, -1) || !lua_isstring(L, -1))
      return Config::Lua::Bindings::Internal::configError(
          L, "add_edge: side must be a string (left, right, top, bottom, "
             "topleft, topright, bottomleft, bottomright)");
    def.side = lua_tostring(L, -1);
  }

  {
    Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
    lua_getfield(L, tableIdx, "special_workspace");
    if (lua_isnil(L, -1) || !lua_isstring(L, -1))
      return Config::Lua::Bindings::Internal::configError(
          L, "add_edge: special_workspace is required and must be a string");
    def.specialWorkspace = lua_tostring(L, -1);
  }

  if (int rc = parseIntField(L, tableIdx, "trigger_width", 15, 0, 512,
                             def.triggerWidth);
      rc)
    return rc;
  if (int rc = parseIntField(L, tableIdx, "dwell_time", 150, 0, 60000,
                             def.dwellTime);
      rc)
    return rc;
  if (int rc = parseStrField(L, tableIdx, "target_monitor", "*",
                             def.targetMonitor);
      rc)
    return rc;
  if (int rc = parseBoolField(L, tableIdx, "hide_on_leave", true,
                              def.hideOnLeave);
      rc)
    return rc;
  if (int rc = parseBoolField(L, tableIdx, "enabled", true, def.enabled); rc)
    return rc;

  if (def.targetMonitor.empty())
    def.targetMonitor = "*";

  g_pHotEdge->m_definitions.push_back(std::move(def));
  return 0;
}

int CHotEdge::luaSetCornerMargin(lua_State *L) {
  if (!g_pHotEdge)
    return Config::Lua::Bindings::Internal::configError(
        L, "set_corner_margin: plugin not initialized");

  auto p = std::make_unique<Config::Lua::CLuaConfigInt>(DEFAULT_CORNER_MARGIN,
                                                        0, 10000);
  auto e = p->parse(L);
  if (e.errorCode != Config::Lua::PARSE_ERROR_OK)
    return Config::Lua::Bindings::Internal::configError(
        L, "set_corner_margin: {}", e.message);

  g_pHotEdge->m_cornerMarginSetting = p->parsed();
  return 0;
}

void CHotEdge::reloadConfig() {
  // Negative would push edges back over the corner they are meant to avoid.
  m_cornerMargin =
      std::max(0, m_cornerMarginSetting.value_or(DEFAULT_CORNER_MARGIN));

  // Rebuild the fixed slots from the definitions collected by the Lua
  // config. Definitions past the slot limit are already rejected at parse
  // time, but guard here as well in case the limit ever moves.
  for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
    if (i >= static_cast<int>(m_definitions.size())) {
      m_edges[i].enabled = false;
      continue;
    }

    const auto &d = m_definitions[i];
    m_edges[i].enabled = d.enabled;
    m_edges[i].side = parseSide(d.side);
    m_edges[i].triggerWidth = std::max(0, d.triggerWidth);
    m_edges[i].dwellTime = std::max(0, d.dwellTime);
    m_edges[i].specialWorkspace = d.specialWorkspace;
    m_edges[i].targetMonitor = d.targetMonitor.empty() ? "*" : d.targetMonitor;
    m_edges[i].hideOnLeave = d.hideOnLeave;
  }
}

PHLMONITOR CHotEdge::getCurrentMonitor() { return monitorAtCursor(); }

PHLMONITOR CHotEdge::getMonitorByName(const std::string &name) {
  return State::monitorState()->query().name(name).run();
}

bool CHotEdge::isEdgeEnabledForMonitor(int slotIndex, PHLMONITOR monitor) {
  if (!monitor || slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
    return false;

  const auto &config = m_edges[slotIndex];
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

  auto monitor = monitorAtCursor();
  if (!monitor || !monitor->m_activeSpecialWorkspace)
    return false;

  std::string specialName = "special:" + m_edges[slotIndex].specialWorkspace;
  return monitor->m_activeSpecialWorkspace->m_name == specialName;
}

bool CHotEdge::isOverlayVisibleOnMonitor(int slotIndex,
                                         PHLMONITOR monitor) const {
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

CBox CHotEdge::getEdgeZone(PHLMONITOR monitor, EdgeSide side,
                           int triggerWidth) {
  if (!monitor)
    return CBox{0, 0, 0, 0};

  const auto &pos = monitor->m_position;
  const auto &size = monitor->m_size;

  // Edges stop short of any corner configured on this monitor, so approaching
  // the corner never clips the edge on the way in.
  const auto [lo, hi] = cornerInsets(monitor, side);
  const double spanX = std::max(0.0, size.x - lo - hi);
  const double spanY = std::max(0.0, size.y - lo - hi);

  switch (side) {
  case EdgeSide::LEFT:
    return CBox{pos.x, pos.y + lo, (double)triggerWidth, spanY};
  case EdgeSide::RIGHT:
    return CBox{pos.x + size.x - triggerWidth, pos.y + lo, (double)triggerWidth,
                spanY};
  case EdgeSide::TOP:
    return CBox{pos.x + lo, pos.y, spanX, (double)triggerWidth};
  case EdgeSide::BOTTOM:
    return CBox{pos.x + lo, pos.y + size.y - triggerWidth, spanX,
                (double)triggerWidth};
  // Corners: a triggerWidth square in the corner, so they stay tunable with
  // the same knob as the edges.
  case EdgeSide::TOP_LEFT:
    return CBox{pos.x, pos.y, (double)triggerWidth, (double)triggerWidth};
  case EdgeSide::TOP_RIGHT:
    return CBox{pos.x + size.x - triggerWidth, pos.y, (double)triggerWidth,
                (double)triggerWidth};
  case EdgeSide::BOTTOM_LEFT:
    return CBox{pos.x, pos.y + size.y - triggerWidth, (double)triggerWidth,
                (double)triggerWidth};
  case EdgeSide::BOTTOM_RIGHT:
    return CBox{pos.x + size.x - triggerWidth, pos.y + size.y - triggerWidth,
                (double)triggerWidth, (double)triggerWidth};
  }
  return CBox{0, 0, 0, 0};
}

std::pair<double, double> CHotEdge::cornerInsets(PHLMONITOR monitor,
                                                 EdgeSide side) {
  if (!monitor || isCornerSide(side))
    return {0.0, 0.0};

  const auto [loCorner, hiCorner] = edgeCorners(side);
  double lo = 0.0, hi = 0.0;

  for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
    if (!m_edges[i].enabled || !isCornerSide(m_edges[i].side))
      continue;
    if (!isEdgeEnabledForMonitor(i, monitor))
      continue;

    const double claim = m_edges[i].triggerWidth + m_cornerMargin;
    if (m_edges[i].side == loCorner)
      lo = std::max(lo, claim);
    if (m_edges[i].side == hiCorner)
      hi = std::max(hi, claim);
  }
  return {lo, hi};
}

CBox CHotEdge::getPanelArea(PHLMONITOR monitor, EdgeSide side) {
  if (!monitor)
    return CBox{0, 0, 0, 0};

  const auto &pos = monitor->m_position;
  const auto &size = monitor->m_size;

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
  // Corners get the quadrant, i.e. the intersection of their two edge strips.
  case EdgeSide::TOP_LEFT:
    return CBox{pos.x, pos.y, panelWidth, panelHeight};
  case EdgeSide::TOP_RIGHT:
    return CBox{pos.x + size.x - panelWidth, pos.y, panelWidth, panelHeight};
  case EdgeSide::BOTTOM_LEFT:
    return CBox{pos.x, pos.y + size.y - panelHeight, panelWidth, panelHeight};
  case EdgeSide::BOTTOM_RIGHT:
    return CBox{pos.x + size.x - panelWidth, pos.y + size.y - panelHeight,
                panelWidth, panelHeight};
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
  const auto zone = getEdgeZone(monitor, m_edges[slotIndex].side,
                                m_edges[slotIndex].triggerWidth);

  return zone.containsPoint(cursorPos);
}

bool CHotEdge::isCursorAtScreenEdge(int slotIndex, PHLMONITOR monitor) {
  if (!monitor || slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
    return false;

  const auto cursorPos = g_pInputManager->getMouseCoordsInternal();
  const auto &pos = monitor->m_position;
  const auto &size = monitor->m_size;

  // Check if cursor is at the absolute screen boundary (within 2-3 pixels)
  const bool atLeft = cursorPos.x <= pos.x + 2;
  const bool atRight = cursorPos.x >= pos.x + size.x - 3;
  const bool atTop = cursorPos.y <= pos.y + 2;
  const bool atBottom = cursorPos.y >= pos.y + size.y - 3;

  // The instant no-dwell trigger has to honour the corner dead zone too, or the
  // edge still wins the race to the corner along the last few pixels.
  const auto [lo, hi] = cornerInsets(monitor, m_edges[slotIndex].side);
  const bool inSpanX =
      cursorPos.x >= pos.x + lo && cursorPos.x <= pos.x + size.x - hi;
  const bool inSpanY =
      cursorPos.y >= pos.y + lo && cursorPos.y <= pos.y + size.y - hi;

  switch (m_edges[slotIndex].side) {
  case EdgeSide::LEFT:
    return atLeft && inSpanY;
  case EdgeSide::RIGHT:
    return atRight && inSpanY;
  case EdgeSide::TOP:
    return atTop && inSpanX;
  case EdgeSide::BOTTOM:
    return atBottom && inSpanX;
  // A corner needs both of its boundaries — this is the pixel the pointer
  // physically cannot overshoot, so it triggers without dwell.
  case EdgeSide::TOP_LEFT:
    return atTop && atLeft;
  case EdgeSide::TOP_RIGHT:
    return atTop && atRight;
  case EdgeSide::BOTTOM_LEFT:
    return atBottom && atLeft;
  case EdgeSide::BOTTOM_RIGHT:
    return atBottom && atRight;
  }
  return false;
}

void CHotEdge::onTick() {
  if (!m_active) {
    armPendingTimer();
    return;
  }

  // No throttle here. There used to be a 16ms one, but onTick is the only
  // place zone state is updated, so a dropped tick drops a *transition*: lose
  // the one that clears bCursorInZone on the way out and re-entering the zone
  // never starts a new dwell, so the panel silently never opens again. That
  // was intermittent by construction — it depended on when the last tick ran.
  // The work is a handful of box tests per enabled slot; cheap enough to run
  // on every motion event.

  // Corners need no special ordering here: getEdgeZone() already keeps each
  // edge clear of the corners configured on its monitor, so the zones are
  // disjoint and at most one can claim the cursor.
  for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
    if (m_edges[i].enabled)
      processEdge(i);
  }

  armPendingTimer();
}

void CHotEdge::processEdge(int slotIndex) {
  auto &config = m_edges[slotIndex];
  auto &state = m_states[slotIndex];

  auto cursorMonitor = getCurrentMonitor();
  if (!cursorMonitor)
    return;

  // Check if this edge is enabled for the current monitor
  bool enabledForThisMonitor =
      isEdgeEnabledForMonitor(slotIndex, cursorMonitor);

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
    Log::logger->log(Log::DEBUG,
                     "[HotEdge] Cursor left monitor {}, hiding {} overlay",
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
    auto elapsed =
        duration_cast<milliseconds>(steady_clock::now() - state.dwellStart)
            .count();
    if (elapsed >= config.dwellTime) {
      showOverlay(slotIndex);
      state.activeMonitorName = cursorMonitor->m_name;
      state.bDwelling = false;
    }
  }

  // Check grace period - skip auto-hide if overlay was just shown (keyboard
  // toggle)
  if (state.bJustShown) {
    auto elapsed =
        duration_cast<milliseconds>(steady_clock::now() - state.showTime)
            .count();
    if (elapsed >= 800) { // 800ms grace period
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
  // Use a small delay to allow animation and prevent accidental closes.
  // hide_on_leave = 0 opts out entirely: the panel then lives until focus
  // leaves its workspace, the cursor changes monitor, or a dispatcher hides
  // it. That is the only sane mode for a panel that is not actually the
  // 1/3-of-screen rectangle getPanelArea() assumes.
  if (config.hideOnLeave && visible && !inPanel && !inZone && !atEdge) {
    if (!state.bHideDelaying) {
      state.bHideDelaying = true;
      state.hideDelayStart = steady_clock::now();
    } else {
      auto elapsed = duration_cast<milliseconds>(steady_clock::now() -
                                                 state.hideDelayStart)
                         .count();
      if (elapsed >= 150) { // 150ms delay before hiding
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
  if (!m_active)
    return;

  // Check each enabled edge
  for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
    if (!m_edges[i].enabled)
      continue;

    // Skip if in grace period (keyboard toggle)
    if (m_states[i].bJustShown) {
      auto elapsed = duration_cast<milliseconds>(steady_clock::now() -
                                                 m_states[i].showTime)
                         .count();
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
      Log::logger->log(
          Log::DEBUG,
          "[HotEdge] Focus left special workspace {}, hiding overlay",
          EDGE_SLOT_NAMES[i]);
      hideOverlay(i);
      m_states[i].activeMonitorName.clear();
    }
  }
}

void CHotEdge::onMonitorRemoved(PHLMONITOR pMonitor) {
  if (!pMonitor)
    return;

  // A monitor is being torn down (unplug / output disable / DPMS off). Hide any
  // panel still open on it BEFORE Hyprland unmaps the live special-workspace
  // window — that unmap path races with screencopy frame destruction and
  // null-derefs in CWLSurfaceResource::sendPreferredScale (see showOverlay).
  // processEdge only hides on cursor-monitor-change, which doesn't fire here.
  for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
    if (m_states[i].activeMonitorName == pMonitor->m_name) {
      Log::logger->log(Log::DEBUG,
                       "[HotEdge] Monitor {} removed, hiding {} overlay",
                       pMonitor->m_name, EDGE_SLOT_NAMES[i]);
      hideOverlay(i);
      m_states[i].activeMonitorName.clear();
      m_states[i].bCursorInZone = false;
      m_states[i].bDwelling = false;
      m_states[i].bHideDelaying = false;
    }
  }
}

void CHotEdge::showOverlay(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
    return;

  if (isOverlayVisible(slotIndex))
    return;

  Log::logger->log(Log::DEBUG, "[HotEdge] Showing overlay: {} (slot {})",
                   m_edges[slotIndex].specialWorkspace,
                   EDGE_SLOT_NAMES[slotIndex]);

  // Set grace period to prevent immediate auto-hide (for keyboard toggles)
  m_states[slotIndex].bJustShown = true;
  m_states[slotIndex].showTime = steady_clock::now();

  // Defer to idle: calling togglespecialworkspace synchronously from signal
  // listeners (render.pre, window.active, mouse.move) can race with screencopy
  // frame teardown and crash in CWLSurfaceResource::sendPreferredScale.
  const auto ws = m_edges[slotIndex].specialWorkspace;
  g_pEventLoopManager->doLater([ws]() {
    auto it = g_pKeybindManager->m_dispatchers.find("togglespecialworkspace");
    if (it != g_pKeybindManager->m_dispatchers.end())
      it->second(ws);
    else
      Log::logger->log(Log::ERR,
                       "[HotEdge] togglespecialworkspace dispatcher not found");
  });
}

void CHotEdge::hideOverlay(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= MAX_EDGE_SLOTS)
    return;

  if (!isOverlayVisible(slotIndex))
    return;

  Log::logger->log(Log::DEBUG, "[HotEdge] Hiding overlay: {} (slot {})",
                   m_edges[slotIndex].specialWorkspace,
                   EDGE_SLOT_NAMES[slotIndex]);

  // Defer to idle (see showOverlay for rationale).
  const auto ws = m_edges[slotIndex].specialWorkspace;
  g_pEventLoopManager->doLater([ws]() {
    auto it = g_pKeybindManager->m_dispatchers.find("togglespecialworkspace");
    if (it != g_pKeybindManager->m_dispatchers.end())
      it->second(ws);
  });
}

int CHotEdge::parseEdgeArg(const std::string &arg) {
  std::string lower = arg;
  for (auto &c : lower)
    c = std::tolower(c);

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
  if (lower.find_first_not_of("0123456789") == std::string::npos &&
      !lower.empty()) {
    const int n = std::stoi(lower);
    if (n >= 1 && n <= MAX_EDGE_SLOTS)
      return n - 1;
  }

  // Check for side names - find slot matching side AND current monitor
  static const std::pair<const char *, EdgeSide> SIDE_ARGS[] = {
      {"left", EdgeSide::LEFT},
      {"l", EdgeSide::LEFT},
      {"right", EdgeSide::RIGHT},
      {"r", EdgeSide::RIGHT},
      {"top", EdgeSide::TOP},
      {"t", EdgeSide::TOP},
      {"bottom", EdgeSide::BOTTOM},
      {"b", EdgeSide::BOTTOM},
      {"topleft", EdgeSide::TOP_LEFT},
      {"top-left", EdgeSide::TOP_LEFT},
      {"tl", EdgeSide::TOP_LEFT},
      {"topright", EdgeSide::TOP_RIGHT},
      {"top-right", EdgeSide::TOP_RIGHT},
      {"tr", EdgeSide::TOP_RIGHT},
      {"bottomleft", EdgeSide::BOTTOM_LEFT},
      {"bottom-left", EdgeSide::BOTTOM_LEFT},
      {"bl", EdgeSide::BOTTOM_LEFT},
      {"bottomright", EdgeSide::BOTTOM_RIGHT},
      {"bottom-right", EdgeSide::BOTTOM_RIGHT},
      {"br", EdgeSide::BOTTOM_RIGHT},
  };

  EdgeSide targetSide = EdgeSide::RIGHT;
  bool isSideName = false;
  for (const auto &[name, side] : SIDE_ARGS) {
    if (lower == name) {
      targetSide = side;
      isSideName = true;
      break;
    }
  }

  if (isSideName && g_pHotEdge) {
    // Find the slot that matches this side AND the current monitor
    auto cursorMonitor = monitorAtCursor();
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

  return -1; // Invalid or empty - means "all enabled edges"
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

SDispatchResult CHotEdge::dispatchEnable(std::string) {
  if (!g_pHotEdge)
    return {.success = false, .error = "HotEdge not initialized"};
  g_pHotEdge->m_active = true;
  Log::logger->log(Log::INFO, "[HotEdge] Edge detection enabled");
  return {};
}

SDispatchResult CHotEdge::dispatchDisable(std::string) {
  if (!g_pHotEdge)
    return {.success = false, .error = "HotEdge not initialized"};
  g_pHotEdge->m_active = false;
  // Reset transient cursor-tracking state so re-enable starts clean.
  // Don't touch activeMonitorName or visible overlays — those remain
  // recoverable.
  for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
    g_pHotEdge->m_states[i].bCursorInZone = false;
    g_pHotEdge->m_states[i].bDwelling = false;
    g_pHotEdge->m_states[i].bHideDelaying = false;
  }
  Log::logger->log(Log::INFO, "[HotEdge] Edge detection disabled");
  return {};
}

SDispatchResult CHotEdge::dispatchToggleActive(std::string args) {
  if (!g_pHotEdge)
    return {.success = false, .error = "HotEdge not initialized"};
  return g_pHotEdge->m_active ? dispatchDisable(args) : dispatchEnable(args);
}
