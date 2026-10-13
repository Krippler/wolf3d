# Shareware game data

Wolfenstein 3-D version 1.4, episode 1 ("Escape from Wolfenstein"), ten
levels: every file of Apogee's shareware release, unmodified. It is baked
into the container image so the game plays with nothing mounted.

The files are the contents of `1WOLF14.ZIP` as Apogee distributed it, taken
out of the installer's archive (`W3D1_BBS._1`, which is a plain zip behind a
short header):

```
1wolf14.zip  sha256 309eaa5dffd8d00452b5002ff991768bd7f097ab665227571e3b5f95fb61302d
```

and `SHA256SUMS` lists each file's checksum. The entrypoint checks them at
startup and refuses to use the data if any is wrong.

## Why it is committed rather than downloaded

As in the DOOM container: a build-time download would be a URL that has to
stay up for as long as the image is built, the nightly rebuild included. The
files come to 1.4 MB and never change, and committing them makes the build
hermetic.

## Licence

These files are **not** covered by anything else in this repository. They
are Apogee's shareware, and [`VENDOR.DOC`](VENDOR.DOC) is the licence they
are distributed under. In short: anyone may copy and pass the shareware
episode around free of charge, as long as all of its files go with it,
unmodified -- which is why all of them are here, `WOLF3D.EXE`, the catalogue
and the order form included, though only the `.WL1` files are used -- and it
is not sold.

Nothing else is included. `*.WL6`, `*.WL3` and Spear of Destiny are
commercial data and come from your own copy of the game: mount them at
`/data` and they take precedence over these.

The ignore rules in `.gitignore` and `.dockerignore` exclude every game data
file except these, so a registered game sitting in the working tree cannot be
committed or baked into an image by accident.
