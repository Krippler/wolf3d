#!/bin/sh
#
# smoke-test.sh [WORKDIR]
#
# Plays a little of the shareware episode's first level on a throwaway Xvfb
# and checks what comes out:
#
#   - the picture is a picture, not one colour
#   - holding the up arrow walks, the strafe key steps aside, and the
#     controller's stick turns
#   - the controller connection says which shape to draw the picture
#   - there is sound, music and effects both, in the stream a browser gets
#   - a game saved is a game that loads
#   - F10, Y quits cleanly with the order screen
#   - the Settings pages change the picture's shape, and it is remembered
#   - the registered-game engine turns the shareware away and says why
#
# Needs the engines built (make -C WOLFSRC), and Xvfb, xdotool, ImageMagick's
# import and compare, and python3. Screenshots and logs are left in WORKDIR.
#

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
work="${1:-$(mktemp -d)}"
mkdir -p "$work"
work=$(cd "$work" && pwd)

wl1="$here/WOLFSRC/linux/wolf3d-wl1"
wl6="$here/WOLFSRC/linux/wolf3d"
data="$here/shareware"
disp=":91"
padport=5992

fails=0
pass() { printf 'ok    %s\n' "$*"; }
fail() { printf 'FAIL  %s\n' "$*"; fails=$((fails + 1)); }

[ -x "$wl1" ] && [ -x "$wl6" ] || { echo "build the engines first: make -C WOLFSRC"; exit 1; }

Xvfb "$disp" -screen 0 640x480x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xvfb=$!
engine=""
cleanup() {
    [ -n "$engine" ] && kill "$engine" 2>/dev/null || true
    kill "$xvfb" 2>/dev/null || true
}
trap cleanup EXIT
export DISPLAY="$disp"
i=0
until xdotool getdisplaygeometry >/dev/null 2>&1 || [ "$i" -gt 50 ]; do sleep 0.1; i=$((i + 1)); done

shot() { import -window root "$work/$1.png"; }
press() { xdotool keydown "$1"; sleep "${2:-0.15}"; xdotool keyup "$1"; }
colours() { convert "$work/$1.png" -format %k info:; }
pixels() { compare -metric AE "$work/$1.png" "$work/$2.png" null: 2>&1 | cut -d' ' -f1; }
differ() { [ "$(pixels "$1" "$2")" != "0" ]; }
# The same view, give or take a door frame or a flicker: within 2%
same() { [ "$(pixels "$1" "$2")" -lt 6144 ]; }

# A controller's state, as play.html sends it: 'P' connected buttons lx ly rx ry lt rt
pad() {
    python3 - "$padport" "$1" "$2" <<'EOF'
import socket, struct, sys, time
port, buttons, rx = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
s = socket.create_connection(('127.0.0.1', port))
s.settimeout(2)
first = s.recv(16)
print('aspect' if first[:1] == b'A' else 'no aspect')
s.sendall(b'P' + struct.pack('<BIhhhhBB', 1, buttons, 0, 0, rx, 0, 0, 0))
time.sleep(1.0)
s.sendall(b'P' + struct.pack('<BIhhhhBB', 1, 0, 0, 0, 0, 0, 0, 0))
time.sleep(0.3)
s.close()
EOF
}

state="$work/state"
rm -rf "$state"
mkdir -p "$state"
cd "$state"

#
# A level, played
#
WOLF_DATA="$data" WOLF_AUDIO="file:$work/audio.raw" WOLF_PAD_PORT="$padport" \
    "$wl1" -nowait -tedlevel 0 >"$work/engine.log" 2>&1 &
engine=$!
sleep 4

shot 1-start
[ "$(colours 1-start)" -gt 20 ] && pass "the level is drawn" || fail "the first picture is $(colours 1-start) colours"

press Up 1.0
sleep 0.3
shot 2-walked
differ 1-start 2-walked && pass "the up arrow walks" || fail "the up arrow changed nothing"

said=$(pad 0 -32767)		# the right stick, pushed left: turn
sleep 0.3
shot 3-pad
[ "$said" = "aspect" ] && pass "the controller connection says which shape to draw" \
                       || fail "the controller connection said nothing first"
differ 2-walked 3-pad && pass "the controller turns" || fail "the controller changed nothing"

press period 0.8		# the strafe-right key, by default
sleep 0.3
shot 3-strafed
differ 3-pad 3-strafed && pass "the strafe key steps sideways" || fail "the strafe key changed nothing"

press Control_L 0.2
sleep 0.5

# Save in slot 0, walk on, load it back: the picture should be the saved one
press F2
sleep 1.5
press Return
sleep 0.7
for k in s m o k e; do press "$k" 0.08; done
press Return
sleep 2
shot 4-saved
press Left 0.8
press Up 0.6
sleep 0.3
shot 5-moved
press F3
sleep 1.5
press Return
sleep 2
shot 6-loading
sleep 4
shot 6-loaded
ls SAVEGAM0.WL1 >/dev/null 2>&1 && pass "a game was saved" || fail "no savegame was written"
if ! same 4-saved 5-moved && same 4-saved 6-loaded; then
    pass "the saved game loads where it was saved"
else
    fail "the loaded game is not where it was saved (see 4-saved, 6-loaded)"
fi

press F10
sleep 1.5
press y
status=0
wait "$engine" || status=$?
engine=""
[ "$status" = "0" ] && pass "F10, Y quits cleanly" || fail "quitting exited with status $status"
grep -q "Thanks for playing" "$work/engine.log" && pass "the order screen is printed" \
                                               || fail "no order screen on the way out"

#
# The sound: a 44.1 kHz 16-bit stereo stream, as the browser gets it
#
python3 - "$work/audio.raw" <<'EOF' && pass "there is sound" || fail "the sound is silent"
import struct, sys
d = open(sys.argv[1], 'rb').read()
n = len(d) // 2
s = struct.unpack('<%dh' % n, d[:n * 2])
loud = sum(1 for i in range(0, n, 441 * 2) if abs(s[i]) > 300)
print('      %.1f s of sound, %d of %d hundredths above the noise' % (n / 2 / 44100, loud, n // 882))
sys.exit(0 if loud > n // 882 // 4 else 1)
EOF

#
# The Settings pages: F12, Picture, the shape made square. The window is
# remade 640x400, the choice is written down, and the next run starts so.
#
geometry() { xdotool search --name "Wolfenstein" getwindowgeometry 2>/dev/null | sed -n 's/.*Geometry: //p' | head -1; }
WOLF_DATA="$data" WOLF_AUDIO=none "$wl1" -nowait -tedlevel 0 >"$work/settings.log" 2>&1 &
engine=$!
sleep 4
before=$(geometry)
press F12
sleep 1.5
press Return		# Picture: Game is not offered without a launcher
sleep 1.5
press Down		# past View size to Shape
press Left
sleep 0.5
shot 7-settings
after=$(geometry)
press Escape
sleep 1
press Escape
sleep 1.5
press F10
sleep 1.5
press y
wait "$engine" || true
engine=""
if [ "$before" = "640x480" ] && [ "$after" = "640x400" ] && grep -q "^shape 0" settings 2>/dev/null; then
    pass "the Settings pages make the picture square, and say so in settings"
else
    fail "the Settings pages: the window went from $before to $after; settings: $(cat settings 2>/dev/null | tr '\n' ' ')"
fi
WOLF_DATA="$data" WOLF_AUDIO=none "$wl1" -nowait -tedlevel 0 >>"$work/settings.log" 2>&1 &
engine=$!
sleep 3
again=$(geometry)
kill "$engine" 2>/dev/null; wait "$engine" 2>/dev/null || true
engine=""
[ "$again" = "640x400" ] && pass "the next run starts with the settings it was left with" \
                         || fail "the next run's window is $again, not 640x400"

#
# The wrong engine for the data
#
status=0
WOLF_DATA="$data" WOLF_AUDIO=none "$wl6" >"$work/wl6.log" 2>&1 || status=$?
if [ "$status" = "1" ] && grep -q "wolf3d-wl1 plays the shareware" "$work/wl6.log"; then
    pass "the registered-game engine sends the shareware to wolf3d-wl1"
else
    fail "the registered-game engine with the shareware: status $status, $(cat "$work/wl6.log")"
fi

echo
if [ "$fails" = "0" ]; then
    echo "all passed; screenshots and logs in $work"
else
    echo "$fails failed; screenshots and logs in $work"
fi
exit "$fails"
