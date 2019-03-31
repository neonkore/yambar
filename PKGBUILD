pkgname=f00bar
pkgver=0.9.0.r159.g902245d
pkgrel=1
pkgdesc="Simplistic and highly configurable status panel for X and Wayland"
arch=('x86_64')
url=https://gitlab.com/dnkl/f00bar
license=(mit)
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
  git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'
}

build() {
  cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr ../
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
