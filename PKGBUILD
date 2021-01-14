pkgname=yambar
pkgver=1.6.1
pkgrel=1
pkgdesc="Simplistic and highly configurable status panel for X and Wayland"
arch=('x86_64' 'aarch64')
url=https://codeberg.org/dnkl/yambar
license=(mit)
makedepends=('meson' 'ninja' 'scdoc' 'tllist>=1.0.1')
depends=(
  'libxcb' 'xcb-util' 'xcb-util-cursor' 'xcb-util-wm'
  'wayland'
  'pixman'
  'libyaml'
  'alsa-lib'
  'libudev.so'
  'json-c'
  'libmpdclient'
  'fcft>=2.0.0')
optdepends=('xcb-util-errors: better X error messages')
source=()

pkgver() {
  cd ../.git &> /dev/null && git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
      head -3 ../meson.build | grep version | cut -d "'" -f 2
}

build() {
  meson --buildtype=release --prefix=/usr --wrap-mode=nofallback -Db_lto=true -Dbackend-x11=enabled -Dbackend-wayland=enabled ../
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
