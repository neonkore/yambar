pkgname=f00bar
pkgver=0.9.r1.g383e031
pkgrel=1
pkgdesc="Simplistic and highly configurable status panel for X"
arch=('x86_64')
url=https://gitlab.com/dnkl/f00bar
license=(mit)
depends=(
  'libxcb' 'xcb-util' 'xcb-util-cursor'
  'freetype2' 'fontconfig' 'cairo'
  'libyaml'
  'alsa-lib'
  'libsystemd'
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