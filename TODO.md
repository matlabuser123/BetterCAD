# BetterCAD — TODO

> `[x]` = implemented **+** tested **+** validated **+** evidence recorded.
> Nothing else earns a tick. Documentation work never ticks a box.

This file authorizes implementation. [ROADMAP.md](ROADMAP.md) does not: a
capability listed there is not authorized by being listed, by being next, or by
being easy. Milestone IDs are allocated here, in sequence, when work is
authorized — never in advance.

## Status

| | |
| --- | --- |
| Current | **None** |
| Next | **Awaiting explicit scope decision** |
| Blocked / Manual | None |
| Last milestone | `P11` Production Part Modeling — **QUALIFIED** |
| Released | `v0.1.0` (`P0`–`P10`); `P11` qualified, not released |

## Current

Nothing in progress. No milestone is open.

## Next

Awaiting an explicit scope decision. The next milestone must be recorded here —
ID, deliverables, acceptance gates — before any implementation begins.

The candidates are in [ROADMAP.md](ROADMAP.md#capability-roadmap). The one
dependency worth weighing first: a minimal stable-reference layer (semantic
topology) may be a prerequisite for assemblies, drawings and simulation boundary
conditions, because all three attach to faces and edges that geometric
references cannot follow today.

## Blocked / Manual

| Item | Why |
| --- | --- |
| `LICENSE` | Not yet chosen. A decision, not an implementation. |
| Release tagging | Manual, and only on request. `v0.1.0` is the only tag. |

## Planned — Not Authorized

Not started. Not authorized. Grouped to match
[ROADMAP.md](ROADMAP.md#capability-roadmap).

### Parametric CAD completion

- [ ] Shell feature
- [ ] Draft feature
- [ ] Rib feature
- [ ] Variable-radius fillets, setback and corner-transition controls
- [ ] Datum planes, axes and coordinate systems
- [ ] Parameter expression evaluation
- [ ] Sketch constraints: angle, tangent, concentric, midpoint, symmetric, diameter
- [ ] Sketch entities: ellipse, spline
- [ ] Sketches on arbitrary planar faces
- [ ] Through-all extrude
- [ ] Split body and combine
- [ ] Configurations and design equations
- [ ] Hole threads, spotface, standards databases and tolerance classes
- [ ] Pattern modes: symmetric, total-length, suppressed instances, patterns of patterns
- [ ] Sweep guide curves, twist and non-planar paths
- [ ] Loft sections of differing shapes, smooth interpolation, end conditions

### Desktop application

- [ ] Tessellated display pipeline and 3D viewport
- [ ] Model tree and property editor
- [ ] Command system and diagnostics panel
- [ ] Sketch environment
- [ ] Selection: body, face, edge, vertex, sketch, feature
- [ ] GUI smoke tests

### Semantic topology

- [ ] Minimal stable-reference layer: feature provenance and persistent naming
- [ ] Adjacency signatures and semantic matching
- [ ] Ambiguity detection and confidence scores
- [ ] Reference recovery after topology-changing edits

### Interchange

- [ ] STEP import
- [ ] DXF import and export
- [ ] IGES and OBJ
- [ ] Document schema migration between format versions

### Assemblies

- [ ] Component model: part reference, instance, transform, suppression
- [ ] Mate representation
- [ ] Assembly solver, separate from the mate model
- [ ] Interference and clearance analysis
- [ ] Assembly mass properties
- [ ] Exploded views and motion constraints

### Drawings

- [ ] Sheets and standard views
- [ ] Projected, section and detail views
- [ ] Dimensions, centre marks and annotations
- [ ] GD&T foundation
- [ ] BOM tables, balloons, revision tables
- [ ] PDF, DXF and SVG export

### Engineering data

- [ ] Material database
- [ ] Material assignment, persisted through save/load
- [ ] Mass properties driven by assigned materials

### Simulation

- [ ] Meshing infrastructure: 1D/2D/3D, sizing, quality metrics
- [ ] Structural FEA: linear elastic, static
- [ ] Analytical FEA benchmarks: bar, cantilever, simply supported, plate
- [ ] Thermal analysis, coupled into structural expansion
- [ ] CFD: fluid volume extraction through solver results

### Optimization

- [ ] Design variables and design studies
- [ ] Parameter sweeps
- [ ] Gradient, population and surrogate methods

### Automation and versioning

- [ ] Python bindings (pybind11) over the public API
- [ ] Plugin system with a declared API version
- [ ] Semantic document diff
- [ ] Engineering version control: commit, branch, merge, history

### AI engineering

- [ ] Structured tool layer over the public API
- [ ] Intent extraction and engineering plan validation

### Manufacturing

- [ ] Manufacturing metadata: stock, threads, tolerances, finish, bends
- [ ] 2.5D CAM foundation

### Tooling and hardening

- [ ] CI workflow (`.github/workflows` does not exist)
- [ ] Sanitizer presets (ASan/UBSan)
- [ ] Coverage configuration
- [ ] Enforced `.clang-format` check
- [ ] Automated documentation link check
- [ ] Regeneration and pattern performance benchmarks
- [ ] Crash recovery and autosave

## Recently Completed

| Milestone | Commit | Evidence |
| --- | --- | --- |
| `P11-QUAL-001` Qualification | `449d5fd` | [P11-QUAL-001](docs/verification/P11-QUAL-001/README.md) |
| `P11-REF-001` Mechanical reference models | `79dab04` | [P11-REF-001](docs/verification/P11-REF-001/README.md) |
| `P11-FEAT-009` Loft | `24a8134` | [P11-FEAT-009](docs/verification/P11-FEAT-009/README.md) |

## Completed Milestones

`P0`–`P11` are complete. Each milestone, what it delivered and its evidence are
recorded in [ROADMAP.md](ROADMAP.md#capability-roadmap) under the capability it
belongs to, so this file stays a list of work still to do.

Per-milestone acceptance criteria, measured values, analytic validation and
known limits live with the evidence in
[docs/verification/](docs/verification/), and are not summarised here.

## Known Limitations

Properties of the system as qualified. Each modeling limitation is pinned by a
regression test, so none can change silently. Detail:
[P11-QUAL-001](docs/verification/P11-QUAL-001/README.md).

### Modeling

- **Geometric references do not follow moved geometry.** Edges and faces are
  matched by geometry — an edge's supporting line or circle, a face's plane and
  outward side — not named semantically. When a parameter moves the referenced
  geometry, the feature fails with `NotFound` and keeps no body. Nothing is ever
  substituted.
- **Parameter expressions are stored but not evaluated.** A derived dimension
  needs its own parameter, or a sketch that builds the relation geometrically.
- **No through-all extrude.** A cut is given a depth.
- **Loft sides stay B-splines** even where flat, costing about 6e-12 relative
  volume and 3.4e-6 mm in the centroid; plane references find only a loft's end
  faces.
- **Uniting a half body with its mirror image** is refused when a half cylinder
  lies on the mirror plane: the kernel's fuse returns a shape its own checker
  rejects, so BetterCAD refuses it rather than building it wrongly.
- **Sketch constraints** cover coincident, horizontal, vertical, parallel,
  perpendicular, distance, radius, equal and fixed only.
- **Pattern cost** grows with the square of the instance count, capped at 500.

### Not implemented

- The desktop application is a placeholder shell; no GUI functionality is
  claimed or qualified.
- STEP **export** only. The kernel-based reader under `tests/support/occt/` is
  test-only tooling that reads exports back to check them. No DXF, IGES or OBJ.
- Everything under *Planned — Not Authorized* above.

### Limits of the evidence

- One platform: Windows 11 AMD64, GCC 16.1.0 (MinGW-w64), OCCT 8.0.1, Qt 6.11.2.
- No CI, no sanitizers, no coverage, no memory checking; `.clang-format` is
  defined but unenforced.
- 738 tests passing means 738 tests passed. Where a behaviour has no test, the
  evidence says nothing about it.

## Project Documents

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction and completed capabilities |
| [TODO.md](TODO.md) | Authoritative implementation status and next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow for coding agents |
| [docs/verification/](docs/verification/) | Proof of completion |
