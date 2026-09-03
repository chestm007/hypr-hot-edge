#pragma once

#include "EdgeConfig.hpp"

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <array>
#include <optional>
#include <string>

// Compatibility adapter for plugin:hot-edge:* values in hyprland.conf.
// No runtime code depends on the representation below: callers receive only
// canonical EdgeConfig values. This class can therefore be deleted as a unit
// when legacy hyprlang support is removed.
class LegacyConfig {
public:
    void registerValues(HANDLE handle);

    std::optional<EdgeConfig> edge(int slot) const;
    std::optional<int> cornerMargin() const;

private:
    struct EdgeValues {
        SP<Config::Values::Int> enabled;
        SP<Config::Values::String> side;
        SP<Config::Values::Int> triggerWidth;
        SP<Config::Values::Int> dwellTime;
        SP<Config::Values::String> specialWorkspace;
        SP<Config::Values::String> targetMonitor;
        SP<Config::Values::Int> hideOnLeave;
    };

    static constexpr int FIELDS_PER_SLOT = 7;

    const char* configName(int slot, int field, const char* suffix);

    std::array<std::string, MAX_EDGE_SLOTS * FIELDS_PER_SLOT> m_configNames;
    std::array<EdgeValues, MAX_EDGE_SLOTS> m_values;
    SP<Config::Values::Int> m_cornerMargin;
    bool m_registered = false;
};
