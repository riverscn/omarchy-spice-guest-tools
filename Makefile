PREFIX ?= /usr
DESTDIR ?=
CC ?= cc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner
VERSION := 0.1.4
NAME := omarchy-spice-guest-tools
BUILD_DIR := build
NATIVE_DIR := native
PROTOCOL_XML := /usr/share/wayland-protocols/staging/ext-data-control/ext-data-control-v1.xml
PROTOCOL_HEADER := $(BUILD_DIR)/ext-data-control-v1-client-protocol.h
PROTOCOL_CODE := $(BUILD_DIR)/ext-data-control-v1-protocol.c
PROVIDER := $(BUILD_DIR)/spice-clipboard-provider
CONTENT_TEST := $(BUILD_DIR)/clipboard-content-test
NATIVE_PACKAGES := wayland-client gio-2.0 gdk-pixbuf-2.0 gtk+-3.0
CPPFLAGS += -I$(BUILD_DIR) -I$(NATIVE_DIR)
CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror \
	$(shell $(PKG_CONFIG) --cflags $(NATIVE_PACKAGES))
LDLIBS += $(shell $(PKG_CONFIG) --libs $(NATIVE_PACKAGES))

.PHONY: all check clean install user-install uninstall-user dist

all: $(PROVIDER)

$(BUILD_DIR):
	mkdir -p "$@"

$(PROTOCOL_HEADER): $(PROTOCOL_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header "$<" "$@"

$(PROTOCOL_CODE): $(PROTOCOL_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code "$<" "$@"

$(BUILD_DIR)/clipboard-content.o: $(NATIVE_DIR)/clipboard-content.c \
		$(NATIVE_DIR)/clipboard-content.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/spice-clipboard-provider.o: $(NATIVE_DIR)/spice-clipboard-provider.c \
		$(NATIVE_DIR)/clipboard-content.h $(PROTOCOL_HEADER) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/ext-data-control-v1-protocol.o: $(PROTOCOL_CODE) $(PROTOCOL_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$(PROTOCOL_CODE)" -o "$@"

$(PROVIDER): $(BUILD_DIR)/spice-clipboard-provider.o \
		$(BUILD_DIR)/clipboard-content.o $(BUILD_DIR)/ext-data-control-v1-protocol.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(BUILD_DIR)/clipboard-content-test.o: $(NATIVE_DIR)/clipboard-content-test.c \
		$(NATIVE_DIR)/clipboard-content.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(CONTENT_TEST): $(BUILD_DIR)/clipboard-content-test.o $(BUILD_DIR)/clipboard-content.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

check: $(PROVIDER) $(CONTENT_TEST)
	$(CONTENT_TEST)
	./tests/run
	@if command -v shellcheck >/dev/null 2>&1; then \
		shellcheck bin/spice-guest-tools libexec/common.sh libexec/clipboard.sh \
			libexec/backends/omarchy-hyprland.sh \
			libexec/activate-active-users libexec/spice-clipboard-bridge \
			libexec/spice-display-bridge omarchy-spice-guest-tools.install tests/run; \
	else \
		printf '%s\n' 'shellcheck not installed; skipped (install: omarchy pkg aur add shellcheck-bin)'; \
	fi

clean:
	rm -rf -- "$(BUILD_DIR)"

install: $(PROVIDER)
	install -Dm755 bin/spice-guest-tools "$(DESTDIR)$(PREFIX)/bin/spice-guest-tools"
	install -Dm755 libexec/spice-clipboard-bridge "$(DESTDIR)$(PREFIX)/bin/spice-clipboard-bridge"
	install -Dm755 libexec/spice-display-bridge "$(DESTDIR)$(PREFIX)/bin/spice-display-bridge"
	install -Dm755 "$(PROVIDER)" "$(DESTDIR)$(PREFIX)/bin/spice-clipboard-provider"
	install -Dm644 libexec/common.sh "$(DESTDIR)$(PREFIX)/lib/spice-guest-tools/common.sh"
	install -Dm644 libexec/clipboard.sh "$(DESTDIR)$(PREFIX)/lib/spice-guest-tools/clipboard.sh"
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

user-install: $(PROVIDER)
	install -Dm755 bin/spice-guest-tools "$(HOME)/.local/bin/spice-guest-tools"
	install -Dm755 libexec/spice-clipboard-bridge "$(HOME)/.local/bin/spice-clipboard-bridge"
	install -Dm755 libexec/spice-display-bridge "$(HOME)/.local/bin/spice-display-bridge"
	install -Dm755 "$(PROVIDER)" "$(HOME)/.local/bin/spice-clipboard-provider"
	install -Dm644 libexec/common.sh "$(HOME)/.local/lib/spice-guest-tools/common.sh"
	install -Dm644 libexec/clipboard.sh "$(HOME)/.local/lib/spice-guest-tools/clipboard.sh"
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
	rm -f "$(HOME)/.local/bin/spice-clipboard-provider"
	rm -f "$(HOME)/.local/lib/spice-guest-tools/common.sh"
	rm -f "$(HOME)/.local/lib/spice-guest-tools/clipboard.sh"
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
