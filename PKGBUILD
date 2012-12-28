# Maintainer: Jesse McClure AKA "Trilby" <jmcclure [at] cns [dot] umass [dot] edu>
pkgname=wifi-git
pkgver=20121228
pkgrel=1
pkgdesc="Wireless connection script with auto connect"
url="http://github.com/TrilbyWhite/wifi2.git"
arch=('any')
license=('GPLv3')
depends=('wireless_tools' 'iproute2' 'wpa_supplicant' 'dhcpcd')
makedepends=('git')
_gitroot="git://github.com/TrilbyWhite/wifi2.git"
_gitname="wifi2"

build() {
    cd "$srcdir"
    msg "Connecting to GIT server...."
    if [ -d $_gitname ] ; then
        cd $_gitname && git pull origin
        msg "The local files are updated."
    else
        git clone $_gitroot $_gitname
    fi
    msg "GIT checkout done or server timeout"
    msg "Starting make..."
    rm -rf "$srcdir/$_gitname-build"
    git clone "$srcdir/$_gitname" "$srcdir/$_gitname-build"
    cd "$srcdir/$_gitname-build"
	make
}

package() {
	cd "$srcdir/$_gitname-build"
	make PREFIX=/usr DESTDIR=$pkgdir install
}

