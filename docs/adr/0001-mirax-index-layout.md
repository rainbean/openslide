# 1. Locate MIRAX records with the hierarchy layout

Status: accepted (2026-11-20)

## Context

A MIRAX slide's `Slidedat.ini` lists `HIER_COUNT` hierarchies, each with a
name and a value count: `Slide zoom level` (the pyramid), `Microscope focus
level` (focal planes), and others such as `Slide filter level`. The index file
holds a table of pointers to image records, one record per combination of
hierarchy values.

Upstream reads zoom level *i* at `table + 4·i` and refuses a slide whose zoom
hierarchy is not `HIER_0`. That ignores every other hierarchy. The fork exposes
focal planes as levels, so it must also find the record of each focal plane.
Its first attempt stepped through the table by a fixed number of bytes per
plane: 108 when `CURRENT_SLIDE_VERSION >= 2.5`, otherwise 240. These were
measured by hand on two scanner families. Version 1.9 slides, which have no
focus hierarchy, could not open at all.

## The layout

The table is a mixed-radix array with `HIER_0` varying fastest:

```
record(v₀, …, vₙ₋₁) = Σₕ vₕ · strideₕ      strideₕ = Π_{g<h} count_g
pointer             = table + 4 · record
```

Dumping the table of every available slide (`misc/mirax-index-layout.py`)
shows images only at records this formula predicts:

| Scanner family | Version | Hierarchies, in order (count) | Focus stride |
|---|---|---|---|
| Scanner family A, 12-plane and 6-plane z-stacks | 2.5 | zoom 9, filter 3, focus, scan info 2 | 27 records = 108 B |
| Scanner family A, single plane | 2.2 | zoom 9, filter 3, focus 1, scan info 2 | n/a |
| Scanner family B, 4-plane and 22-plane z-stacks | 2.3 | zoom 10, zoom mask 2, filter 3, focus | 60 records = 240 B |
| Scanner family B, single plane (200+ slides) | 2.3 | zoom 10, zoom mask 2, filter 3, focus 1 | n/a |
| Upstream test slides | 1.9 | zoom, filter 3, scan info 2 (no focus) | n/a |
| Upstream test slides | 2.2, 2.3 | zoom, filter, focus 1, … | n/a |

Both hand-measured strides are this formula with every other value at 0. The
version check only happened to line up with whether a slide carries the
`Slide zoom mask level2` hierarchy. A slide of either version with the other
hierarchy set would have read the wrong records.

What the records hold:

- **Zoom**: one pyramid level per value; value 0 is full resolution.
- **Focus**: value 0 is `ExtFocusLevel` (the extended-focus image). Values
  1..N are `ZStackLevel_(±n)`, and each one's section gives
  `OFFSET_IN_MICROMETERS`. Every focus value has its own full pyramid.
- **Zoom mask** (value 1): a second full pyramid, a tissue mask.
- **Filter** values above 0 and the **scan info** layer: not image pyramids.

## Decision

1. Parse every hierarchy's name and count, and compute strides from the
   counts. Find `Slide zoom level` (required) and `Microscope focus level`
   (optional: a slide without it has one plane) by name, at any position.
   Do not read `CURRENT_SLIDE_VERSION`.
2. Hold every other hierarchy at value 0, its default: filter 0, the image
   rather than the mask, and the default scan-info layer. A hierarchy that a
   future scanner adds is skipped the same way.
3. Expose one level per focal plane, each read from that plane's zoom-0
   record, in focus-value order (extended focus first, then the z-stack from
   the most negative offset upward).
4. For the quickhash, hash the images of the smallest zoom level of focus
   plane 0, without building tiles. These are the same bytes upstream hashes,
   so upstream quickhashes still match, and opening a slide never hashes a
   full-resolution plane.
5. Report each plane as properties: `mirax.level[N].name` (the focus value
   name, such as `ZStackLevel_(-1)`) and `mirax.level[N].focus-offset-um`.
   They are set only when the slide has a focus hierarchy.

## Consequences

- Version 1.9 slides open again, and single-plane slides open without hashing
  their whole base level.
- A slide whose hierarchy counts point past the end of the index file fails
  the open with a clear error.
- The zoom-mask pyramid and the scan-info records are not exposed. Exposing
  them later is a new value pinned in step 2, not a new stride.
