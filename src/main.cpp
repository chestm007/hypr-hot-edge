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

    // Register configuration values for each edge slot (edge1-edge8)
    for (int i = 0; i < MAX_EDGE_SLOTS; i++) {
        std::string prefix = std::string("plugin:hot-edge:") + EDGE_SLOT_NAMES[i] + ":";

        HyprlandAPI::addConfigValue(PHANDLE, (prefix + "enabled").c_str(), Hyprlang::INT{0});
        HyprlandAPI::addConfigValue(PHANDLE, (prefix + "side").c_str(), Hyprlang::STRING{"right"});
        HyprlandAPI::addConfigValue(PHANDLE, (prefix + "trigger_width").c_str(), Hyprlang::INT{15});
        HyprlandAPI::addConfigValue(PHANDLE, (prefix + "dwell_time").c_str(), Hyprlang::INT{150});
        HyprlandAPI::addConfigValue(PHANDLE, (prefix + "special_workspace").c_str(), Hyprlang::STRING{""});
        HyprlandAPI::addConfigValue(PHANDLE, (prefix + "target_monitor").c_str(), Hyprlang::STRING{"*"});
    }

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
    g_pHotEdge->init();

    // Register event listeners as members on g_pHotEdge so they unregister
    // automatically in PLUGIN_EXIT when the instance is destroyed. This replaces
    // the previous static-local pattern, which prevented clean plugin reload.
    //
    // Note: previously listened on render.pre as well, but calling dispatchers
    // (togglespecialworkspace) from inside render preparation races with
    // screencopy frame teardown and crashes Hyprland in surface scale walks.
    // mouse.move alone is enough for edge detection.
    g_pHotEdge->registerCallbacks();

    Log::logger->log(Log::INFO, "[HotEdge] Registered callbacks");
    Log::logger->log(Log::INFO, "[HotEdge] Plugin loaded successfully!");

    return {"hypr-hot-edge", "Hot edge trigger for special workspace overlays (supports multiple edges per monitor)", "claychinasky", "0.4.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Log::logger->log(Log::INFO, "[HotEdge] Plugin unloading...");
    g_pHotEdge.reset();
}
