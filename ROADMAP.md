# Roadmap

## 0.1.0 — First release

- [x] Plain-text and image SPICE/X11 to Wayland clipboard bridge
- [x] Immediate SPICE monitor request parser with duplicate suppression
- [x] Dynamic Hyprland modeline persistence
- [x] Hypervisor-independent active-output detection
- [x] Keep display scale owned exclusively by Omarchy
- [x] Derive refresh rate automatically from Hyprland
- [x] Apply runtime modelines atomically without restarting the SPICE agent
- [x] systemd user services for `graphical-session.target`
- [x] Zero-configuration per-user bootstrap for system/AUR installations
- [x] Immediate activation of an existing Wayland session after package install
- [x] Persistent opt-out without root package hooks modifying user homes
- [x] System-package/per-user-runtime production boundary
- [x] Active-seat SPICE ownership across overlapping user sessions
- [x] Split compositor-independent SPICE core from desktop display backends
- [x] User configuration with strict validation
- [x] Safe install, uninstall, status, doctor, restart, and logs commands
- [x] Arch package skeleton and isolated installer tests

## Hardening

- [x] Add fixture coverage for duplicate and malformed SPICE journal records
- [ ] Add crash/restart and rapid resize integration tests
- [ ] Verify dynamic resizing across representative Omarchy scale values
- [x] Validate transactional multi-output support with two live UTM displays
- [ ] Add a stable event source that does not depend on debug log wording
- [ ] Add release CI
- [ ] Add a standard Hyprland backend independent of Omarchy's Lua config

## Distribution

- [ ] Test on aarch64 and x86_64 Omarchy guests
- [ ] Publish signed release archives and checksums
- [ ] Publish AUR metadata
- [ ] Propose an optional `omarchy setup spice` entry upstream
