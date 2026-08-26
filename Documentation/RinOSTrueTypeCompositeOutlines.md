# RinOS TrueType composite outline v1

The RinOS `TypefaceTrueTypeRinOS` glyph path reader retains decoded contours
until an entire composite glyph has been resolved. This is required for the
TrueType component form where a later component names one of its own points and
one point from a preceding component: the two are aligned in design space before
the component is appended. A command-only reader cannot recover those point
indices and must not substitute a PSF glyph or invent a zero translation.

Simple glyph points are transformed into bounded floating design coordinates,
then each composite component is resolved recursively. XY arguments support the
documented scale/unscaled-offset flags and optional grid rounding. Point-match
arguments use unsigned point indices; missing predecessor points, missing child
points, unknown or conflicting transform flags, non-finite transforms, overflow,
and the global point/component/depth limits reject the outline before it is
returned to the path consumer.

The focused LibGfx regression constructs a minimal SFNT whose second composite
component attaches point 0 to the first component's point 1. The resulting
contours run from `(0, 0)` to `(100, 0)` and then `(100, 0)` to `(200, 0)`.
It also verifies that an out-of-range component anchor returns no outline. The
RinOS source is syntax-checked with the existing x86_64 and i386 Ladybird
compile commands. CFF/OTTO, WOFF2, variable/color-font tables, complex shaping,
antialiasing, and product/QEMU browser evidence remain separate P1.4 work.
