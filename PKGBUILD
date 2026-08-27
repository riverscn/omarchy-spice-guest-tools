pkgname=omarchy-spice-guest-tools
pkgver=0.1.3
pkgrel=1
pkgdesc='System-packaged, per-user SPICE integration for Omarchy'
url='https://github.com/riverscn/omarchy-spice-guest-tools'
arch=('aarch64' 'x86_64')
license=('MIT')
install=omarchy-spice-guest-tools.install
depends=(
  'bash'
  'clipnotify'
  'coreutils'
  'gawk'
  'gdk-pixbuf2'
  'glib2'
  'gtk3'
  'hyprland'
  'jq'
  'libxcvt'
  'spice-vdagent'
  'systemd'
  'util-linux'
  'wayland'
  'wl-clipboard'
  'xclip'
)
makedepends=(
  'pkgconf'
  'wayland-protocols'
)
source=("${pkgname}-${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  make
}

check() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  make check
}

package() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  make DESTDIR="${pkgdir}" PREFIX=/usr install
}
