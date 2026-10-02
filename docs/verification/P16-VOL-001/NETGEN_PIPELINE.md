# P16-VOL-001 — the Netgen pipeline

```text
SUBJECT:  what crosses the backend seam, in which direction, and what is
          checked on each side of it
RESULT:   nglib appears in exactly one file. Its return code is never trusted.
DATE:     2026-10-02
```

## The call sequence

Inside `src/meshing/netgen/NetgenBackend.cpp`, and nowhere else:

```text
Ng_Init                       under a mutex, per generation (see below)
Ng_NewMesh
  Ng_AddPoint          x N    SI metres, 1-based numbering begins here
  Ng_AddSurfaceElement x M    NG_TRIG, indices + 1
Ng_GenerateVolumeMesh         with pinned Ng_Meshing_Parameters
Ng_GetNP / Ng_GetNE           CHECKED, because NG_OK is not evidence
  Ng_GetPoint          x NP
  Ng_GetVolumeElement  x NE   type checked == NG_TET, indices - 1
Ng_DeleteMesh                 RAII
Ng_Exit                       RAII
```

Both the mesh and the library are owned by scope guards, so no return path —
including the eight failure returns — leaks an `Ng_Mesh` or leaves the library
initialised.

## NG_OK is not evidence of success

The single most important line in the adapter:

```cpp
const int nodeCount = nglib::Ng_GetNP(mesh.get());
const int elementCount = nglib::Ng_GetNE(mesh.get());
if (elementCount <= 0 || nodeCount <= 0) {
    return failure(VolumeBackendFailure::NoTetrahedra, ...);
}
```

`INFRA-NETGEN-001` established that nglib **returns `NG_OK` with zero
tetrahedra** when it gives up on a surface, having printed its own complaint to
stdout. So the acceptance condition is not the return code but all of:

```text
status == NG_OK
+ node count > 0
+ element count > 0
+ every element is NG_TET, not NG_TET10/PYRAMID/PRISM
+ every element index within 1..nodeCount
+ every returned coordinate finite
```

and then, in the layer above and over a real `Mesh`:

```text
+ validate() passes          connectivity, degeneracy, orientation, duplicates
+ boundary conforms          computed from the tetrahedra, not from the backend
+ volume recovered           against the boundary's own enclosed volume
```

`NoTetrahedra` is a distinct failure rather than being folded into
`GenerationFailed`, because it is the documented behaviour of the admitted
backend rather than a hypothetical, and a reader of the diagnostic should be
told which of the two happened.

A test feeds it a tetrahedron with one face removed and requires the refusal.
Netgen prints `Meshing of domain 1 failed with error: Stop meshing since too
many attempts in domain 1` while that test runs; that is the backend being
honest in the one channel it has, and it is expected output rather than a
problem.

## Meshing parameters, and which are pinned

```text
maxh              from VolumeMeshControls::maxElementSize when given,
                  otherwise Netgen's own default
second_order = 0  PINNED. Tet4 is P16's only volume element (ADR-031), and a
                  Tet10 would arrive as ten nodes through an interface that
                  promises four.
quad_dominated = 0  PINNED, same reason.
invert_tets = 0   PINNED. The boundary is already coherently oriented by
invert_trigs = 0  P16-SURF-001; nothing here may flip it.
```

Everything else is left at Netgen's defaults. Sizing as a user-facing concept
is `P16-SIZE-001`'s, and `SurfaceMeshControls` already warns that a second
canonical home for it would be the competing-state failure.

## Global state and threading

nglib keeps global state: `Ng_Init`/`Ng_Exit` bracket it and the mesher reads
module-level parameters. Every call into the backend is therefore serialised by
a `std::mutex`, and the library is initialised **per generation** rather than
once per process — which gives each generation the same starting state, and is
what makes repeated meshing of one body produce one answer.

The Init/Exit cycle was proven to survive repetition in `INFRA-NETGEN-001`
before being relied on here.

**Not tested concurrently.** The mutex is the design; two threads actually
meshing at once is not exercised by any test, and is recorded as a limitation
rather than claimed.

## Units

BetterCAD is SI internally, so the backend receives **metres** — a 20 mm box
arrives as 0.02. Netgen is unit-agnostic but has some absolute defaults
(`minedgelen` 1e-4, `maxh` 1000), so working at this scale is a real question
rather than an obvious one. It was measured rather than assumed:

```text
1 mm cube         meshed, volume recovered to 1e-9 relative
0.4 mm wall on a 40 mm plate (aspect 100)   meshed, volume recovered
```

Both are in `REFERENCE_CASES.md`. The tests are written so that an explicit
refusal would also pass — the requirement is that the outcome is one of two
named things, never a mesh that is quietly wrong — and in fact both succeeded.

## What does not cross the seam

```text
Ng_Mesh*                 owned by a scope guard inside one function
Ng_Tet / Ng_Volume_Element_Type    read, checked, discarded
Ng_Meshing_Parameters    constructed and used inside one function
Ng_Result                translated into VolumeBackendFailure
nglib's error text       QUOTED inside a BetterCAD diagnostic, which ADR-033
                         permits; its TYPES are not, which ADR-033 forbids
```

`nglib.h` is included in exactly one translation unit, wrapped in
`namespace nglib { }` because Netgen's own `nglib.cpp` does the same and the
symbols are mangled accordingly — a consumer that includes it at global scope
compiles cleanly and then fails to link.
