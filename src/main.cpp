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

    // Create the HotEdge instance BEFORE registering callbacks
    g_pHotEdge = std::make_unique<CHotEdge>();
    g_pHotEdge->init();

    // Register event callbacks using lambda style (like official plugins)
    // Use mouseMove for responsive edge detection
    // mouseMove -> m_events.input.mouse.move (Cancellable<Vector2D>)
    static auto mouseMoveCb = Event::bus()->m_events.input.mouse.move.listen(
        [](const Vector2D&, Event::SCallbackInfo&) {
            if (g_pHotEdge)
                g_pHotEdge->onTick();
        }
    );

    // Note: previously listened on render.pre as well, but calling dispatchers
    // (togglespecialworkspace) from inside render preparation races with
    // screencopy frame teardown and crashes Hyprland in surface scale walks.
    // mouse.move alone is enough for edge detection.

    // activeWindow -> m_events.window.active (Event<PHLWINDOW, Desktop::eFocusReason>)
    static auto activeWindowCb = Event::bus()->m_events.window.active.listen(
        [](const PHLWINDOW& pWindow, Desktop::eFocusReason) {
            if (g_pHotEdge)
                g_pHotEdge->onActiveWindowChange(pWindow);
        }
    );

    // configReloaded -> m_events.config.reloaded (Event<>)
    static auto configReloadedCb = Event::bus()->m_events.config.reloaded.listen(
        []() {
            if (g_pHotEdge)
                g_pHotEdge->reloadConfig();
        }
    );

    Log::logger->log(Log::INFO, "[HotEdge] Registered callbacks");
    Log::logger->log(Log::INFO, "[HotEdge] Plugin loaded successfully!");

    return {"hypr-hot-edge", "Hot edge trigger for special workspace overlays (supports multiple edges per monitor)", "claychinasky", "0.3.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Log::logger->log(Log::INFO, "[HotEdge] Plugin unloading...");
    g_pHotEdge.reset();
}
