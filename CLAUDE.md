# CLAUDE.md

OpenSlide is a C library (LGPL-2.1) that reads whole-slide images from many vendor
formats behind one API (`src/openslide.h`), shipped as `libopenslide` plus the
`slidetool` CLI.

## This is a fork

This checkout is the `rainbean/openslide` fork (`origin`), developed on `devel`. It
diverges from upstream (`upstream` remote) on purpose. Run
`git log upstream/main..devel` to see every fork-local commit.

- **Full-resolution levels only.** Each driver exposes only base-size levels, and
  `openslide.c` fails the open on any level whose `downsample != 1.0`. A plain slide
  reports `openslide.level-count: 1`. Upstream pyramid behaviour no longer applies.
- **Z-stack = levels.** Each focal plane is its own level, all at downsample 1.0
  (a 21-plane Aperio slide has 21 levels). The fork has no stack-count property.
- **Rebasing onto upstream:** any driver the fork has not adapted (new or changed
  pyramid code) trips the downsample check. Adapt it to expose base-size levels.
- **Fork-local test expectations** carry a `# fork-local:` comment in their
  `test/cases/*/config.yaml`, with the reason.
- **Scope:** push only to `origin`. Issues are off on the fork; the issue tracker
  is recorded in local memory. Releases are date tags (`2026-11-18`), not `vX.Y`.
  The fork runs no CI: leave `.github/workflows` untouched and drop CI commits
  when rebasing.

## Build and test in the container

The host lacks the build deps. Build and test inside the `openslide` image
(`docker build -t openslide docker/`), running as root: with `-u $(id -u)`,
meson's wrap handling fails with a PermissionError. Build into the container's
`/build` so the repo stays clean, and keep the slide cache and pre-commit hook
environments in the repo's ignored `build/` so they survive `--rm`:

```bash
docker run --rm -v "$PWD":/mnt -w /mnt \
  -e OPENSLIDE_TEST_CACHE=/mnt/build/_slidedata \
  -e PRE_COMMIT_HOME=/mnt/build/pre-commit openslide bash -c '
  meson setup /build -Dtest=enabled && ninja -C /build &&
  cd /build/test && ./driver unpack nonfrozen && ./driver run "dicom*"'
```

The Dockerfile points apt at `tw.archive.ubuntu.com`, because
`archive.ubuntu.com` is unusably slow from the dev host. Elsewhere, pass
`--build-arg UBUNTU_MIRROR=<url>`. The image also carries `pre-commit`; run
`pre-commit run -a` inside the container, not on the host.

The suite is the generated Python `driver` in `<builddir>/test/`. There are no
`meson test` targets. Run `./driver` with no arguments for its subcommands
(`run`, `sanitize`, `valgrind`, `exports`, …). Narrow `run` and `unpack` with a
pattern. The first full `unpack` downloads about 30 GB of slides (2.5 hours
here); once cached, the full `run` takes about a minute. Every failure left in
the full suite is a MIRAX case. To check one file, run
`/build/tools/slidetool prop list <slide>` with the slide mounted read-only.

## Coding rules

`pre-commit run -a` is the gate. Run it before declaring C changes done. Its local
hooks enforce the C conventions. The tables in `.pre-commit-config.yaml` are the
source of truth for every replacement:

- Allocate and free with glib (`g_malloc`, `g_new0`, `g_free`, `g_strdup`).
- In `src/`, call the `_openslide_*` wrappers for parsing
  (`_openslide_parse_int64`, `_openslide_parse_double`), file I/O
  (`_openslide_fopen`, `_openslide_fread`), TIFF (`_openslide_tiffcache_get`), and
  sqlite. The hooks also block `g_ascii_strto*` there.
- Use `int64_t` for offsets and sizes, and `OPENSLIDE_G_DESTROY_NOTIFY_WRAPPER()`
  for destroy callbacks.
- Give every `g_auto*` declaration an initializer (`g_autofree char *p = NULL;`).
- The wrapper implementations themselves mark the line `// ci-allow`.

The repo has no clang-format. Match the surrounding C style by hand.

## Architecture

- **Format dispatch:** `formats[]` in `src/openslide.c` is ordered from most
  specific to least. `detect_format()` calls each driver's `detect()` in turn,
  then calls `open()` on the first match. `generic-tiff` is the last fallback.
  Each driver is `src/openslide-vendor-<name>.c`, which fills `openslide_t`
  (`struct _openslide` in `openslide-private.h`): `levels[]`, `properties`,
  `associated_images`, and `ops` (`paint_region`, `read_icc_profile`, `destroy`).
- **Grids** (`openslide-grid.c`: simple, tilemap, range) map a region paint
  to the driver's tile-read callback. Tiles go through the shared LRU cache
  (`openslide-cache.c`).
- **Decoders** (`src/openslide-decode-*.c`) wrap jpeg, jp2k, png, bmp, tiff,
  xml, sqlite, and dicom. Drivers call these rather than the libraries.
- **New format:** write the driver, add its `extern` and its `formats[]` slot in
  `openslide.c`, add it to `src/meson.build`, and add cases under `test/cases/`.
  Expose only full-resolution levels, as described in the fork section.
