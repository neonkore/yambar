pkgname=f00bar
pkgver=$(git describe --always)
pkgrel=1
pkgdesc="Simplistic and highly configurable status panel for X"
arch=('x86_64')
url=https://gitlab.com/dnkl/f00bar
license=(mit)
source=()

build() {
  cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr ../
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
