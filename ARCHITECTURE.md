# Architecture

The project is split into a compositor-independent SPICE core and desktop
integration backends.

## SPICE core

The core owns:

- SPICE monitor transaction parsing and duplicate suppression;
- SPICE/X11 to Wayland text clipboard bridging;
- active-seat session ownership;
- configuration, runtime state, and systemd user-service lifecycle;
- modeline generation and validation.

The core does not inspect a hypervisor, compositor, or desktop-vendor name.
It selects a display backend through `[integration].backend`. With `auto`, each
installed backend is probed and exactly one must match.

## Display backend contract

Backend implementations live in `libexec/backends/<name>.sh` and provide:

- `backend_probe`
- `backend_require_commands`
- `backend_config_file`
- `backend_snapshot_config`
- `backend_configure`
- `backend_restore_config`
- `backend_remove_integration`
- `backend_reload`
- `backend_integration_is_installed`
- `backend_detect_outputs`
- `backend_detect_any_output`
- `backend_detect_refresh_rate`
- `backend_apply_monitor_layout`
- `backend_config_is_clean`
- `backend_status`

Backend assets live under `backends/<name>/` and are installed into the matching
payload directory under `/usr/share/spice-guest-tools/backends/`.

## Current backend

`omarchy-hyprland` is the first backend. It owns every Omarchy/Hyprland-specific
assumption: `monitors.lua`, `hl.monitor`, `hyprctl`, output scale translation,
and the persistent Lua monitor block. Runtime rules retain the live per-output
scale, while persistent rules follow Omarchy's `omarchy_monitor_scale` setting.
Adding another compositor should require a new backend and its assets, without
changes to SPICE parsing or clipboard code.
