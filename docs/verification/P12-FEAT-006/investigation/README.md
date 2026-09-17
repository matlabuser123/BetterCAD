# P12-FEAT-006 — Investigation: Variable-Radius Fillet, Setback, Corner Transitions

> **Historical record.** This is the capability assessment that blocked
> `P12-FEAT-006` on 2026-09-18 (commit `d14c817`, where it was this
> directory's `README.md`). The scope decision that followed (options A and C:
> implement verified variable-radius fillets, defer setbacks and corner
> transitions) and the implementation are recorded in
> [../README.md](../README.md). The probes it cites are in
> [../kernel-probe/](../kernel-probe/). The text below is unchanged apart from
> this note and the probe paths, which now start with `../`.

## Status

**BLOCKED.** The milestone names three capabilities. Each was assessed
separately against what OCCT 8.0.1, the kernel BetterCAD uses, offers
through its public API. The assessment read the kernel's sources and ran
two probes.

| Capability | OCCT 8.0.1 | Classification |
| --- | --- | --- |
| A. Variable-radius fillet | Radius stations along an edge; exact rolling-ball sections of the kernel's own radius law | **PARTIALLY SUPPORTED** |
| B. Setback controls | No setback input anywhere; the kernel trims blends at a corner by its own rules | **UNSUPPORTED** |
| C. Corner-transition controls | No corner choice; the corner blend is the kernel's, computed in protected steps | **UNSUPPORTED** |

- **B and C need new surface modeling.** Setback distances or a selectable
  corner transition would require BetterCAD to build its own corner
  patches: trimmed blends, N-sided G1 fills, sewing and validation. That is
  a new surface-patch subsystem, which the P12 instructions say must not be
  built opportunistically.
- **A works, with caveats.** The kernel builds geometrically exact
  variable-radius fillets. But the radius between the stations is not what
  its API documents, is not under the caller's control, and changes with
  the fillets next to it. Its unguarded paths crash the process or return
  wrong solids reported as valid.
- **Using A needs a decision first.** Offering it means adopting the
  kernel's interpolation as BetterCAD's definition of a variable radius.
  That is part of the scope decision below, not an implementation detail.

**Nothing was implemented in BetterCAD.** No test changed, and no box is
ticked. The two probes are evidence, not part of the build, like the
earlier kernel probes.

Date: 2026-09-18. `main` was at `aa2d41a` (`P12-FEAT-005`).

## Method

- **Sources.** The OCCT 8.0.1 sources are the tarball the dependency
  superbuild downloaded (`occt-V8_0_1.tar.gz`). The parts cited are quoted
  with their line numbers in `../kernel-probe/occt-source-excerpts.txt`.
- **Public interface.** `../kernel-probe/api-search.log` covers:
  - the public interface of `BRepFilletAPI_MakeFillet`;
  - the access levels of the corner steps in `ChFi3d_Builder` and
    `ChFi3d_FilBuilder`;
  - a search of all 6748 installed headers for setback and vertex-blend
    inputs.
- **`../kernel-probe/fillet_probe.cpp`** (`fillet-probe.log`) builds constant,
  two-station, station-list and corner fillets on a 100 × 60 × 40 box. It
  compares them with analytic volumes and tries each fillet shape.
- **`../kernel-probe/variable_radius_probe.cpp`** (`variable-radius-probe.log`)
  runs each case in its own process. It measures what the kernel builds
  against geometry computed in the probe itself:
  - **Reference law.** Its own tridiagonal solve for the clamped cubic
    spline gives the radius law.
  - **Reference volume.** The removed volume is (1 − π/4)∫r² dx,
    integrated exactly with 4-point Gauss–Legendre on each span.
  - **Section model.** For the box edge along x, the section at x = c is a
    quarter circle of radius r(c) about (c, r, r). It touches y = 0 at
    z = r and z = 0 at y = r.
  - **Sampling.** Every fillet face is sampled on a 61 × 61 grid and every
    boundary edge at 201 points.

Build commands and the compiler are recorded at the top of each log.

## A. Variable-Radius Fillet — Partially Supported

### What the API offers

`BRepFilletAPI_MakeFillet` takes:

- `Add(R1, R2, E)`, documented as "a linear radius evolution law";
- `Add(UandR, E)`: (parameter, radius) stations;
- `Add(Law_Function, E)`, `SetRadius(...)` and `SetLaw(IC, E, law)`;
- `GetLaw` and `GetBounds`, to read the law back.

### What the kernel builds

`occt-source-excerpts.txt`, items 2–11, gives the source for each step:

1. **Stations.** `Add(R1, R2, E)` sets the stations (0, R1) and (1, R2).
   `Add(UandR, E)` sets the given stations.
2. **Extension.** For the build, the kernel extends the spine beyond the
   edge. It adds the first and last station radii again as stations at the
   extended ends (item 5a).
3. **Interpolation.** Between all these stations the radius is
   `Law_Interpol(stations, 0, 0)`. That is the clamped cubic spline: C2,
   knots at the stations, zero slope at both extended ends (items 5b–7).
4. **Sections.** Each section lies in the plane normal to the spine, with
   the law's radius (item 11).
5. **Extension lengths.** They come from fixed rules in `ChFi3d_FilBuilder`
   (items 8–10):

   | End of the fillet | Extension |
   | --- | --- |
   | No other blend meets it | 0.5 × the spine length |
   | On a free boundary of the shape | 1.0 × the spine length |
   | Two blends meet there | the larger of 0.3 × each spine length and 1.5 × its largest radius, for both |
   | Three blends meet there | 0.1 × the spine length (depending on the corner's convexity) |
   | Tangent end | none |

### What the probes measured

| Check | Cases | Result |
| --- | --- | --- |
| Law read back (`GetLaw`) against the clamped spline through the stations and the extended ends (`GetBounds`) | 1a–1c, 1t, 1l, 1s, 2, 2t, 2s, 11 | ≤ 1.1e-14 |
| Fillet surface against the section model of that spline | same | ≤ 1.8e-14 mm |
| Contact lines (z = r(x) on y = 0, y = r(x) on z = 0) | same | ≤ 2.3e-14 mm |
| Removed volume, kernel adaptive integration with 1e-10 (what `Body::massProperties()` uses for these bodies) | same | ≤ 8.8e-10 relative |
| The same, Gauss–Kronrod over knot spans with 1e-10 | same | ≤ 4.1e-10 relative |
| The same, the kernel's default integration (no tolerance) | same | up to 1.0e-3 relative (case 2) |
| Extension of 30, 100 and 150 mm edges no other blend meets | 1s, 1b, 1l | [−15, 45], [−50, 150], [−75, 225]: 0.5 × length |
| Approximation tolerance 1e-6 instead of 1e-4 | 1t, 2t | identical results |
| Constant radius r 5 on one edge and on three edges at a vertex | probe 1, A and D | 5.1e-15 and 2.8e-14 relative to the analytic volumes |

Within its own law, the kernel builds exact variable-radius sections.
Every built result was valid and free of self-intersection except case 10,
and case 4 built no fillet at all (both below). Case 3 ended the process.

### Why only partially

1. **"Linear" is not linear.** For r 3 → 8 on a 100 mm edge, the radius a
   quarter of the way along is 3.982 (a linear law gives 4.25). The
   removed volume is 1.6 % more than a linear law removes. Probe 1, B
   shows 0.7 %, 1.6 % and 2.6 % for end radii 5.5, 8 and 12.
2. **The shape between stations depends on the neighbours.** The same
   edge with the same stations gets a different law when other fillets
   meet it:
   - where no other blend meets it, the law's bounds are [−50, 150]
     (case 1b);
   - with two blends at its corner they are [−30, 150] (case 7);
   - with three they are [−10, 150] (case 8).

   So a fillet's radius between its stations changes when a neighbouring
   edge is added to or removed from the fillet.
3. **The radius leaves the station range, without notice.**
   - Stations 5, 5, 20, 20 (at 0, 0.4, 0.6, 1) give radii from 1.02 to
     23.98 (case 11). The result is valid.
   - Stations 5, 5, 36, 36 give −3.23 to 44.23 (case 10). The builder
     reports success, and the result is invalid and self-intersecting.

   Every station radius in both cases fits the corner. So a room check on
   the stations, as the constant-radius fillet does, would not protect the
   build. It would have to check the spline's extremes, which depend on
   the extension rules above.
4. **A radius too large for its face is not refused.** r 3 → 45 on a
   40 mm face (case 5; probe 1, H) is reported done, valid and without
   self-intersection, yet it is not that fillet:
   - the fillet face reaches y = 45;
   - the y = 0 face is cut off at x = 82.75.
5. **BetterCAD cannot impose its own law.**
   - `Add(Law_Function, E)` passes the law to
     `ChFiDS_FilSpine::SetRadius`, which builds it into a local object and
     drops it, clearing the stations (item 4). The build then ends the
     process with an access violation, `0xC0000005` (case 3). This is the
     same uncatchable failure mode recorded for `ChFi3d` since P11.
   - `SetLaw` after a build, followed by another build, returns the
     unfilleted box, reported valid (case 4).
6. **Tangent chains.** Stations given on one edge of a line–arc–line
   chain apply to that edge. The rest of the chain keeps a constant radius
   (case 9: `GetLaw` on the arc reports "no law on constant edges").
7. **Closed edges.** A disc's rim with stations 3, 6, 3 builds (case 6).
   The periodic law, whose closing slope comes from a separate estimate,
   was not examined.

### What a verified variable-radius fillet could be

If variable radius is to be offered from this kernel, a validated
implementation would have to:

- define the radius as exact at the stations, with the kernel's clamped
  spline in between;
- define where a station lies on an edge referred to by its supporting
  curve (`EdgeSignature`), for example a fraction of the edge from the end
  first in the curve's canonical direction. A full circle has no such end;
- compute that spline, and its extension rules, itself;
- check the law the kernel reports after every build;
- before any build, refuse stations whose spline is not positive or leaves
  the room at any point;
- never use `Add(Law_Function)` or `SetLaw`;
- restrict what it has not validated, such as closed edges;
- check volumes at about 1e-9 relative rather than 1e-12, because the
  kernel's surface integration limits them.

The fillet feature's definition, file format, validation and tests would
grow accordingly. Whether this definition is acceptable (not linear, and
dependent on neighbouring blends) is a decision, not a fact the probes
can settle.

## B. Setback Controls — Unsupported

- **No input.**
  - `BRepFilletAPI_MakeFillet`'s public interface has no setback
    (`api-search.log` §2).
  - No modeling header in OCCT 8.0.1 mentions setbacks or vertex blends.
    The only matches for the search are view setters such as
    `SetBackground` (§1).
- **The kernel decides where strips end.** Where blends meet, it stops
  each strip by its own extension rules (items 8–10) and fills the corner
  itself.
- **What setbacks would take:**
  1. trim each edge blend back by given distances along its edges;
  2. build an N-sided (up to 2N-sided) patch between the trimmed blends
     and the faces, tangent to all of them;
  3. sew it into the solid, check it, and name its faces.

  OCCT provides only approximate filling tools for step 2:
  `BRepFill_Filling`, `GeomPlate_BuildPlateSurface` and
  `GeomFill_ConstrainedFilling` (§5). Everything else would be new
  BetterCAD surface-modeling code.

## C. Corner-Transition Controls — Unsupported

- **The only shape option is the cross-section.** `SetFilletShape`
  (rational, quasi-angular, polynomial) affects edge blends, not corners.
  For a constant fillet between planes all three give the same volume,
  536.504591506 (probe 1, G).
- **The kernel chooses every corner.**
  - Three equal radii at a vertex give a sphere-octant corner, matching
    r³(1 − π/6) plus the strips within 2.8e-14 (probe 1, D).
  - Different or variable radii give the kernel's own patches: 7 and 4
    surfaces (probe 1, E and F), and cases 7 and 8.
- **The corner steps are not reachable.** `PerformTwoCorner`,
  `PerformThreeCorner`, `PerformMoreThreeCorner` and `Extent*Corner` are
  protected. `PerformTwoCornerbyInter` is public but is an internal step,
  and `BRepFilletAPI_MakeFillet` keeps its `ChFi3d_FilBuilder` private
  (`api-search.log` §3). Changing a corner means subclassing the builder
  and writing the corner surfaces: the same new subsystem as B.

## Root Cause

OCCT 8.0.1's fillet builder computes corners and strip ends by internal
rules and exposes no setback or corner-type input. Its variable-radius
support is a fixed interpolation whose shape depends on those same
internal rules, and its law-function inputs are broken. Setbacks and
selectable corner transitions are therefore not a matter of calling an
API. They need a BetterCAD-owned surface-patch layer (trim, fill, sew,
validate) that P12 has not authorized.

## Options for the Scope Decision

- **A. Narrow FEAT-006 to verified variable-radius fillets.**
  - Radius stations on single open edges (and, if validated, tangent
    chains), exact at the stations.
  - The kernel's clamped-spline law in between, documented as such and
    checked after every build.
  - Stations refused when the spline leaves the positive range or the
    room.
  - Neighbour-dependent laws either accepted as defined or refused (for
    example, variable-radius edges may not meet other blended edges).
  - Setback and corner transitions recorded as out of scope.
- **B. Authorize a separate advanced surface-modeling prerequisite.**
  - A BetterCAD corner-patch subsystem (trimmed blends, N-sided G1 fills,
    sewing, validation, face naming) with its own milestone and
    references.
  - FEAT-006's setback and corner-transition parts would follow it.
- **C. Defer setback and corner-transition controls.**
  - Remove them from P12, record them for a later surface-modeling phase,
    and decide separately whether variable radius stays in P12 (as in A)
    or is deferred too.

## Recommendation for the Scope Decision

A combined with C:

- **Narrow `P12-FEAT-006` to variable-radius fillets** with the
  definition and guards in A: stations exact, the kernel's spline
  documented and checked, and stations whose spline leaves the positive
  range or the room refused.
- **Refuse variable-radius edges that meet other blended edges.** That
  keeps each edge's radius a function of its own definition.
- **Defer setbacks and corner transitions** to a surface-modeling phase.

Option B is the only way to deliver setbacks and corner transitions. It
is a large subsystem whose validation, beyond the analytic sphere corner,
has no independent references ready. This is a recommendation only. No
scope change has been made, and P12 does not continue past this milestone
until the decision.

## Evidence Files

- `README.md`: this file.
- `../kernel-probe/fillet_probe.cpp`, `../kernel-probe/fillet-probe.log`: the
  first probe (constant, two-station and station fillets, corners, fillet
  shapes, a radius too large).
- `../kernel-probe/variable_radius_probe.cpp`,
  `../kernel-probe/variable-radius-probe.log`: the measurements of A, one
  process per case.
- `../kernel-probe/occt-source-excerpts.txt`: the OCCT 8.0.1 source cited
  above, with line numbers.
- `../kernel-probe/api-search.log`: the public fillet interface, the corner
  steps' access levels and the header search.

## Final Result

```text
TASK:            P12-FEAT-006 Variable-radius fillet, setback, corner transitions
IMPLEMENTATION:  none in BetterCAD; two kernel probes (evidence only)
TESTS:           no BetterCAD test added or changed; probes run on OCCT 8.0.1
                 (18 variable-radius cases in separate processes, 12 fillet
                 cases)
VALIDATION:      variable-radius sections match the kernel's clamped-spline
                 law within 1.8e-14 mm, contact lines 2.3e-14 mm, law
                 1.1e-14, volumes 8.8e-10 (adaptive 1e-10); the law is not
                 the documented linear one and depends on neighbouring
                 blends; Add(Law_Function) crashes (0xC0000005); SetLaw +
                 rebuild returns the unfilleted box; oversized and
                 overshooting laws are reported done (one valid but wrong,
                 one invalid); no setback or corner-type input exists
RESULT:          BLOCKED — A partially supported, B and C unsupported
                 without a new surface-patch subsystem
EVIDENCE:        docs/verification/P12-FEAT-006/
TODO:            P12-FEAT-006 recorded as blocked; no box ticked
```
