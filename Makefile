PREFIX ?= /usr
DESTDIR ?=
VERSION := 0.1.0
NAME := omarchy-spice-guest-tools

.PHONY: all check install user-install uninstall-user dist

all: check

check:
	./tests/run
	@if command -v shellcheck >/dev/null 2>&1; then \
		shellcheck bin/spice-guest-tools libexec/common.sh \
			libexec/backends/omarchy-hyprland.sh \
			libexec/activate-active-users libexec/spice-clipboard-bridge \
			libexec/spice-display-bridge omarchy-spice-guest-tools.install tests/run; \
	else \
		printf '%s\n' 'shellcheck not installed; skipped (install: omarchy pkg aur add shellcheck-bin)'; \
	fi

install:
	install -Dm755 bin/spice-guest-tools "$(DESTDIR)$(PREFIX)/bin/spice-guest-tools"
	install -Dm755 libexec/spice-clipboard-bridge "$(DESTDIR)$(PREFIX)/bin/spice-clipboard-bridge"
	install -Dm755 libexec/spice-display-bridge "$(DESTDIR)$(PREFIX)/bin/spice-display-bridge"
	install -Dm644 libexec/common.sh "$(DESTDIR)$(PREFIX)/lib/spice-guest-tools/common.sh"
	install -Dm644 libexec/backends/omarchy-hyprland.sh \
		"$(DESTDIR)$(PREFIX)/lib/spice-guest-tools/backends/omarchy-hyprland.sh"
	install -Dm755 libexec/activate-active-users \
		"$(DESTDIR)$(PREFIX)/lib/spice-guest-tools/activate-active-users"
	install -Dm644 systemd/user/spice-clipboard-bridge.service \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/spice-clipboard-bridge.service"
	install -Dm644 systemd/user/spice-display-bridge.service \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/spice-display-bridge.service"
	install -Dm644 systemd/user/spice-guest-tools-bootstrap.service \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/spice-guest-tools-bootstrap.service"
	install -Dm644 systemd/user/spice-vdagent.service.d/50-spice-guest-tools.conf \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/spice-vdagent.service.d/50-spice-guest-tools.conf"
	install -Dm644 config/config.toml \
		"$(DESTDIR)$(PREFIX)/share/spice-guest-tools/config/config.toml"
	install -Dm644 backends/omarchy-hyprland/managed-monitor.lua \
		"$(DESTDIR)$(PREFIX)/share/spice-guest-tools/backends/omarchy-hyprland/managed-monitor.lua"
	install -Dm644 README.md "$(DESTDIR)$(PREFIX)/share/doc/$(NAME)/README.md"
	install -Dm644 ROADMAP.md "$(DESTDIR)$(PREFIX)/share/doc/$(NAME)/ROADMAP.md"
	install -Dm644 CHANGELOG.md "$(DESTDIR)$(PREFIX)/share/doc/$(NAME)/CHANGELOG.md"
	install -Dm644 ARCHITECTURE.md "$(DESTDIR)$(PREFIX)/share/doc/$(NAME)/ARCHITECTURE.md"
	install -Dm644 LICENSE "$(DESTDIR)$(PREFIX)/share/licenses/$(NAME)/LICENSE"
	install -d "$(DESTDIR)$(PREFIX)/lib/systemd/user/graphical-session.target.wants"
	ln -sfn ../spice-guest-tools-bootstrap.service \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/graphical-session.target.wants/spice-guest-tools-bootstrap.service"
	ln -sfn ../spice-clipboard-bridge.service \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/graphical-session.target.wants/spice-clipboard-bridge.service"
	ln -sfn ../spice-display-bridge.service \
		"$(DESTDIR)$(PREFIX)/lib/systemd/user/graphical-session.target.wants/spice-display-bridge.service"

user-install:
	install -Dm755 bin/spice-guest-tools "$(HOME)/.local/bin/spice-guest-tools"
	install -Dm755 libexec/spice-clipboard-bridge "$(HOME)/.local/bin/spice-clipboard-bridge"
	install -Dm755 libexec/spice-display-bridge "$(HOME)/.local/bin/spice-display-bridge"
	install -Dm644 libexec/common.sh "$(HOME)/.local/lib/spice-guest-tools/common.sh"
	install -Dm644 libexec/backends/omarchy-hyprland.sh \
		"$(HOME)/.local/lib/spice-guest-tools/backends/omarchy-hyprland.sh"
	install -Dm644 systemd/user/spice-clipboard-bridge.service \
		"$(HOME)/.config/systemd/user/spice-clipboard-bridge.service"
	install -Dm644 systemd/user/spice-display-bridge.service \
		"$(HOME)/.config/systemd/user/spice-display-bridge.service"
	install -Dm644 systemd/user/spice-guest-tools-bootstrap.service \
		"$(HOME)/.config/systemd/user/spice-guest-tools-bootstrap.service"
	install -Dm644 systemd/user/spice-vdagent.service.d/50-spice-guest-tools.conf \
		"$(HOME)/.config/systemd/user/spice-vdagent.service.d/50-spice-guest-tools.conf"
	install -Dm644 config/config.toml \
		"$(HOME)/.local/share/spice-guest-tools/config/config.toml"
	install -Dm644 backends/omarchy-hyprland/managed-monitor.lua \
		"$(HOME)/.local/share/spice-guest-tools/backends/omarchy-hyprland/managed-monitor.lua"
	install -d "$(HOME)/.config/systemd/user/graphical-session.target.wants"
	ln -sfn ../spice-guest-tools-bootstrap.service \
		"$(HOME)/.config/systemd/user/graphical-session.target.wants/spice-guest-tools-bootstrap.service"
	ln -sfn ../spice-clipboard-bridge.service \
		"$(HOME)/.config/systemd/user/graphical-session.target.wants/spice-clipboard-bridge.service"
	ln -sfn ../spice-display-bridge.service \
		"$(HOME)/.config/systemd/user/graphical-session.target.wants/spice-display-bridge.service"
	systemctl --user daemon-reload
	@printf '%s\n' \
		'Development user installation complete. Run: spice-guest-tools install'

uninstall-user:
	"$(HOME)/.local/bin/spice-guest-tools" uninstall || true
	rm -f "$(HOME)/.local/bin/spice-guest-tools"
	rm -f "$(HOME)/.local/bin/spice-clipboard-bridge"
	rm -f "$(HOME)/.local/bin/spice-display-bridge"
	rm -f "$(HOME)/.local/lib/spice-guest-tools/common.sh"
	rm -f "$(HOME)/.local/lib/spice-guest-tools/backends/omarchy-hyprland.sh"
	rm -f "$(HOME)/.config/systemd/user/spice-clipboard-bridge.service"
	rm -f "$(HOME)/.config/systemd/user/spice-display-bridge.service"
	rm -f "$(HOME)/.config/systemd/user/spice-guest-tools-bootstrap.service"
	rm -f "$(HOME)/.config/systemd/user/spice-vdagent.service.d/50-spice-guest-tools.conf"
	rm -f "$(HOME)/.config/systemd/user/graphical-session.target.wants/spice-guest-tools-bootstrap.service"
	rm -f "$(HOME)/.config/systemd/user/graphical-session.target.wants/spice-clipboard-bridge.service"
	rm -f "$(HOME)/.config/systemd/user/graphical-session.target.wants/spice-display-bridge.service"
	systemctl --user daemon-reload

dist:
	git archive --format=tar.gz --prefix="$(NAME)-$(VERSION)/" \
		-o "$(NAME)-$(VERSION).tar.gz" HEAD
