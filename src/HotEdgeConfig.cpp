#include "HotEdge.hpp"
#include "LegacyConfig.hpp"
#include "globals.hpp"

#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/config/lua/types/LuaConfigBool.hpp>
#include <hyprland/src/config/lua/types/LuaConfigInt.hpp>
#include <hyprland/src/config/lua/types/LuaConfigString.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include <algorithm>
#include <utility>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

int parseIntField(lua_State* state, int tableIndex, const char* key, int defaultValue,
                  int minimum, int maximum, int& output) {
    Hyprutils::Utils::CScopeGuard guard([state] { lua_pop(state, 1); });
    lua_getfield(state, tableIndex, key);
    if (lua_isnil(state, -1))
        return 0;

    Config::Lua::CLuaConfigInt parser(defaultValue, minimum, maximum);
    const auto error = parser.parse(state);
    if (error.errorCode != Config::Lua::PARSE_ERROR_OK)
        return Config::Lua::Bindings::Internal::configError(
            state, "add_edge: {}: {}", key, error.message);

    output = parser.parsed();
    return 0;
}

int parseStringField(lua_State* state, int tableIndex, const char* key,
                     const std::string& defaultValue, std::string& output) {
    Hyprutils::Utils::CScopeGuard guard([state] { lua_pop(state, 1); });
    lua_getfield(state, tableIndex, key);
    if (lua_isnil(state, -1))
        return 0;

    Config::Lua::CLuaConfigString parser(defaultValue);
    const auto error = parser.parse(state);
    if (error.errorCode != Config::Lua::PARSE_ERROR_OK)
        return Config::Lua::Bindings::Internal::configError(
            state, "add_edge: {}: {}", key, error.message);

    output = parser.parsed();
    return 0;
}

int parseBoolField(lua_State* state, int tableIndex, const char* key,
                   bool defaultValue, bool& output) {
    Hyprutils::Utils::CScopeGuard guard([state] { lua_pop(state, 1); });
    lua_getfield(state, tableIndex, key);
    if (lua_isnil(state, -1))
        return 0;

    Config::Lua::CLuaConfigBool parser(defaultValue);
    const auto error = parser.parse(state);
    if (error.errorCode != Config::Lua::PARSE_ERROR_OK)
        return Config::Lua::Bindings::Internal::configError(
            state, "add_edge: {}: {}", key, error.message);

    output = parser.parsed();
    return 0;
}

} // namespace

int CHotEdge::luaAddEdge(lua_State* state) {
    if (!g_pHotEdge)
        return Config::Lua::Bindings::Internal::configError(
            state, "add_edge: plugin not initialized");

    if (!lua_istable(state, 1))
        return Config::Lua::Bindings::Internal::configError(
            state,
            "add_edge: expected a table { side, special_workspace, trigger_width, "
            "dwell_time, target_monitor, hide_on_leave, enabled }");

    if (g_pHotEdge->m_luaEdges.size() >= MAX_EDGE_SLOTS)
        return Config::Lua::Bindings::Internal::configError(
            state, "add_edge: maximum of {} edges reached", MAX_EDGE_SLOTS);

    const int tableIndex = lua_absindex(state, 1);
    EdgeConfig config;
    config.enabled = true;
    config.targetMonitor = "*";

    std::string side;
    {
        Hyprutils::Utils::CScopeGuard guard([state] { lua_pop(state, 1); });
        lua_getfield(state, tableIndex, "side");
        if (lua_isnil(state, -1) || !lua_isstring(state, -1))
            return Config::Lua::Bindings::Internal::configError(
                state, "add_edge: side must be a string (left, right, top, bottom, "
                       "topleft, topright, bottomleft, bottomright)");
        side = lua_tostring(state, -1);
    }
    config.side = parseEdgeSide(side);

    {
        Hyprutils::Utils::CScopeGuard guard([state] { lua_pop(state, 1); });
        lua_getfield(state, tableIndex, "special_workspace");
        if (lua_isnil(state, -1) || !lua_isstring(state, -1))
            return Config::Lua::Bindings::Internal::configError(
                state, "add_edge: special_workspace is required and must be a string");
        config.specialWorkspace = lua_tostring(state, -1);
    }

    if (int result = parseIntField(state, tableIndex, "trigger_width", 15, 0, 512,
                                   config.triggerWidth);
        result)
        return result;
    if (int result = parseIntField(state, tableIndex, "dwell_time", 150, 0, 60000,
                                   config.dwellTime);
        result)
        return result;
    if (int result = parseStringField(state, tableIndex, "target_monitor", "*",
                                      config.targetMonitor);
        result)
        return result;
    if (int result = parseBoolField(state, tableIndex, "hide_on_leave", true,
                                    config.hideOnLeave);
        result)
        return result;
    if (int result = parseBoolField(state, tableIndex, "enabled", true, config.enabled);
        result)
        return result;

    g_pHotEdge->m_luaEdges.push_back(normalizeEdgeConfig(std::move(config)));
    return 0;
}

int CHotEdge::luaSetCornerMargin(lua_State* state) {
    if (!g_pHotEdge)
        return Config::Lua::Bindings::Internal::configError(
            state, "set_corner_margin: plugin not initialized");

    Config::Lua::CLuaConfigInt parser(DEFAULT_CORNER_MARGIN, 0, 10000);
    const auto error = parser.parse(state);
    if (error.errorCode != Config::Lua::PARSE_ERROR_OK)
        return Config::Lua::Bindings::Internal::configError(
            state, "set_corner_margin: {}", error.message);

    g_pHotEdge->m_cornerMarginSetting = parser.parsed();
    return 0;
}

void CHotEdge::registerLegacyConfig() {
    if (!m_legacyConfig)
        m_legacyConfig = std::make_unique<LegacyConfig>();
    m_legacyConfig->registerValues(PHANDLE);
}

void CHotEdge::reloadConfig() {
    // Runtime configuration always starts empty, then the current Lua API owns
    // the leading slots. The compatibility adapter may fill only the slots Lua
    // did not claim.
    m_edges.fill(EdgeConfig{});
    std::copy(m_luaEdges.begin(), m_luaEdges.end(), m_edges.begin());

    if (m_legacyConfig) {
        for (int i = static_cast<int>(m_luaEdges.size()); i < MAX_EDGE_SLOTS; ++i) {
            if (auto legacyEdge = m_legacyConfig->edge(i))
                m_edges[i] = std::move(*legacyEdge);
        }
    }

    if (m_cornerMarginSetting)
        m_cornerMargin = std::max(0, *m_cornerMarginSetting);
    else if (m_legacyConfig)
        m_cornerMargin = m_legacyConfig->cornerMargin().value_or(DEFAULT_CORNER_MARGIN);
    else
        m_cornerMargin = DEFAULT_CORNER_MARGIN;

    for (int i = 0; i < MAX_EDGE_SLOTS; ++i) {
        if (!m_edges[i].enabled)
            continue;

        Log::logger->log(
            Log::DEBUG,
            "[HotEdge] Slot {} enabled: side={}, trigger={}px, dwell={}ms, workspace={}, monitor={}",
            EDGE_SLOT_NAMES[i], SIDE_NAMES[static_cast<int>(m_edges[i].side)],
            m_edges[i].triggerWidth, m_edges[i].dwellTime,
            m_edges[i].specialWorkspace, m_edges[i].targetMonitor);
    }
}
