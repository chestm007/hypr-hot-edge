# hypr-hot-edge

A Hyprland plugin that triggers special workspace overlays when the mouse cursor reaches screen edges. Similar to hot corners, but for edges.

## Features

- **Slot-based configuration**: Up to 8 independent edge triggers (edge1-edge8)
- **Per-monitor support**: Assign edges to specific monitors or all monitors
- **Multi-monitor aware**: Each monitor can have its own edge panels with separate animations
- **Auto-hide**: Panels automatically close when cursor leaves the panel area
- **Dwell time**: Optional delay before triggering (prevents accidental activation)
- **Instant edge trigger**: Moving cursor to absolute screen edge triggers immediately
- **Keyboard toggle**: Hotkeys to show/hide panels with grace period to prevent immediate close
- **Smart dispatchers**: Toggle by side name (right/bottom) - automatically finds correct slot for current monitor

## Installation

### Building from source

```bash
# Clone the repository
git clone https://github.com/claychinasky/hypr-hot-edge.git
cd hypr-hot-edge

# Build
make

# The plugin will be at build/hypr-hot-edge.so
```

### Requirements

- Hyprland (built from source or with headers available)
- CMake
- C++23 compatible compiler

## Configuration

Add to your `hyprland.conf`:

```conf
# Load the plugin
plugin = /path/to/hypr-hot-edge.so

# Configure edge slots (edge1 through edge8)
plugin {
    hot-edge {
        # Each slot can be any edge on any monitor
        # side = left/right/top/bottom
        # target_monitor = "*" for all, or specific name like "DP-1"

        edge1 {
            enabled = 1
            side = right
            trigger_width = 15        # pixels from edge to trigger zone
            dwell_time = 150          # ms delay before triggering (0 = instant)
            special_workspace = hotedge-right-mon1
            target_monitor = HDMI-A-1
        }

        edge2 {
            enabled = 1
            side = right
            trigger_width = 15
            dwell_time = 150
            special_workspace = hotedge-right-mon2
            target_monitor = DP-3
        }

        edge3 {
            enabled = 1
            side = bottom
            trigger_width = 15
            dwell_time = 150
            special_workspace = hotedge-bottom-mon1
            target_monitor = HDMI-A-1
        }

        edge4 {
            enabled = 1
            side = bottom
            trigger_width = 15
            dwell_time = 150
            special_workspace = hotedge-bottom-mon2
            target_monitor = DP-3
        }

        # edge5 through edge8 available for more configurations
    }
}
```

### Workspace Rules

Configure how panels appear on screen using `gapsout` (format: top right bottom left):

```conf
# Right panels (~1/3 width from right edge, 1700px gap on left for 2560px wide monitor)
workspace = special:hotedge-right-mon1, gapsout:0 0 0 1700
workspace = special:hotedge-right-mon2, gapsout:0 0 0 1700

# Bottom panels (~1/3 height from bottom, 960px gap on top for 1440px tall monitor)
workspace = special:hotedge-bottom-mon1, gapsout:960 0 0 0
workspace = special:hotedge-bottom-mon2, gapsout:960 0 0 0

# Left panels (gap on right)
workspace = special:hotedge-left, gapsout:0 1700 0 0

# Top panels (gap on bottom)
workspace = special:hotedge-top, gapsout:0 0 960 0
```

### Animation

```conf
# Fade animation (works well for all edge directions)
animation = specialWorkspace, 1, 3, almostLinear, fade
animation = specialWorkspaceIn, 1, 2, almostLinear, fade
animation = specialWorkspaceOut, 1, 3, almostLinear, fade

# Or slide animation (horizontal only)
animation = specialWorkspace, 1, 4, easeOutQuint, slide

# Disable blur/dim for cleaner look
decoration {
    dim_special = 0.0
    blur {
        special = false
    }
}
```

### Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | int | 0 | Enable this edge slot (0 or 1) |
| `side` | string | "right" | Edge side: `left`, `right`, `top`, `bottom` |
| `trigger_width` | int | 15 | Pixel width of the trigger zone from edge |
| `dwell_time` | int | 150 | Milliseconds to wait before triggering |
| `special_workspace` | string | "" | Name of the special workspace to toggle |
| `target_monitor` | string | "*" | Monitor name or "*" for all monitors |

## Dispatchers

The plugin provides three dispatchers that can be used with keybindings:

```conf
# Toggle by side name (recommended - auto-selects slot for current monitor)
bind = SUPER CTRL, H, hotedge:toggle, right
bind = SUPER CTRL, B, hotedge:toggle, bottom
bind = SUPER CTRL, L, hotedge:toggle, left
bind = SUPER CTRL, T, hotedge:toggle, top

# Toggle specific slot
bind = SUPER CTRL, 1, hotedge:toggle, edge1
bind = SUPER CTRL, 2, hotedge:toggle, edge2

# Show/hide specific edges
bind = SUPER CTRL, S, hotedge:show, right
bind = SUPER CTRL, X, hotedge:hide, right
```

### Dispatcher Arguments

| Argument | Description |
|----------|-------------|
| `right`, `left`, `top`, `bottom` | Toggle edge by side for current monitor |
| `edge1` - `edge8` | Toggle specific slot |
| `1` - `8` | Toggle slot by number |
| *(empty)* | Toggle first visible or first enabled slot |

## Multi-Monitor Setup

For proper multi-monitor support, use **unique workspace names per monitor**. This prevents animation glitches where panels animate from the wrong position.

```conf
plugin {
    hot-edge {
        # Monitor 1 - right edge
        edge1 {
            enabled = 1
            side = right
            special_workspace = hotedge-right-hdmi
            target_monitor = HDMI-A-1
        }

        # Monitor 2 - right edge (different workspace name!)
        edge2 {
            enabled = 1
            side = right
            special_workspace = hotedge-right-dp3
            target_monitor = DP-3
        }
    }
}

# Separate workspace rules for each
workspace = special:hotedge-right-hdmi, gapsout:0 0 0 1700
workspace = special:hotedge-right-dp3, gapsout:0 0 0 1700
```

Find your monitor names with:
```bash
hyprctl monitors | grep Monitor
```

## Behavior Notes

- **Edge trigger**: Moving cursor to the absolute screen edge (last 2-3 pixels) triggers immediately, bypassing dwell time
- **Zone trigger**: Moving cursor into the trigger zone (configurable width) starts the dwell timer
- **Auto-hide**: Panel closes when cursor leaves the panel area (with 150ms delay to prevent flicker)
- **Monitor-aware**: Moving cursor to another monitor automatically closes the panel on the previous monitor
- **Keyboard grace period**: When toggling via keyboard, there's an 800ms grace period before auto-hide kicks in, giving you time to move your cursor to the panel

## Troubleshooting

### Plugin not loading
- Check Hyprland log: `tail -f /tmp/hypr/$HYPRLAND_INSTANCE_SIGNATURE/hyprland.log | grep HotEdge`
- Ensure the plugin path is correct
- Rebuild after Hyprland updates

### Edge not triggering
- Verify `enabled = 1` in config
- Check `target_monitor` matches your monitor name (`hyprctl monitors`)
- Ensure `special_workspace` is set

### Panel closes immediately
- Check if cursor is in the panel area when it opens
- For keyboard toggles, you have 800ms to move cursor to panel
- Verify no other window is stealing focus

### Animation issues on multi-monitor
- Use unique workspace names per monitor
- Don't share the same `special_workspace` across different `target_monitor` values

### Reserved keyword error
- Use `target_monitor` not `monitor` (monitor is a reserved Hyprland keyword)

## Example: Minimal Single-Monitor Setup

```conf
plugin = /path/to/hypr-hot-edge.so

plugin {
    hot-edge {
        edge1 {
            enabled = 1
            side = right
            special_workspace = sidebar
            target_monitor = *
        }
    }
}

workspace = special:sidebar, gapsout:0 0 0 1700
animation = specialWorkspace, 1, 3, almostLinear, fade

bind = SUPER, H, hotedge:toggle, right
```

## License

MIT
