#!/bin/sh
#
# Starts Wolfenstein 3-D inside the container, playable from a browser.
#
#   Xvfb         an X server of the container's own, for the engine to draw in
#   x11vnc       exports it over VNC
#   wolf-wsproxy websockify with the noVNC client, on one port, carrying the
#                picture, the sound and a game controller
#   wolf3d       the engine, restarted if it quits or falls over
#
# Everything the engine writes -- savegames, its settings, the high scores --
# goes in WOLF_STATE, which is the volume to keep. The game's data comes
# from WOLF_DATAMOUNT if anything is mounted there, and from the shareware
# episode built into the image if not.
#
# The DOOM container's entrypoint, docker/entrypoint.sh in that repository,
# is where most of what follows was worked out; the comments there say why
# x11vnc is run the way it is.
#

set -eu

STATE="${WOLF_STATE:-/wolf/state}"
DATAMOUNT="${WOLF_DATAMOUNT:-/data}"
SHAREWARE="${WOLF_SHAREWARE:-/usr/share/wolf3d/shareware}"
ENGINES="${WOLF_ENGINES:-/usr/local/games}"
NOVNC_ROOT="${WOLF_NOVNC_ROOT:-/usr/share/novnc}"
WSPROXY_BIN="${WOLF_WSPROXY_BIN:-/usr/local/bin/wolf-wsproxy}"

DISP="${WOLF_DISPLAY:-:99}"
VNC_PORT="${WOLF_VNC_PORT:-5900}"
AUDIO_PORT="${WOLF_AUDIO_PORT:-5901}"
PAD_PORT="${WOLF_PAD_PORT:-5902}"
WEB_PORT="${WOLF_WEB_PORT:-6080}"
SCALE="${WOLF_SCALE:-2}"
RESTART="${WOLF_RESTART:-1}"

log() { printf 'wolf3d: %s\n' "$*" >&2; }
die() { log "$*"; exit 1; }

#
# Start as root only long enough to make the state directory ours, then drop
# to PUID:PGID -- 1001 unless told otherwise, 99:100 on Unraid. Started with
# --user there is nothing to do and PUID/PGID are ignored.
#
if [ "$(id -u)" = "0" ] && [ -z "${WOLF_DROPPED:-}" ]; then
    PUID="${PUID:-1001}"
    PGID="${PGID:-1001}"

    case "$PUID$PGID" in
        *[!0-9]*) die "PUID and PGID must be numeric (got '$PUID' and '$PGID')" ;;
    esac

    mkdir -p "$STATE"
    chown "$PUID:$PGID" "$STATE" 2>/dev/null || true

    export WOLF_DROPPED=1
    if [ "$PUID" = "0" ] && [ "$PGID" = "0" ]; then
        log "running as root (PUID=0)"
    else
        log "running as ${PUID}:${PGID}"
        exec setpriv --reuid "$PUID" --regid "$PGID" --clear-groups "$0" "$@"
    fi
fi

log "Wolfenstein 3-D ${WOLF_VERSION:-dev}"

mkdir -p "$STATE" 2>/dev/null || true
if ! ( : > "$STATE/.write-test" ) 2>/dev/null; then
    die "cannot write to $STATE as $(id -u):$(id -g). Make the volume writable,
or run with -e PUID=<owner uid> -e PGID=<owner gid>"
fi
rm -f "$STATE/.write-test"
export HOME="$STATE"
cd "$STATE"

#
# Which game, and so which engine. The chunk numbers of the graphics, sounds
# and music are compiled in and differ between games, so there is an engine
# for each:
#
#   wl6   *.WL6, *.WL3   the registered game, six or three episodes   wolf3d
#   wl1   *.WL1          the shareware episode, version 1.4           wolf3d-wl1
#   sod   *.SOD          Spear of Destiny                             wolf3d-sod
#   sd2   *.SD2          its first mission pack, Return to Danger     wolf3d-sod
#   sd3   *.SD3          its second, Ultimate Challenge               wolf3d-sod
#   sdm   *.SDM          Spear of Destiny's two-level demo            wolf3d-sdm
#
# Every game found at WOLF_DATAMOUNT is offered in the game's Settings, and
# the one chosen there is remembered in the state directory; until one is,
# the first in the order above is played. The shareware built into the
# image is always there too.
#
has() {     # has DIR EXTENSION: is there a VSWAP of that kind, in any case?
    [ -d "$1" ] || return 1
    find -L "$1" -maxdepth 1 -type f -iname "vswap.$2" 2>/dev/null | grep -q .
}

# The shareware is checked before it is offered, as the DOOM container checks
# its IWAD: a corrupt copy would otherwise fail somewhere deep in the engine
# with no hint that the data was the problem.
shareware_ok=0
if [ -f "$SHAREWARE/SHA256SUMS" ]; then
    ( cd "$SHAREWARE" && sha256sum -c --quiet SHA256SUMS ) >/dev/null 2>&1 \
        && shareware_ok=1 \
        || log "the shareware data in $SHAREWARE does not match its checksums"
elif has "$SHAREWARE" wl1; then
    shareware_ok=1
fi

GAMES=""
for kind in wl6 sod sd2 sd3 wl1 sdm; do
    if has "$DATAMOUNT" "$kind" || { [ "$kind" = wl6 ] && has "$DATAMOUNT" wl3; }; then
        GAMES="$GAMES $kind"
    fi
done
case " $GAMES " in
    *" wl1 "*) ;;
    *) [ "$shareware_ok" = 1 ] && GAMES="$GAMES wl1" ;;
esac
GAMES="${GAMES# }"
[ -n "$GAMES" ] || die "no game data: nothing at $DATAMOUNT, and no shareware in the image"

if [ -d "$DATAMOUNT" ] && [ -n "$(ls -A "$DATAMOUNT" 2>/dev/null)" ] \
   && [ "$GAMES" = "wl1" ] && ! has "$DATAMOUNT" wl1; then
    log "nothing at $DATAMOUNT looks like game data (no VSWAP.WL6, .WL3, .WL1,"
    log ".SOD, .SD2, .SD3 or .SDM). Playing the shareware built into the image."
fi

# What to play: WOLF_GAME if set, the one last chosen in the game, or the
# first there is
pick_game() {
    want="${WOLF_GAME:-}"
    [ -z "$want" ] && [ -f "$STATE/game" ] && want=$(cat "$STATE/game" 2>/dev/null)
    case " $GAMES " in
        *" $want "*) GAME="$want" ;;
        *) GAME="${GAMES%% *}" ;;
    esac

    case "$GAME" in
        wl6)         ENGINE="$ENGINES/wolf3d";     name="the registered game" ;;
        wl1)         ENGINE="$ENGINES/wolf3d-wl1"; name="the shareware episode" ;;
        sod|sd2|sd3) ENGINE="$ENGINES/wolf3d-sod"; name="Spear of Destiny" ;;
        sdm)         ENGINE="$ENGINES/wolf3d-sdm"; name="the Spear of Destiny demo" ;;
    esac
    case "$GAME" in
        sd2) export WOLF_MISSION=2; name="$name, Return to Danger" ;;
        sd3) export WOLF_MISSION=3; name="$name, Ultimate Challenge" ;;
        *)   unset WOLF_MISSION ;;
    esac

    if [ "$GAME" = wl1 ] && ! has "$DATAMOUNT" wl1; then
        DATA="$SHAREWARE"
    else
        DATA="$DATAMOUNT"
    fi

    [ -x "$ENGINE" ] || die "no engine at $ENGINE"
    unreadable=$(find -L "$DATA" -maxdepth 1 -type f ! -readable 2>/dev/null | head -3)
    if [ -n "$unreadable" ]; then
        die "cannot read the game data as $(id -u):$(id -g): $(echo $unreadable).
Make the files readable (chmod a+r), or set PUID and PGID to their owner."
    fi
    export WOLF_DATA="$DATA" WOLF_GAME_NOW="$GAME" WOLF_GAMES="$GAMES"
    log "playing $name from $DATA"
}

log "games here: $GAMES"
pick_game


#
# Where the sound goes: to the browser, unless a PulseAudio server has been
# handed in, in which case straight to it.
#
if [ "${WOLF_SOUND:-1}" = "0" ]; then
    export WOLF_AUDIO=none
    log "sound: off (WOLF_SOUND=0)"
elif [ -n "${PULSE_SERVER:-}" ]; then
    export WOLF_AUDIO=pulse
    log "sound: to the PulseAudio server at $PULSE_SERVER"
else
    export WOLF_AUDIO="tcp:$AUDIO_PORT"
fi
export WOLF_PAD_PORT="$PAD_PORT"
export WOLF_SCALE="$SCALE"
# The page stretches the picture to 4:3, so the engine draws square pixels
export WOLF_PAGE_ASPECT=1
# The page captures the mouse; the engine must not chase the pointer too
export WOLF_NOGRAB=1

#
# The display. 24-bit: the engine draws in truecolour from its palette.
# As big as the largest resolution in the game's Settings (6x); x11vnc sends
# only the part the engine's window covers, which the engine tells it as
# the resolution is changed (sys_x11.c, TellVNC).
#
WIDTH=$((320 * SCALE))
HEIGHT=$((200 * SCALE))
SCREEN_WIDTH=1920
SCREEN_HEIGHT=1200
export DISPLAY="$DISP"
rm -f "/tmp/.X${DISP#:}-lock" "/tmp/.X11-unix/X${DISP#:}" 2>/dev/null || true

log "starting Xvfb on $DISP at ${SCREEN_WIDTH}x${SCREEN_HEIGHT}; the picture starts at ${WIDTH}x${HEIGHT}"
Xvfb "$DISP" -screen 0 "${SCREEN_WIDTH}x${SCREEN_HEIGHT}x24" -nolisten tcp >"$STATE/xvfb.log" 2>&1 &
XVFB_PID=$!

i=0
while [ ! -e "/tmp/.X11-unix/X${DISP#:}" ]; do
    i=$((i + 1))
    [ "$i" -gt 100 ] && die "Xvfb failed to start; see $STATE/xvfb.log"
    kill -0 "$XVFB_PID" 2>/dev/null || die "Xvfb exited; see $STATE/xvfb.log"
    sleep 0.1
done

vnc_auth="-nopw"
if [ -n "${WOLF_VNC_PASSWORD:-}" ]; then
    x11vnc -storepasswd "$WOLF_VNC_PASSWORD" "$STATE/.vncpasswd" >/dev/null 2>&1
    vnc_auth="-rfbauth $STATE/.vncpasswd"
fi

#
# -nowireframe and -noscrollcopyrect: x11vnc otherwise watches for a window
# being dragged or a pane scrolled while a mouse button is held, and holds the
# picture back while it decides. A shooter holds the fire button down, and
# the picture stopped for 300 ms at a time whenever it was. -wait and -defer
# at 3 ms keep the delay between the engine drawing a frame and x11vnc
# sending it to a frame or so.
#
log "starting x11vnc on port $VNC_PORT"
# shellcheck disable=SC2086
x11vnc -display "$DISP" -rfbport "$VNC_PORT" -forever -shared -quiet \
       -nowireframe -noscrollcopyrect -nonap -clip "${WIDTH}x${HEIGHT}+0+0" \
       -wait "${WOLF_VNC_WAIT:-3}" -defer "${WOLF_VNC_DEFER:-3}" \
       ${WOLF_VNC_ARGS:-} \
       $vnc_auth >"$STATE/x11vnc.log" 2>&1 &
VNC_PID=$!

#
# One WebSocket port, three streams behind it: the picture, the sound and the
# controller. A connection asking for none of them gets the picture, so stock
# noVNC's vnc.html still works.
#
{
    printf 'vnc: localhost:%s\n' "$VNC_PORT"
    [ "$WOLF_AUDIO" = "tcp:$AUDIO_PORT" ] && printf 'audio: localhost:%s\n' "$AUDIO_PORT"
    printf 'pad: localhost:%s\n' "$PAD_PORT"
} > "$STATE/ws-targets"

log "starting noVNC on port $WEB_PORT"
"$WSPROXY_BIN" --web="$NOVNC_ROOT" \
       --token-plugin=websockify.token_plugins.ReadOnlyTokenFile \
       --token-source="$STATE/ws-targets" \
       "$WEB_PORT" 2>&1 |
    while IFS= read -r wsline; do
        case "$wsline" in
            "picture gap"*|"controller:"*|"sound:"*) log "$wsline" ;;
            *) printf '%s\n' "$wsline" >>"$STATE/websockify.log" ;;
        esac
    done &
WEB_PID=$!

log ""
log "  play at  http://localhost:$WEB_PORT/  (or /play.html)"
log ""

SHUTTING_DOWN=0
WOLF_PID=""

shutdown() {
    SHUTTING_DOWN=1
    [ -n "$WOLF_PID" ] && kill "$WOLF_PID" 2>/dev/null || true
    for pid in "$WEB_PID" "$VNC_PID" "$XVFB_PID"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap shutdown TERM INT

#
# Run the engine, and put it back on its feet if it falls over or quits.
#
# The display, the VNC server and the proxy all outlive the engine, so only
# the engine has to come back, and the browser reconnects to the same session
# on its own -- where a crash or Quit used to leave a container with nothing in
# it and a page that could never reconnect. Three runs in a row that end
# within seconds are a container that cannot run, and that stops with the
# reason above it rather than burying it under an endless loop.
#
crash_runs=0
status=0

while :; do
    log "running: $(basename "$ENGINE") $*"
    started=$(date +%s)

    "$ENGINE" "$@" &
    WOLF_PID=$!

    status=0
    wait "$WOLF_PID" || status=$?
    WOLF_PID=""

    case "$status" in
        0)   log "the game was quit"; why=quit ;;
        3)   log "a different game was chosen in the settings"; why=switch ;;
        132) log "the engine hit an illegal instruction (SIGILL)"; why=crash ;;
        134) log "the engine was aborted (SIGABRT)"; why=crash ;;
        136) log "the engine hit an arithmetic error (SIGFPE)"; why=crash ;;
        139) log "the engine crashed (SIGSEGV)"; why=crash ;;
        143) log "the engine was stopped (SIGTERM)"; why="" ;;
        *)   log "the engine exited with status $status"; why="" ;;
    esac

    [ -n "$why" ] || break
    [ "$SHUTTING_DOWN" = "0" ] || break

    # The choice made in the game wins over WOLF_GAME from now on
    if [ "$why" = "switch" ]; then
        WOLF_GAME=""
        pick_game
        continue
    fi

    [ "$RESTART" = "1" ] || break

    now=$(date +%s)
    if [ "$((now - started))" -lt 10 ]; then
        crash_runs=$((crash_runs + 1))
    else
        crash_runs=0
    fi
    if [ "$crash_runs" -ge 3 ]; then
        log "three runs in a row ended within seconds of starting, which is"
        log "not something restarting will fix. Stopping."
        break
    fi

    if [ "$why" = "quit" ]; then
        log "starting the game again: stopping the container is what stops it"
    else
        log "restarting the engine; the browser reconnects on its own, and the"
        log "game starts again at the title screen"
    fi
    log "set WOLF_RESTART=0 to have the container stop instead"
    sleep 1
done

shutdown
exit "$status"
