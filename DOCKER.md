# The container

```
docker run --rm -p 6080:6080 \
  -v "$PWD/data:/data:ro" -v wolf3d-state:/wolf/state \
  ghcr.io/krippler/wolf3d
```

Open `http://HOST:6080/` and click **Play fullscreen** or **Play in this
window**. Plain noVNC is at `/vnc.html`, for a viewer that does not capture
the mouse; it carries no sound.

## What runs inside

| | |
|---|---|
| Xvfb | an X server of the container's own, sized to the picture |
| x11vnc | exports it over VNC, on 5900 |
| wolf-wsproxy | websockify serving noVNC on 6080, and carrying the picture, the sound and the controller there on one WebSocket port |
| wolf3d | the engine, which draws in Xvfb and serves its own sound |

The entrypoint restarts the engine if it is quit or crashes, so the page can
always reconnect; the display and the proxy stay up.

## Volumes

| Path | |
|---|---|
| `/data` | Optional, read-only. Your own copy of the game's files: `VSWAP.WL6` and the rest for six episodes, `.WL3` for three, `.SOD` for Spear of Destiny, `.SD2` and `.SD3` for its mission packs, `.SDM` for its demo. Several can share it. With nothing here, the shareware episode built into the image plays. |
| `/wolf/state` | Savegames (`SAVEGAM?.WL6`), the game's options and high scores (`CONFIG.WL6`), the Settings pages' `settings`, the game last chosen (`game`), and the logs of everything above. Keep it. |

A configuration file or savegame from the DOS game is not read: the
engine's are a different shape, and it says so rather than loading nonsense.

## Environment

| Variable | Default | |
|---|---|---|
| `PUID`, `PGID` | `1001` | Who the game runs as. The state directory is made theirs. 99 and 100 on Unraid. |
| `WOLF_GAME` | | Which game to play when `/data` holds several: `wl6`, `sod`, `sd2`, `sd3`, `sdm` or `wl1`. Without it, the one last chosen under Settings → Game, or the first. |
| `WOLF_SCALE` | `2` | The picture is 320x200 times this. The page scales it to the window either way; larger costs bandwidth. |
| `WOLF_ASPECT` | `4:3` | `square` draws square pixels rather than the 4:3 picture DOS showed. Settings → Picture changes it in the game, and from then on what was chosen there wins. |
| `WOLF_SOUND` | `1` | `0` for no sound at all, saving the stream's bandwidth. |
| `WOLF_AUDIO_RATE` | `22050` | The sound's rate, in Hz. About 700 kbit/s at the default; 11025 halves it, 44100 doubles it. |
| `PULSE_SERVER` | | A PulseAudio server to play the sound on instead of the browser. |
| `WOLF_VNC_PASSWORD` | | A password for the page and any VNC client. |
| `WOLF_RESTART` | `1` | `0` stops the container when the game is quit or crashes. |
| `WOLF_VNC_ARGS` | | More options for x11vnc. |

## Ports

| Port | |
|---|---|
| 6080 | The page, the picture, the sound and the controller. The only one you need. |
| 5900 | VNC, for a native viewer. Not needed for the browser. |

## Controls in the browser

The page captures the mouse, so the browser keeps <kbd>Esc</kbd>: it gives
the mouse back and brings up the start screen. The game's menu, which is on
Esc on DOS, is on <kbd>`</kbd> beside it. The rest are the game's own: arrow
keys move, Ctrl fires, Space opens, Alt strafes, Shift runs, 1 to 4 change
weapon, F1 to F10 are what the help screen says, and F12 opens the port's
Settings (also at the bottom of the Control menu, for a browser that keeps
F12 for itself).

A game controller is read by the browser and passed to the engine, which
plays it as the keyboard, so the menus work with it too:

| | |
|---|---|
| Left stick, d-pad | walk and turn |
| Right stick | turn |
| A, right trigger | fire, or choose in a menu |
| B, X | open a door, or choose |
| Y | next weapon |
| Left bumper | strafe |
| Right bumper, left trigger, left stick click | run |
| Menu, View | the menu, and back out of it |

It vibrates when you are hit, where the browser can drive the pad's motors.

## Logs

The container's log has what each part is doing; each part's own chatter is
in the state directory (`xvfb.log`, `x11vnc.log`, `websockify.log`). A crash
is named in the log, and the engine is started again.
