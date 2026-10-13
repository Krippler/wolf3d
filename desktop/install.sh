#!/bin/sh
#
# install.sh [PREFIX]
#
# Installs Wolfenstein 3-D for this user, into ~/.local, or for everyone into
# PREFIX (sudo ./install.sh /usr/local). install.sh --uninstall [PREFIX]
# takes it out again; savegames in ~/.local/share/wolf3d are left alone.
#

set -eu

here=$(cd "$(dirname "$0")" && pwd)

uninstall=0
if [ "${1:-}" = "--uninstall" ]; then
    uninstall=1
    shift
fi
prefix="${1:-$HOME/.local}"

if [ "$uninstall" = "1" ]; then
    rm -rf "$prefix/lib/wolf3d"
    rm -f "$prefix/bin/wolf3d" "$prefix/share/applications/wolf3d.desktop" \
          "$prefix/share/icons/hicolor/128x128/apps/wolf3d.png"
    echo "Removed Wolfenstein 3-D from $prefix."
    exit 0
fi

install -d "$prefix/bin" "$prefix/lib/wolf3d" "$prefix/share/applications" \
           "$prefix/share/icons/hicolor/128x128/apps"
rm -rf "$prefix/lib/wolf3d/shareware"
cp -R "$here/lib/wolf3d/." "$prefix/lib/wolf3d/"
install -m 755 "$here/bin/wolf3d" "$prefix/bin/wolf3d"
install -m 644 "$here/share/applications/wolf3d.desktop" "$prefix/share/applications/"
install -m 644 "$here/share/icons/hicolor/128x128/apps/wolf3d.png" \
               "$prefix/share/icons/hicolor/128x128/apps/"

# Menus pick a new entry up on their own, mostly; this hurries the rest
command -v update-desktop-database >/dev/null 2>&1 \
    && update-desktop-database -q "$prefix/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
    && gtk-update-icon-cache -q -t "$prefix/share/icons/hicolor" 2>/dev/null || true

echo "Installed Wolfenstein 3-D in $prefix."
case ":$PATH:" in
    *":$prefix/bin:"*) echo "Start it from your desktop's menu, or run: wolf3d" ;;
    *) echo "Start it from your desktop's menu, or run: $prefix/bin/wolf3d" ;;
esac
echo "It plays the shareware episode until it finds your own copy of the game:"
echo "  wolf3d --data /path/to/the/directory/with/VSWAP.WL6"
