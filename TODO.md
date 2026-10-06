# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:
           P16 — Meshing — QUALIFIED

Current milestone:
           None. P16-QUAL-001 is PASS (2026-10-06), so P16 — Meshing is
           QUALIFIED. The next phase is a scope decision, not Claude's to
           make.

Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data
           P16 — Meshing

Next:
           P17 — Structural FEA. NOT AUTHORIZED: P16 being qualified is not
           permission to start the next phase. Starting it is a scope
           decision.

Qualified milestones in P16:
           P16-ARCH-001 — Meshing Architecture (ADR-030 to ADR-033)
           P16-DATA-001 — Mesh Data Model / Identity / Units
           P16-GEOM-001 — Geometry Preparation / Validity / Regeneration Boundary
           P16-SURF-001 — Engineering Surface Mesh
           INFRA-NETGEN-001 — Netgen toolchain qualification (infrastructure)
           P16-VOL-001 — 3D Tetrahedral Volume Mesh (Tet4)
           P16-SIZE-001 — Global / Local Mesh Sizing
           P16-QUALITY-001 — Mesh Quality Metrics / Validation
           P16-MAP-001 — Geometry ↔ Mesh Correspondence / Regions
           INFRA-VIEWER-001 — OCCT visualization toolchain qualification
                              (infrastructure)
           P16-VIZ-001 — Mesh Visualisation / Inspection
           P16-CMD-001 — Meshing Commands / Undo / Redo
           P16-PERSIST-001 — Meshing Intent Persistence
           P16-CLI-001 — Headless Meshing Workflows
           P16-REFMOD-001 — Meshing Reference Models
           P16-QUAL-001 — Full P16 Qualification

P16 objective:
           Geometry
           → canonical meshing controls
           → generated engineering mesh
           → geometry/mesh correspondence
           → mesh-quality validation
           → downstream P17 Structural FEA

P16 does NOT implement:
           structural FEA
           stresses
           strains
           loads
           restraints
           stiffness matrices
           thermal solver
           CFD discretisation
```

---

# Carried

```text
1. FileIo atomic replace

   A document save can still lose to a file synchroniser when Windows
   temporarily refuses the final rename.

   Keep carried.
   Do not silently fold this into P16.


2. Configuration regeneration

   A configuration parameter override can change the effective parameter
   without rebuilding the geometry that reads it.

   This matters directly to P16:
   a mesh must NEVER be generated from stale configuration geometry.

   Until fixed:
   configuration-sensitive meshing must explicitly refuse stale geometry,
   following the same safety principle used by P15 mass properties.


3. Hole POSITION dimensions unsupported.

4. GD&T symbols not fully embedded in PDF/DXF.

5. Cross-preset drawing export byte identity not guaranteed.
```

---

# Completed Phases

```text
P0–P10   BetterCAD v0.1.0
P11      Advanced Part Modelling              QUALIFIED
P12      Production Part Modelling            QUALIFIED
P13      Assemblies                           QUALIFIED
P14      Technical Drawings                   QUALIFIED
P15      Materials / Engineering Data         QUALIFIED
P16      Meshing                              QUALIFIED
```

Detailed completed history belongs in:

```text
ROADMAP.md
docs/verification/
docs/architecture/decisions/
```

Do not expand completed-phase implementation diaries in `TODO.md`.

---

# Future Phases

```text
P17  Structural FEA
P18  Thermal Analysis
P19  CFD Integration
P20  Design Optimization
P21  Semantic Topology
P22  Versioning / Collaboration
P23  Python / Automation
P24  AI Engineering Agent
P25  Manufacturing / CAM
P26  Performance / GPU / Scale
P27  Production Hardening
P28  BetterCAD 1.0
```

---

# Workflow

```text
UNDERSTAND
→ ARCHITECT
→ BLAST RADIUS
→ IMPLEMENT
→ TARGETED TESTS
→ INDEPENDENT VALIDATION
→ FAILURE PATHS
→ PERSISTENCE
→ DETERMINISM
→ ADVERSARIAL REVIEW
→ FULL REGRESSION
→ EVIDENCE
→ [x]
→ COMMIT
→ PUSH
```

If a gate fails:

```text
STOP
→ reproduce
→ regression test
→ root cause
→ fix
→ revalidate
```

Never mark work complete because it merely compiles.

---

# Project Authority

```text
TODO.md
→ current authorized work

ROADMAP.md
→ long-term direction + completed phases

ARCHITECTURE.md
→ architecture / invariants

CLAUDE.md
→ engineering process / Definition of Done

docs/architecture/decisions/
→ durable ADRs

docs/verification/<milestone>/
→ qualification evidence

docs/engineering/
→ reusable engineering templates
```

---

# CURRENT NEXT STEP

```text
P16 — Meshing is QUALIFIED. The next step is a scope decision, not an
implementation.
```

BetterCAD generates, inspects, persists and drives a validated Tet4 volume
mesh from authoritative CAD geometry, with sizing controls, quality metrics,
geometry/mesh correspondence, boundary regions, undo/redo, a headless CLI and
eight independently validated reference models. Evidence:
[docs/verification/P16-QUAL-001/](docs/verification/P16-QUAL-001/README.md).

## What is next, and whose call it is

```text
P17  Structural FEA     the next phase in the roadmap. NOT AUTHORIZED.
                        P16 being qualified is not permission to start it.
```

A qualified phase is a stop condition. Nothing in this document authorizes
P17, and being next in the roadmap is not authorization.

## Still owed, from earlier reviews

```text
F6 (P16-ARCH)   DISCHARGED by P16-QUAL-001: a MeshControl whose body is
                deleted reports ineligibility object_not_found, refuses
                generation, and keeps the mesh it already held marked stale.
                One test proves the sequence.
F6 (P16-GEOM)   partMassProperties has no currency check, so mass properties can
                be computed from stale geometry after an unregenerated edit. A
                P15 behaviour change, so a scope decision
cross-preset    DISCHARGED by P16-REFMOD-001: the CLI reference fixtures run
determinism     in a fresh process in every preset and assert exact element
                counts and SI volume doubles, so a mesh IS now compared across
                Debug, Release and Debug-shared.
no sanitizers   this MinGW ships no libasan/libubsan, so neither Netgen nor
                BetterCAD has ASan/UBSan coverage. Worth a different toolchain
                before a volume mesh is trusted numerically. (INFRA-NETGEN-001)
deps/ unfingerprinted
                the qualification fingerprints 8 paths, and deps/ is not one of
                them, so a changed Netgen patch would not void a qualification.
                Pre-existing in kind -- a differently-rebuilt dependency prefix
                was never detectable either. (INFRA-NETGEN-001)
clean-checkout  a clean checkout cannot re-fetch Catch2 or nlohmann_json,
fetch           because CMake's bundled curl carries no CA trust anchors in
                this environment. Verified instead with
                FETCHCONTENT_SOURCE_DIR_* pointing at the same SHA256-verified
                archives. (P16-REFMOD-001, P16-QUAL-001)
stale cache     nothing FORCES a holder to call isStale. A VolumeMesh records
                its source and revision and answers truthfully, and the request
                path refuses stale geometry, so this bites only code that
                CACHES a mesh. P17's solver entry point should take the
                document and feature, or re-check. (P16-VOL-001)
concurrency     calls into the volume backend are serialised by a mutex because
                nglib keeps global state; no test runs two threads through it.
                (P16-VOL-001)
multiple solids a body with more than one solid is REFUSED, explicitly and
                tested. ADR-032's one-region-per-solid remains the eventual
                design. (P16-VOL-001)
sphere          no fixture, so a degenerate pole edge is the one geometry where
                exact-coordinate node unification is untested
boundary edges  protected by exactly one test, because closed solids cannot
                detect the counter's removal
quality         P16-QUALITY-001's policy is deliberately report-only and
thresholds      ships no accept/reject thresholds. The phase that consumes a
                mesh owns them. (P16-QUALITY-001, P16-QUAL-001)
drilled hole    a hole cut by a feature has no stable face reference, so a
wall            boundary set cannot name it. A tube's bore can. (P16-MAP-001)
RM-MESH-09      the configuration reference case is DEFERRED, not failed: a
                configuration override is a refusal by design, so the case
                would assert a refusal rather than a mesh. It belongs with
                carried defect 2. (P16-REFMOD-001)
```

## Carried defects, neither P16's

```text
the FileIo replace defect     a document save can still lose to a file
                              synchroniser. Arguably ahead of any new milestone.

configuration regeneration    a configuration override does not rebuild the
                              geometry it changes. Mass properties and meshing
                              both REFUSE under one, with the same guard.
```

Also carried from earlier phases: hole POSITION dimensions are unsupported, GD&T symbols are not
fully embedded in PDF/DXF, and cross-preset export byte identity is not guaranteed for drawings.
