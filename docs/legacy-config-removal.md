# Removing legacy hyprlang configuration

The `plugin:hot-edge:*` configuration values are a temporary compatibility path. The supported long-term interface is the Lua API under `hl.plugin.hyprhotedge`.

Legacy code is deliberately isolated in `src/LegacyConfig.hpp` and `src/LegacyConfig.cpp`. Both frontends produce the canonical `EdgeConfig` model from `src/EdgeConfig.hpp`; runtime edge handling does not know which frontend supplied a slot.

## Removal prerequisites

Before deleting compatibility support:

1. Announce the release in which `plugin:hot-edge:*` will stop working.
2. Give users at least one release with the Lua examples in `README.md` marked as the recommended migration target.
3. Confirm the minimum supported Hyprland version provides `HyprlandAPI::addLuaFunction` and the Lua configuration manager APIs used by `src/HotEdgeConfig.cpp`.
4. Decide whether loading the plugin from `hyprland.conf` while configuring it from an included Lua source remains supported. Removing the legacy edge values does not itself require removing that plugin-loading mechanism.

## Code removal

Perform the following as one focused change:

1. Delete `src/LegacyConfig.hpp` and `src/LegacyConfig.cpp`.
2. Remove `src/LegacyConfig.cpp` from `CMakeLists.txt`.
3. Remove `#include "LegacyConfig.hpp"` from `src/HotEdge.cpp` and `src/HotEdgeConfig.cpp`.
4. In `src/HotEdge.hpp`:
   - remove the `LegacyConfig` forward declaration;
   - remove `registerLegacyConfig()`;
   - remove `m_legacyConfig`;
   - remove comments describing legacy precedence.
5. In `src/main.cpp`, remove the `registerLegacyConfig()` call and its compatibility comment.
6. In `CHotEdge::reloadConfig()` in `src/HotEdgeConfig.cpp`:
   - remove the loop that fills unclaimed slots from `m_legacyConfig`;
   - replace the corner-margin precedence block with `m_cornerMarginSetting.value_or(DEFAULT_CORNER_MARGIN)`, clamped to zero;
   - retain the initial `m_edges.fill(EdgeConfig{})` and copy from `m_luaEdges`.
7. Remove legacy examples, mixed-source precedence text, legacy defaults, and hyprlang troubleshooting from `README.md`. Keep unrelated `hyprland.conf` examples only if they are still valid for the supported Lua setup.
8. Update the plugin version and release notes as required by the release process.

Do not remove `src/EdgeConfig.hpp` or `src/HotEdgeConfig.cpp`. They are the canonical model and Lua configuration implementation, not compatibility scaffolding.

## Verification

Run:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
./check-symbols.sh build/hypr-hot-edge.so
git diff --check
```

Then confirm no compatibility symbols or keywords remain:

```bash
git grep -nE 'LegacyConfig|registerLegacyConfig|plugin:hot-edge|hyprlang|m_legacyConfig'
```

Review every remaining match. A clean removal should have no code matches; documentation matches should exist only in release history or migration notes intentionally retained for old releases.

Finally, load the uniquely named build through the normal development workflow (`make reload`) and verify:

- a Lua `add_edge` definition is applied after `hyprctl reload`;
- removing a Lua definition disables its old slot after reload;
- `set_corner_margin` still overrides the compiled default;
- more than 16 Lua definitions still produce a config error;
- plugin unload/reload remains clean.
