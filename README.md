# Omarchy SPICE Guest Tools

Wayland text and image clipboard sharing plus dynamic SPICE display resizing
for Omarchy guests running Hyprland. The project adapts SPICE's X11-oriented
guest agent to the native Wayland session without depending on a particular
hypervisor or virtual machine frontend, and without modifying files under
`/usr/share/omarchy`.

Runtime activation is based on the standard SPICE agent channel at
`/dev/virtio-ports/com.redhat.spice.0`. There is no hypervisor check or
display-vendor matching.

## Status

This repository is an early MVP. UTM with a QEMU aarch64 guest is the currently
verified host environment, not a runtime requirement. Text and image clipboard
sharing, dynamic modelines, transactional multi-monitor layout handling, safe
installation, and diagnostics are implemented. Clipboard images and single-
and dual-display resizing are tested on a live UTM aarch64 guest.

## Components

- `spice-clipboard-bridge`: two-way text and image X11/Wayland clipboard bridge
- `spice-display-bridge`: immediate, atomic SPICE monitor-layout translator
- `spice-guest-tools-bootstrap.service`: idempotent per-user configuration at
  login, implemented by the `spice-guest-tools bootstrap` command
- `spice-guest-tools`: install, bootstrap, uninstall, status, doctor, restart,
  logs, and version CLI
- systemd user services attached to `graphical-session.target`
- a display-backend interface; `omarchy-hyprland` is the first implementation

The core SPICE/session/clipboard code has no Omarchy or Hyprland assumptions.
The current backend exclusively owns `monitors.lua`, `hl.monitor`, `hyprctl`,
output mapping, and runtime layout application. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the backend contract.

## Package installation

The supported production model is system-managed distribution with per-user
execution:

- pacman owns the immutable programs, payloads, and user-unit definitions under
  `/usr`;
- systemd user services own the running clipboard and display processes;
- the logged-in user owns configuration and state under `~/.config` and
  `~/.local/state`;
- no system-level daemon runs as root.

The system package installs programs and shared resources under `/usr`, then
globally attaches three systemd user units to `graphical-session.target`. Its
package lifecycle hook selects active, local Wayland user sessions and asks
their systemd user managers to run the one-shot bootstrap immediately. The
bootstrap creates the user's default configuration, installs or upgrades the
marked Hyprland monitor block, and allows the two bridge services to start.
The root package hook never writes to a user's home directory itself.

After installing a package built from the `PKGBUILD`, no manual configuration,
logout, or reboot is required. Until the package is published to the AUR,
install the release-backed `PKGBUILD` from GitHub:

```bash
git clone https://github.com/riverscn/omarchy-spice-guest-tools.git
cd omarchy-spice-guest-tools
makepkg -si
```

Once the package is published to the AUR, install it through Omarchy with:

```bash
omarchy pkg aur add omarchy-spice-guest-tools
```

If installation occurs without an active local Wayland session, the globally
attached user units perform the same setup automatically at the next graphical
login. `spice-guest-tools install` remains available as a recovery and development
command, not as a normal package-install step.

Removing the user integration with `spice-guest-tools uninstall` creates a local
opt-out marker, so the globally enabled bootstrap will not recreate it at the
next login. Running `install` again removes that marker.

Each login account receives independent configuration and state. When local
graphical sessions overlap during fast user switching, both users may retain
running bridge processes, but every clipboard and resize operation is guarded
by logind session ownership. Only the active, non-remote Wayland user on
`seat0` can interact with SPICE; inactive users become inert and automatically
regain access when switched back to the foreground.

## Development-only user install

`make user-install` is intended for source-tree development and testing. It is
not the supported installation method for end users and does not provide
pacman-managed dependencies, upgrades, integrity checks, or removal. Install
the development and runtime dependencies explicitly on a fresh Omarchy guest:

```bash
omarchy pkg add base-devel clipnotify gawk jq libxcvt lua spice-vdagent \
  wl-clipboard xclip
```

ShellCheck is optional but recommended. On Omarchy aarch64, use the AUR
static-binary package, which provides an aarch64 binary without requiring the
Haskell build toolchain:

```bash
omarchy pkg aur add shellcheck-bin
```

If `omarchy pkg add shellcheck` does not provide a build for the guest
architecture, use `shellcheck-bin` as shown above. Avoid the source-built
`shellcheck-git` package unless testing ShellCheck development snapshots is the
goal.

On a fresh supported Omarchy guest:

```bash
make check
make user-install
spice-guest-tools install
spice-guest-tools doctor
```

`make user-install` copies project-owned files into `~/.local` and
`~/.config/systemd/user`. `spice-guest-tools install` creates user configuration,
adds a marked dynamic-mode block to `~/.config/hypr/monitors.lua`, backs up the
file before changing it, and enables the bootstrap and both bridge services.

For a staged system package installation, `make DESTDIR=<package-root>
PREFIX=/usr install` installs global user units and their graphical-session
wants links, plus the active-session package helper, without touching any
user's home directory.

## Configuration

User configuration lives at:

```text
~/.config/spice-guest-tools/config.toml
```

The complete default configuration is:

```toml
[integration]
backend = "auto"

[display]
enabled = true
output = "auto"

[clipboard]
enabled = true
max_bytes = 104857600
```

It currently resolves to `omarchy-hyprland`. An explicit backend name is useful
when more than one installed backend can handle the active desktop session.
Set `display.enabled` or `clipboard.enabled` to `false` to disable only that
bridge. `clipboard.max_bytes` is the maximum accepted text or image clipboard
item size in bytes; the default is 100 MiB and the accepted range is 1 byte
through 1 GiB. Image forwarding accepts PNG, JPEG, TIFF, and BMP MIME types and
preserves their encoded bytes without conversion.

When the number of active Hyprland outputs matches the SPICE monitor layout,
outputs are mapped automatically in Hyprland output order without inspecting
hypervisor or display-vendor names. The optional `display.output`
override is retained for unusual single-display guests only and is rejected
for a multi-monitor layout. The bridge derives refresh rate independently from
each output's current Hyprland state, rounds it to a stable integer, and caches
it for the process lifetime. A 60Hz internal fallback is used only when the
system value cannot be read. SPICE reports the final layout, so the bridge applies
each new complete layout immediately and suppresses only exact duplicates.

Runtime state is stored under `$XDG_STATE_HOME/spice-guest-tools`, and temporary
files and locks live under `$XDG_RUNTIME_DIR/spice-guest-tools`.

Display scale remains owned entirely by Omarchy's monitor configuration. Stock
configurations may let Hyprland select it automatically:

```lua
local omarchy_monitor_scale = "auto"
```

To pin a numeric scale instead, set the same variable explicitly, for example:

```lua
local omarchy_monitor_scale = 1.5
```

Keep this setting in `~/.config/hypr/monitors.lua` as usual. The SPICE
integration reads each output's live scale only to translate physical SPICE
coordinates into Hyprland's logical coordinate space. Runtime monitor updates
explicitly retain each output's current live scale. Persisted SPICE rules use
`omarchy_monitor_scale`, so Hyprland does not fall back to scale 1 after a
restart and changes made through Omarchy's display settings continue to apply.

## Uninstall

```bash
spice-guest-tools uninstall
```

This disables the services and removes only the marked Hyprland integration.
Configuration and state are retained. Use `uninstall --purge` to remove the
configuration and active display/bootstrap state. Safety backups and the local
opt-out marker are retained.

For a source/user installation, `make uninstall-user` then removes the copied
program and unit files.

## Packaging

The included `PKGBUILD` consumes an archive named
`omarchy-spice-guest-tools-<version>.tar.gz`; `make dist` creates that archive
from `HEAD` for local package builds. Signed release archives, checksums, and AUR
metadata will be added after the SPICE host-environment test matrix is complete.

## Known limitations

- Clipboard support is limited to plain text and PNG, JPEG, TIFF, or BMP image
  data. File-manager copy/paste and other rich clipboard formats are not
  supported.
- Display requests are currently parsed from `spice-vdagent` 0.23.x debug
  logs; other versions have not yet been validated.
- Runtime resizing requires Hyprland's Lua `eval` control API.
- The only display backend currently shipped is `omarchy-hyprland`; the SPICE
  core is ready for additional desktops, but they are not implemented yet.

See [ROADMAP.md](ROADMAP.md) for the release milestones.
