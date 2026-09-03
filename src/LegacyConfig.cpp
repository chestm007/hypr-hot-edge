#include "LegacyConfig.hpp"

#include <hyprland/src/debug/log/Logger.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <utility>

namespace {

int configIntegerToInt(Config::INTEGER value,
                       int minimum = std::numeric_limits<int>::min(),
                       int maximum = std::numeric_limits<int>::max()) {
    return static_cast<int>(std::clamp(
        value, static_cast<Config::INTEGER>(minimum),
        static_cast<Config::INTEGER>(maximum)));
}

} // namespace

const char* LegacyConfig::configName(int slot, int field, const char* suffix) {
    auto& name = m_configNames[slot * FIELDS_PER_SLOT + field];
    name = std::string("plugin:hot-edge:") + EDGE_SLOT_NAMES[slot] + ":" + suffix;
    return name.c_str();
}

void LegacyConfig::registerValues(HANDLE handle) {
    if (m_registered)
        return;

    for (int i = 0; i < MAX_EDGE_SLOTS; ++i) {
        auto& values = m_values[i];

        values.enabled = makeShared<Config::Values::Int>(configName(i, 0, "enabled"), "Enable this hot edge", 0);
        values.side = makeShared<Config::Values::String>(configName(i, 1, "side"), "Screen edge: left, right, top or bottom", "right");
        values.triggerWidth = makeShared<Config::Values::Int>(configName(i, 2, "trigger_width"), "Width of the trigger zone, in px", 15);
        values.dwellTime = makeShared<Config::Values::Int>(configName(i, 3, "dwell_time"), "Time to dwell in the zone before triggering, in ms", 150);
        values.specialWorkspace = makeShared<Config::Values::String>(configName(i, 4, "special_workspace"), "Name of the special workspace to toggle", "");
        values.targetMonitor = makeShared<Config::Values::String>(configName(i, 5, "target_monitor"), "Monitor name, or * for all monitors", "*");
        values.hideOnLeave = makeShared<Config::Values::Int>(configName(i, 6, "hide_on_leave"), "Auto-hide when the cursor leaves the panel area", 1);

        for (const auto& value : std::initializer_list<SP<Config::Values::IValue>>{
                 values.enabled, values.side, values.triggerWidth, values.dwellTime,
                 values.specialWorkspace, values.targetMonitor, values.hideOnLeave}) {
            if (!HyprlandAPI::addConfigValueV2(handle, value))
                Log::logger->log(Log::ERR, "[HotEdge] Failed to register config value {}", value->name());
        }
    }

    // String literals already have the static lifetime required by IValue's
    // non-owning name pointer.
    m_cornerMargin = makeShared<Config::Values::Int>(
        "plugin:hot-edge:corner_margin",
        "Gap in px between a corner zone and the edges either side of it",
        DEFAULT_CORNER_MARGIN);
    if (!HyprlandAPI::addConfigValueV2(handle, m_cornerMargin))
        Log::logger->log(Log::ERR, "[HotEdge] Failed to register config value corner_margin");

    m_registered = true;
}

std::optional<EdgeConfig> LegacyConfig::edge(int slot) const {
    if (!m_registered || slot < 0 || slot >= MAX_EDGE_SLOTS)
        return std::nullopt;

    const auto& values = m_values[slot];
    if (!values.enabled || !values.side || !values.triggerWidth || !values.dwellTime ||
        !values.specialWorkspace || !values.targetMonitor || !values.hideOnLeave)
        return std::nullopt;

    EdgeConfig config;
    config.enabled = values.enabled->value() != 0;
    config.side = parseEdgeSide(values.side->value());
    config.triggerWidth = configIntegerToInt(values.triggerWidth->value());
    config.dwellTime = configIntegerToInt(values.dwellTime->value());
    config.specialWorkspace = values.specialWorkspace->value();
    config.targetMonitor = values.targetMonitor->value();
    config.hideOnLeave = values.hideOnLeave->value() != 0;
    return normalizeEdgeConfig(std::move(config));
}

std::optional<int> LegacyConfig::cornerMargin() const {
    if (!m_registered || !m_cornerMargin)
        return std::nullopt;
    return configIntegerToInt(m_cornerMargin->value(), 0, 10000);
}
