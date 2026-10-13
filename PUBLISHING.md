# Publishing

How the images and releases are published, and how the Unraid template gets
listed. For maintainers; playing needs none of it (see [DOCKER.md](DOCKER.md)).

## Cutting a release

Open a pull request that renames `CHANGELOG.md`'s `[Unreleased]` section to
`[X.Y.Z] — YYYY-MM-DD`, and merge it. CI sees the version change and, in the
same run, publishes the images, pushes the `vX.Y.Z` tag, builds the desktop
tarball and creates the GitHub Release with that CHANGELOG section as its
text. Nothing to push from your machine.

A release that adds something moves the minor number and resets the patch;
one that only fixes moves the patch.

## Tagging

Defined in `.github/workflows/docker-publish.yml`, as in the DOOM container:

| Trigger | Images | Git tag | GitHub Release |
|---|---|---|---|
| Merge to `master`, CHANGELOG top `[Unreleased]` | `edge` | — | — |
| Merge to `master`, CHANGELOG top `[X.Y.Z] — DATE` | `edge`, `X.Y.Z`, `X.Y`, `X`, `latest` | `vX.Y.Z` | yes |
| `git push origin vX.Y.Z` | `X.Y.Z`, `X.Y`, `X`, `latest` | (pushed) | yes |
| Nightly | rebuilds, for base-image updates | — | — |

`claude/**`, `fix/**` and `feat/**` branches are built too and tagged with
their branch name, so there is an image to try before anything is merged.

## Registry

GHCR only, at `ghcr.io/krippler/wolf3d`, authenticated with the workflow's
own `GITHUB_TOKEN`: there are no secrets to set up. Images are signed with
cosign. The package inherits the repository's visibility; check once after
the first run that it can be pulled without credentials:

```bash
tok=$(curl -s "https://ghcr.io/token?scope=repository:krippler/wolf3d:pull&service=ghcr.io" \
      | sed -nE 's/.*"token":"([^"]+)".*/\1/p')
curl -s -o /dev/null -w '%{http_code}\n' -H "Authorization: Bearer $tok" \
     https://ghcr.io/v2/krippler/wolf3d/tags/list      # 200 is public
```

## Unraid Community Applications

`templates/unraid.xml` is the template, `templates/wolf3d-icon.png` the icon
and `ca_profile.xml` the repository's card. Submit the repository through
Community Applications' own form once a release is published, so `latest`
exists for the template to pull.
