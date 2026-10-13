#!/bin/sh
#
# build-linux-tarball.sh VERSION [OUTDIR]
#
# Builds both engines and packs them with the launcher, the menu entry, the
# shareware episode and install.sh into wolf3d-linux-ARCH-VERSION.tar.gz.
#
# The engines are the same ones the container runs. They run on
# distributions with the same C library as the machine they are built on or
# newer, so the release is built on an old one: see the linux job in
# .github/workflows/docker-publish.yml.
#

set -eu

version="${1:?usage: build-linux-tarball.sh VERSION [OUTDIR]}"
out="${2:-.}"
here=$(cd "$(dirname "$0")/.." && pwd)
arch=$(uname -m)
name="wolf3d-linux-$arch-$version"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

make -C "$here/WOLFSRC" clean >/dev/null
make -C "$here/WOLFSRC" -j"$(nproc)"

root="$stage/$name"
install -d "$root/bin" "$root/lib/wolf3d/shareware" \
           "$root/share/applications" "$root/share/icons/hicolor/128x128/apps"
install -m 755 "$here"/WOLFSRC/linux/wolf3d "$here"/WOLFSRC/linux/wolf3d-wl1 \
               "$here"/WOLFSRC/linux/wolf3d-sod "$here"/WOLFSRC/linux/wolf3d-sdm "$root/lib/wolf3d/"
strip "$root"/lib/wolf3d/wolf3d*
install -m 644 "$here"/shareware/* "$root/lib/wolf3d/shareware/"
install -m 755 "$here/desktop/wolf3d" "$root/bin/wolf3d"
install -m 644 "$here/desktop/wolf3d.desktop" "$root/share/applications/"
install -m 644 "$here/templates/wolf3d-icon.png" \
               "$root/share/icons/hicolor/128x128/apps/wolf3d.png"
install -m 755 "$here/desktop/install.sh" "$root/install.sh"
install -m 644 "$here/desktop/README.txt" "$root/README.txt"
install -m 644 "$here/WOLFSRC/opl/LICENSE" "$root/lib/wolf3d/OPL3-LICENSE"

mkdir -p "$out"
tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
