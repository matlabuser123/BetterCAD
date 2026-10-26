# P19-ARCH-001 — CFD Architecture / Authority Contract

```text
STATUS:    PASS
MILESTONE: P19-ARCH-001, the first milestone of P19 — CFD Integration
SCOPE:     architecture only. No cell, face, field, boundary condition,
           viscosity, operator, matrix, pressure-velocity coupling, solve,
           result, command, file schema or CLI verb.
```

## Baseline

```text
HEAD at start      e1a8eb29e4d477cdd5fe3a8b82edac25629420b6  (P18 qualified in
                   the roadmap, the P19 plan recorded in TODO.md)
origin/main        e1a8eb29e4d477cdd5fe3a8b82edac25629420b6, verified
working tree       clean but for the untracked .claude/ settings directory
authorized         2026-10-27, the owner's "start P19-ARCH-001", with the
                   checklist recorded in TODO.md
```

## Prerequisites

```text
P15 — Materials / Engineering Data    QUALIFIED
P16 — Meshing                         QUALIFIED
P17 — Structural FEA                  QUALIFIED, F1-F3 carried
P18 — Thermal Analysis                QUALIFIED 2026-10-27, 5804/5804 x 3,
                                      F1-F3 carried as owner decisions
```

## What the audit found before anything was built

```text
P15 has a density (requireDensity) and NO viscosity: no property, no
  ConsumerKind for CFD. DynamicViscosity and KinematicViscosity exist in core
  as quantities only. -> P19-FLUID-001 adds both; this boundary proves a
  density and says, in the header and in a tripwire test, that it proves
  nothing about a viscosity.
P15 has ONE material per document. -> v1 CFD runs on a document whose single
  assignment is the fluid; per-body materials are a prerequisite of any
  fluid-and-solid workflow, recorded, not built.
P16 provides the Tet4 volume mesh, the geometry/mesh map, the quality report,
  currency and tetrahedralBoundary; NOT interior faces, owner/neighbour
  topology or FV metrics. -> P19-MESH-001, P19-TOPO-001, P19-GEOM-001.
linalg's kernel is symmetric only; a momentum matrix is not symmetric.
  -> P19-LINSOLVE-001 decides.
```

Full audit: [DEPENDENCY_AUDIT.md](DEPENDENCY_AUDIT.md).

## The decisions

```text
ADR-058  scope: 3D, single phase, incompressible, Newtonian, constant rho and mu;
         steady laminar (SIMPLE) and transient laminar (backward Euler);
         collocated cell-centred finite volume, a cell is a P16 tet, face flux
         conserved with Rhie-Chow interpolation, first-order upwind first;
         velocity inlet, static-pressure inlet/outlet, no-slip and moving wall,
         slip/symmetry; every boundary facet exactly one condition, no default
         wall or outlet; an unresolved reference is a named refusal, never a
         rebind; a reference pressure iff nothing fixes p, refused if
         overdetermined; incompatible prescribed flux refused, never rescaled;
         p is static gauge pressure with the hydrostatic part inside it; mass
         balance = cell continuity residual + global boundary imbalance over
         throughput; exceeding the laminar bound is a named refusal; isothermal,
         no coupling in v1; bit identity claimed only for what is assembled and
         summed in a fixed order.
ADR-059  a cfd module at layer 42, below thermal (45) and structural (50), so
         CFD -> structural (a pressure load) and later CFD -> thermal can be
         read without a renumber; cfd links neither, and its link graph is an
         allow-list.
ADR-060  requireCfdModel / CfdModel: possession is the proof; the fluid is the
         document's P15 material; the boundary is a third copy of the
         thermal/structural boundary, kept in step by a test, with extraction
         of the shared seam proposed to P19-MESH-001.
```

Every checklist item and the ADR section that answers it: [SCOPE.md](SCOPE.md). Authority, currentness and the
canonical/derived split: [AUTHORITY_MODEL.md](AUTHORITY_MODEL.md). The layer: [LAYERING.md](LAYERING.md).

## The input boundary

```cpp
Result<CfdModel> requireCfdModel(
    const Document&, const features::Regenerator&,
    const meshing::Mesher&, MeshControlId);
```

No function in the module takes a mesh. `CfdModel` has one private constructor, one friend, and is move-only.
Possession proves the control exists, the body is eligible, a mesh is held and current, and the document's
material yields a usable density; the model carries the material id and the geometry revision it was prepared
against. Seven input problems, each reachable and each named:

```text
control_not_found  geometry_ineligible  no_mesh  mesh_stale
mesh_generation_failed  no_material_assigned  fluid_density_unusable
```

## Tests

```text
tests/cfd/CfdAnalysisBoundaryTests.cpp    14 cases, 450 assertions, identical in
                                          all three presets
tests/architecture/                       8 fixtures, each one violation, each
                                          asserting the message naming it:
  cfd -> io, cfd -> structural, cfd -> thermal, meshing -> cfd, nglib.h in
  src/cfd/ (layering); cfd links thermal, structural, io (link graph)
architecture.layering on the real tree:   554 files, 0 violations
```

## Adversarial review

```text
INDEPENDENT REVIEWER     0 blockers, 6 major, 7 minor -- every one fixed,
                         decided in an ADR, or recorded with its reason
MUTATION PROBES          10 of 10 killed, every file restored byte for byte
AUTHOR'S QUESTIONS       9, each answered by a test, a gate or an ADR
GATE-BLOCKING            0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
The boundary proves a density and nothing about a viscosity, because P15 has
  no viscosity yet (P19-FLUID-001).
One material per document: a fluid and its solid housing cannot be analysed
  in one document until per-body materials exist.
A CfdModel borrows the Mesher's mesh, map and quality report, as the thermal
  and structural models do (P18-QUAL-001 D3): re-prepare rather than hold.
Three boundaries that must stay in step; a test compares the CFD boundary with
  both qualified ones, step for step, until P19-MESH-001 extracts the seam.
Recorded from the review, not fixed: no test uses another control's map, and
  is_constructible is checked for one signature only.
The permitted directions thermal -> cfd and structural -> cfd are proved by
  the valid fixture tree, not by a real include, because nothing consumes the
  module yet.
This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then two repeat stages, in one detached
run: 02:48:09 to 09:25:20, 6h37m11s, 17 stages, `qualify.cmd exit 0`. A first attempt, stopped when the linker ran
out of memory because the OneDrive client held 56 GB, is kept under `qualification/attempt-1/`.

```text
PRESET            CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext           0    30m33s     1 edge       5826/5826   4764.74 s
release-ext         0    36m17s     1 edge       5826/5826   4559.90 s
debug-shared-ext    0    30m56s     1 edge       5826/5826   4938.93 s

REPEAT (5x each of 2583 selected tests)
release-ext                                      2583/2583   1670.18 s
debug-ext                                        2583/2583   1951.05 s

warnings, all three clean builds   0   (-Werror, 745 objects each)
zero-match groups                  237, 0 short, 0 unexecuted, in each preset
```

[FREEZE.md](FREEZE.md), including the harness typo that produced two stderr lines and the postrun-script defect
that was found and fixed before its figures were used.

## Result

```text
RESULT:   PASS
TESTS:    5826/5826 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 745 objects; 2583 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     include e4503a17, src 3dd79170, tests 7967221f moved; apps,
          examples, cmake, CMakeLists.txt, CMakePresets.json identical to
          HEAD; identical before and after the run
EVIDENCE: this directory
ADRs:     ADR-058, ADR-059, ADR-060
NEXT:     P19-DATA-001 is NOT AUTHORIZED
```

## Revision

First issue, 2026-10-27.
