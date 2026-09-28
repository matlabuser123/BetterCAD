# P15-PROV-001 — the VOID first qualification attempt

Kept because a failed gate is evidence, not something to tidy away.

## What happened

```text
debug-ext          configure 0  clean 0  build 0  rebuild 0  ctest 0
release-ext        configure 0  clean 0  build 0  rebuild 0  ctest 0
debug-shared-ext   configure 0  clean 0  build 1  <-- FAILED
repeat release-ext exit 0
repeat debug-ext   exit 0
qualification finished 10:47:07, 1 stage(s) failed
```

```text
Materials.cpp:733: undefined reference to
    bettercad::materials::PropertyProvenance::empty() const
Materials.cpp:752: undefined reference to
    bettercad::materials::PropertyProvenance::empty() const
collect2.exe: error: ld returned 1 exit status
```

## Root cause

`PropertyProvenance::empty()` and `MaterialProvenance::empty()` were declared in
`Provenance.hpp` and defined out of line in `Provenance.cpp`, on structs carrying **no
export macro**. The build uses `-fvisibility=hidden`, so both definitions were hidden
and unresolvable from `bettercad_features` across the DLL boundary.

**Debug and Release could not have caught it.** They are static: everything lands in
one archive and visibility never comes into play. Only a shared build exercises the
export boundary, which is exactly why `debug-shared-ext` is in the preset list — and
this is the second time in P15 that it has caught a DLL-boundary defect the other two
presets passed (P15-MAT-001 hit the dll-imported `constexpr static` problem).

## The fix, and why it is this one

The convention was already there and I had broken it. Every data struct in
`core/materials/` — `MechanicalProperties`, `ThermalProperties`, `MaterialLibraryKey`,
`LinearElasticConstants` — carries no export macro and defines no member out of line.
The module's only out-of-line member is `LibraryMaterial::key()`, and `LibraryMaterial`
is a `class BETTERCAD_CORE_EXPORT`.

So both `empty()` methods became **inline in the header**, which restores the
convention and adds no export surface. Exporting the structs instead would have worked
and would have been the larger change, widening the ABI for two trivial predicates.

Audited rather than patched blind: every out-of-line definition in both new
translation units was checked against its declaration. `Provenance.cpp` had exactly
these two; its only remaining out-of-line member is `Date::of`, and `Date` IS exported.
`Completeness.cpp` defines no members at all — every function in it is free and
exported.

## Consequence

The first qualification is **VOID**: source changed after the freeze. The whole thing
was re-run from clean across all three presets, not merely the preset that failed.

## Possible future guard

An architecture test could reject an out-of-line member definition on an unexported
type in `core`, the way `architecture.layering` rejects a layering violation. Not built
here -- it is new infrastructure beyond this milestone, and `debug-shared-ext` already
catches the class -- but it would catch it at configure time instead of twenty minutes
into a shared build.
