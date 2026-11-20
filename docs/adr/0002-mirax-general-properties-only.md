# 2. Export only the GENERAL group of a MIRAX slide's properties

Status: accepted (2026-11-20)

## Context

Upstream copies every key of every `Slidedat.ini` group into a property
named `mirax.<GROUP>.<KEY>`. Most groups describe the file layout rather than
the slide: `HIERARCHICAL` (hierarchy names, values and section links),
`DATAFILE` (the data file names), and one section per layer, level and
non-hierarchical value. A real 40x slide carries about 500 such properties;
slides of other formats carry 40 to 100 in all.

libvips attaches each OpenSlide property to the image as a metadata field.
`dzsave` creates an image per output tile and copies every field into it,
under a global lock, and the cost of each copy grows with the field count.
Converting one single-plane MIRAX slide with `vips dzsave` (webp tiles):

| Properties exported | Wall | CPU |
|---|---|---|
| every group (505 fields) | 256 s | 241% |
| `GENERAL` and `openslide.*` only (74 fields) | 42 s | 868% |
| same pixels, from a tiled TIFF (17 fields) | 33 s | 1020% |

Stack samples showed the threads waiting on that lock. Reading every tile of
the slide through OpenSlide alone takes 2 s on 16 threads, so the MIRAX
reader is not the bottleneck. Both runs produced byte-identical tiles; only
`dzsave`'s `vips-properties.xml` dump differed.

## Decision

Export only the `GENERAL` group as `mirax.GENERAL.*`, plus the fork's own
`mirax.level[N].*` properties (ADR 0001). The driver still reads the other
groups to open the slide; it no longer republishes them.

`GENERAL` holds what identifies and describes the slide: its ID, name,
scanner software and hardware, objective, and tile counts. The standard
`openslide.*` properties (MPP, objective power, background colour, bounds)
are computed from the parsed slide, never read back from the exported
properties, so they are unchanged.

## Consequences

- Converting a MIRAX slide with libvips is several times faster and uses
  the machine's cores. A z-stack slide gains once per plane.
- `slidetool prop list` and `openslide_get_property_names()` no longer show
  the layout groups. A caller that needs them reads `Slidedat.ini`.
- This diverges from upstream. Upstream test cases that assert a layout
  property expect it to be missing, with a `# fork-local:` comment.
