#include "globals.hpp"
#include "HotEdge.hpp"

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    Log::logger->log(Log::INFO, "[HotEdge] Plugin loading...");

    // Register dispatchers
    // Args can be slot name (edge1, edge2, etc.) or empty for default behavior
    HyprlandAPI::addDispatcherV2(PHANDLE, "hotedge:toggle", CHotEdge::dispatchToggle);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hotedge:show", CHotEdge::dispatchShow);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hotedge:hide", CHotEdge::dispatchHide);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hotedge:enable", CHotEdge::dispatchEnable);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hotedge:disable", CHotEdge::dispatchDisable);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hotedge:toggle-active", CHotEdge::dispatchToggleActive);

    // Create the HotEdge instance BEFORE registering callbacks
    g_pHotEdge = std::make_unique<CHotEdge>();

    // Register the legacy hyprlang config values (plugin:hot-edge:*) first, so
    // they exist before the queued reload at the end of this function fires
    // config.reloaded and reloadConfig() reads them. addConfigValueV2 is
    // accepted by both the hyprlang and the Lua config manager, so this is a
    // no-op-ish path under hyprland.lua and the real one under hyprland.conf.
    // The values are owned by the plugin (SPs in m_configValues) — never
    // looked up by name — which is what avoids the 0.56 name-lookup SIGSEGV.
    g_pHotEdge->registerConfigValues();

    // Configuration is collected from the Lua config via
    // hl.plugin.hyprhotedge.add_edge() / set_corner_margin(). These run while
    // Hyprland evaluates hyprland.lua, so the listeners must exist before the
    // first config pass reaches this plugin. Callbacks are removed from the
    // hl.plugin namespace automatically on plugin unload.
    if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyprhotedge", "add_edge", CHotEdge::luaAddEdge))
        Log::logger->log(Log::ERR, "[HotEdge] Failed to register add_edge Lua function");
    if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyprhotedge", "set_corner_margin", CHotEdge::luaSetCornerMargin))
        Log::logger->log(Log::ERR, "[HotEdge] Failed to register set_corner_margin Lua function");

    // Register event listeners as members on g_pHotEdge so they unregister
    // automatically in PLUGIN_EXIT when the instance is destroyed.
    //
    // Note: previously listened on render.pre as well, but calling dispatchers
    // (togglespecialworkspace) from inside render preparation races with
    // screencopy frame teardown and crashes Hyprland in surface scale walks.
    // mouse.move alone is enough for edge detection.
    g_pHotEdge->registerCallbacks();

    // Hyprland only loads plugins declared in the config from a reload it
    // queues itself (see CPluginSystem), which re-runs the Lua config, so the
    // first pass already sees hl.plugin.hyprhotedge.*. Queue a reload too, to
    // pick up definitions no matter which load path (hyprctl plugin load vs
    // config) this plugin came in through.
    HyprlandAPI::reloadConfig();

    Log::logger->log(Log::INFO, "[HotEdge] Registered callbacks");
    Log::logger->log(Log::INFO, "[HotEdge] Plugin loaded successfully!");

    return {"hypr-hot-edge", "Hot edge trigger for special workspace overlays (supports multiple edges per monitor)", "claychinasky", "0.6.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Log::logger->log(Log::INFO, "[HotEdge] Plugin unloading...");
    g_pHotEdge.reset();
}
