pkgname=f00bar
pkgver=1.0.3
pkgrel=1
pkgdesc="Simplistic and highly configurable status panel for X and Wayland"
arch=('x86_64')
url=https://gitlab.com/dnkl/f00bar
license=(mit)
makedepends=('meson' 'ninja' 'scdoc')
depends=(
  'libxcb' 'xcb-util' 'xcb-util-cursor' 'xcb-util-wm'
  'wayland' 'wlroots'
  'freetype2' 'fontconfig' 'cairo'
  'libyaml'
  'alsa-lib'
  'libudev.so'
  'json-c'
  'libmpdclient')
optdepends=('xcb-util-errors: better X error messages')
source=()

pkgver() {
  [ -d ../.git ] && git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'
  [ ! -d ../.git ] && head -3 ../meson.build | grep version | cut -d "'" -f 2
}

build() {
  meson --buildtype=minsize --prefix=/usr -Dbackend-x11=enabled -Dbackend-wayland=enabled ../
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
