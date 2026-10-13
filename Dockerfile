# Wolfenstein 3-D in a container, playable in a browser.
#
# The engine draws into an X server of the container's own (Xvfb), which is
# exported over VNC and noVNC, and its sound goes to the page over the same
# port. Nothing is needed on the host.
#
#   docker build -t wolf3d .
#   docker run --rm -p 6080:6080 wolf3d
#   # then open http://localhost:6080/
#
# See DOCKER.md for the full set of options.

##############################################################################
# Build stage: compile the four engines.
##############################################################################
FROM ubuntu:24.04 AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        gcc \
        libc6-dev \
        make \
        libx11-dev \
        libxext-dev \
        libpulse-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY WOLFSRC/ ./WOLFSRC/

# wolf3d plays the registered game, wolf3d-wl1 the shareware episode,
# wolf3d-sod Spear of Destiny and its mission packs, wolf3d-sdm Spear's demo:
# the chunk numbers are compiled in and differ between them all.
RUN make -C WOLFSRC -j"$(nproc)" \
 && strip WOLFSRC/linux/wolf3d WOLFSRC/linux/wolf3d-wl1 \
          WOLFSRC/linux/wolf3d-sod WOLFSRC/linux/wolf3d-sdm

##############################################################################
# Runtime stage: the engines plus an X server, VNC and a web client.
##############################################################################
FROM ubuntu:24.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        libx11-6 \
        libxext6 \
        libpulse0 \
        xvfb \
        x11vnc \
        websockify \
        tini \
# noVNC is static HTML and JavaScript served to the browser, but the distro
# package depends on Node and net-tools for tooling this image never runs.
# Unpack just the files instead; websockify above is what serves them.
 && apt-get download novnc \
 && dpkg-deb -x novnc_*.deb / \
 && rm -f novnc_*.deb \
 && apt-get autoremove -y --purge \
# Xvfb is linked against libGL.so.1 so the dispatch library has to stay, but
# nothing here renders through GLX and the Mesa driver behind it is most of
# the image. Drop the driver, keep the dispatch.
 && dpkg --remove --force-depends \
        libglx-mesa0 mesa-libgallium libllvm20 libgl1-mesa-dri 2>/dev/null || true \
# websockify only uses numpy to unmask what the browser sends, which is the
# keys and the mouse; it warns and carries on without it.
 && dpkg --remove --force-depends \
        python3-numpy liblapack3 libblas3 libgfortran5 2>/dev/null || true \
 && rm -rf /var/lib/apt/lists/* /usr/share/doc/* /usr/share/man/*

COPY --from=build /src/WOLFSRC/linux/wolf3d /usr/local/games/wolf3d
COPY --from=build /src/WOLFSRC/linux/wolf3d-wl1 /usr/local/games/wolf3d-wl1
COPY --from=build /src/WOLFSRC/linux/wolf3d-sod /usr/local/games/wolf3d-sod
COPY --from=build /src/WOLFSRC/linux/wolf3d-sdm /usr/local/games/wolf3d-sdm
COPY docker/entrypoint.sh /usr/local/bin/wolf-entrypoint

# websockify, taught to carry the sound and the controller beside the picture
COPY docker/wolf-wsproxy.py /usr/local/bin/wolf-wsproxy

# Which build this is, stamped into the page, so a report of "no sound" can
# be told apart from a browser running a client from two releases ago.
ARG WOLF_VERSION=dev

# A client that captures the mouse and plays the sound
COPY docker/play.html /usr/share/novnc/play.html
COPY docker/wolf-ring.js /usr/share/novnc/wolf-ring.js
COPY docker/wolf-audio.js /usr/share/novnc/wolf-audio.js
COPY docker/wolf-gamepad.js /usr/share/novnc/wolf-gamepad.js
COPY docker/index.html /usr/share/novnc/index.html

# Not sed: a branch name with a slash in it ends an s/// early. The value is
# narrowed to characters that cannot escape the HTML or the JavaScript string
# it lands in.
RUN python3 -c 'import os,re,pathlib; p=pathlib.Path("/usr/share/novnc/play.html"); v=re.sub(r"[^A-Za-z0-9._+-]","-",os.environ.get("WOLF_VERSION") or "dev"); s=p.read_text().replace("__WOLF_VERSION__",v); p.write_text(s); assert "__WOLF_VERSION__" not in s, "version placeholder left in the client"'

# The shareware episode, every file of it as Apogee's terms ask, so the
# container plays with nothing mounted. A mounted game wins over it.
COPY shareware/ /usr/share/wolf3d/shareware/

RUN chmod +x /usr/local/bin/wolf-entrypoint /usr/local/bin/wolf-wsproxy \
 && mkdir -p /data /wolf/state \
 && useradd --create-home --home-dir /wolf/state --uid 1001 wolf \
 && chown -R wolf:wolf /wolf

ENV WOLF_VERSION=${WOLF_VERSION} \
    WOLF_SCALE=2 \
    WOLF_AUDIO_RATE=22050 \
    WOLF_STATE=/wolf/state \
    WOLF_DATAMOUNT=/data \
    WOLF_VNC_PORT=5900 \
    WOLF_AUDIO_PORT=5901 \
    WOLF_PAD_PORT=5902 \
    WOLF_WEB_PORT=6080 \
    WOLF_DISPLAY=:99 \
    HOME=/wolf/state

VOLUME ["/wolf/state"]
EXPOSE 6080 5900

# No USER: the entrypoint starts as root only long enough to take ownership
# of the state directory as PUID:PGID, then drops to that user.
WORKDIR /wolf/state

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/wolf-entrypoint"]
