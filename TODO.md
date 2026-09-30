# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:
           P16 — Meshing

Current milestone:
           P16-GEOM-001 — Geometry Preparation / Validity / Regeneration Boundary

Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data

Next:
           P16-GEOM-001

Qualified milestones in P16:
           P16-ARCH-001 — Meshing Architecture (ADR-030 to ADR-033)
           P16-DATA-001 — Mesh Data Model / Identity / Units

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
```

Detailed completed history belongs in:

```text
ROADMAP.md
docs/verification/
docs/architecture/decisions/
```

Do not expand completed P15 implementation diaries in `TODO.md`.

---

# P16 — Meshing

## Goal

Give BetterCAD a trustworthy engineering-mesh layer that converts authoritative
CAD geometry into validated, deterministic mesh data suitable for downstream
numerical solvers.

```text
CAD model intent
→ regenerated authoritative geometry
→ meshing controls
→ surface representation
→ volume mesh
→ geometry correspondence
→ quality validation
→ P17 Structural FEA
```

P16 owns:

```text
mesh architecture
mesh data structures
meshing controls
surface meshing
volume meshing
mesh quality
geometry ↔ mesh correspondence
mesh inspection
mesh commands
mesh persistence contract
headless workflows
reference models
qualification
```

P16 does NOT own solver physics.

---

# P16 Core Invariants

* CAD geometry remains authoritative.
* Generated mesh is derived state.
* Meshing controls are canonical engineering intent.
* Generated nodes/elements are never treated as CAD geometry.
* A stale or failed model must never produce a nominally valid current mesh.
* Generated mesh IDs are mesh-local identities, not permanent CAD identities.
* Remeshing may invalidate NodeId / ElementId.
* Geometry references and mesh identities must remain separate.
* Engineering mesh must not silently reuse viewer/display tessellation.
* Invalid/open geometry must fail explicitly where a closed volume is required.
* Element orientation must be defined and validated.
* Inverted/degenerate elements must never be accepted silently.
* Mesh quality metrics must have explicit definitions.
* Local sizing must be explicit canonical intent.
* Missing sizing data must use documented defaults, never accidental backend defaults.
* Backend-specific behaviour must remain behind a meshing interface.
* P17 must consume P16 mesh APIs rather than creating its own competing mesher.
* Persisted mesh settings must reproduce meshing intent.
* Generated mesh is not authoritative persisted engineering state.
* Determinism must be measured rather than assumed.
* Every solver-facing mesh must pass validation before use.

---

# P16 Scope Boundaries

## In scope

```text
3D engineering mesh foundation
surface triangle representation
linear tetrahedral volume mesh foundation
global mesh sizing
local sizing foundation
geometry → mesh boundary mapping
mesh quality metrics
mesh inspection / visualisation
mesh regeneration
headless CLI workflows
solver-ready mesh API
```

## Initially out of scope unless P16-ARCH explicitly proves otherwise

```text
quadratic tetrahedra
hexahedral meshing
prism / boundary-layer meshing
adaptive FEA error refinement
CFD boundary-layer meshes
moving mesh
overset mesh
GPU meshing
distributed meshing
assembly contact meshing
automatic contact detection
structural solver
thermal solver
CFD solver
```

These can be added later without corrupting the P16 foundation.

---

# P16 Sequence

```text
P16-ARCH-001       Meshing architecture / backend decision
P16-DATA-001       Mesh data model / identity / units
P16-GEOM-001       Geometry preparation / validity / regeneration boundary
P16-SURF-001       Engineering surface mesh
P16-VOL-001        3D tetrahedral volume mesh
P16-SIZE-001       Global / local sizing controls
P16-QUALITY-001    Mesh-quality metrics / validation
P16-MAP-001        Geometry ↔ mesh correspondence / regions
P16-VIZ-001        Mesh visualisation / inspection
P16-CMD-001        Commands / undo / redo
P16-PERSIST-001    Meshing-intent persistence
P16-CLI-001        Headless meshing workflows
P16-REFMOD-001     Meshing reference models
P16-QUAL-001       Full P16 qualification
```

Do not start a milestone until its predecessor passes.

---

# P16-ARCH-001

## Meshing Architecture

**PASS 2026-09-30.** Evidence:
[docs/verification/P16-ARCH-001/](docs/verification/P16-ARCH-001/README.md),
[AUDIT.md](docs/verification/P16-ARCH-001/AUDIT.md),
[BACKEND_MATRIX.md](docs/verification/P16-ARCH-001/BACKEND_MATRIX.md),
[ADVERSARIAL_REVIEW.md](docs/verification/P16-ARCH-001/ADVERSARIAL_REVIEW.md).

Architecture only. **No mesh type exists, no `src/meshing/` exists, no backend has been
chosen, added or linked, and no meshing code was written.**

* [x] Audit all existing tessellation / triangulation code — `geometry::triangulate()` and
      **one** production consumer, STL export. No other `BRepMesh` or `Poly_Triangulation`
      user anywhere. `src/renderer/` holds one file: `.gitkeep`
* [x] Audit OCCT meshing capabilities already used by BetterCAD — `BRepMesh_IncrementalMesh`
      in exactly one place, on a deliberate shape COPY so nothing is cached, `InParallel`
      already off for determinism
* [x] Audit any existing volume-mesh capability — **NONE**, and OCCT 8.0.1 ships none: the
      14 headers matching "tet" are DateTime, Trihedron and Tangence
* [x] Audit current geometry-validity APIs — `Body::isValid()` (`BRepCheck_Analyzer`, used in
      exactly two places), `TopologySummary`; `ShapeFix`/`ShapeAnalysis` used nowhere, so the
      tree does no healing. P16 defines no second validity notion
* [x] Audit stable geometry-reference infrastructure — `FaceName` is generative, not
      positional, and `listFaces()` already returns each face's name SET, propagated through
      booleans by kernel history. **`FaceId` is declared and used nowhere** — a P21
      placeholder P16 must not adopt
* [x] Audit configuration/regeneration interaction — the carried defect and P15-MASS-001's
      refusal guard, reused unchanged. `Document::revision()` is **monotonic** — every write
      is `++revision_` and undo increments it too, so there is no ABA hazard
* [x] Define surface-mesh vs display-tessellation boundary — ADR-030/033. There is no display
      tessellation yet; OCCT's own `Poly_MeshPurpose` already separates Calculation from
      Presentation. `Poly_MergeNodesTool` is NOT the welding route: its own header says it
      splits nodes at sharp corners, the opposite of watertight
* [x] Define volume-mesh backend interface — ADR-033: backend-neutral, no backend type in any
      API, code only in `src/meshing/<backend>/`, **enforced by a new architecture rule**
* [x] Decide initial supported element types — Tet4 only. Sufficient for the P17 pipeline;
      **not** sufficient for accurate bending stress, recorded as a known limitation, which is
      why an element carries its type from day one
* [x] Define canonical meshing controls — ADR-030: a `MeshControl` document object holding
      intent only. Field detail is P16-DATA-001's and P16-SIZE-001's
* [x] Define canonical vs derived mesh state — ADR-030. `ARCHITECTURE.md` already declared
      simulation meshes derived; this details it
* [x] Define mesh ownership — a `Mesher` service, exactly as the `Regenerator` owns derived
      bodies. Not the Document, not the GUI
* [x] Define mesh lifetime / invalidation — a build stamp of three revisions plus upstream
      dirtiness, deliberately conservative: it may narrow with measurement, never widen. A
      mesh cannot go stale on disk because it is never on disk
* [x] Define NodeId semantics — ADR-031: a handle into one generation of one mesh, and
      deliberately **not** `bettercad::Id`, whose documented contract is stable persisted
      identity with never-reused values. A stale handle is refused, not reinterpreted
* [x] Define ElementId semantics — the same, and a distinct type from `NodeId`
* [x] Define geometry-selection mapping strategy — ADR-032: a selection names CAD geometry and
      **never** a mesh entity, so it survives a remesh definitionally. The face-to-name
      relation is stored as the **many-to-many** relation it is
* [x] Define material-region boundary — P16 holds no material data at all (ADR-028). One
      material per document today; resolved at consumption through P15's require* entry points
* [x] Define transformed-body behaviour — a mesh is in its body's frame and carries no
      transform; patterns and mirrors bake theirs in during regeneration. `Placement` is
      rotation and translation with **no scale field**, so a future occurrence placement is
      rigid and cannot change a quality metric
* [x] Define configuration behaviour — REFUSE, reusing P15-MASS-001's guard unchanged, while
      the carried regeneration defect stands
* [x] Define persistence boundary — controls only. No nodes, elements, facets, quality numbers
      or backend version in a `.bcad` file
* [x] Define downstream P17 consumer contract — P17 receives a `ValidatedMesh` or a structured
      diagnostic, and **cannot** obtain an unvalidated mesh for computation. Enforced by the
      type, not by a comment — this was adversarial finding F5
* [x] Define module / dependency layering — `meshing` at layer 4, sharing it with `drawing`,
      with **no renumber**, unlike ADR-006 and ADR-015. Registered in the enforced table, and
      `ARCHITECTURE.md`'s mirror updated to match its source of truth
* [x] Record required ADRs — ADR-030, ADR-031, ADR-032, ADR-033
* [x] Architecture adversarial review PASS — 6 findings, **5 found while the architecture was
      being written and fixed in the ADRs**, 1 residual assigned to P16-DATA-001/P16-CMD-001.
      0 new production defects
* [x] Evidence recorded — with a three-preset clean regression, because the milestone changed
      production code

**What this milestone found.** The audit changed the design four times, which is why the brief's
order — audit before library, before code — was worth following.

The finding that decided the backend is not about meshers: **BetterCAD has no licence.** `LICENSE`
grants no permission to distribute, and everything shipped is weak copyleft, dynamically linked.
So a GPL or AGPL mesher would not add a dependency, it would DECIDE BetterCAD's unchosen licence.
Applying the project's existing obligations as an admission rule leaves exactly one candidate
(Netgen, LGPL-2.1); CGAL's meshing package is GPL despite cgal.org's LGPL headline, and MMG's own
licence says it MODIFIES meshes rather than generating them. **Admitting the dependency is a
project decision, so it is deliberately not taken here** — `P16-VOL-001` carries it as an entry
condition, and `P16-DATA-001` through `P16-SURF-001` do not depend on it.

One production defect class was closed: P16's invariant that backend behaviour stay behind the
meshing interface was **unenforceable**. The OCCT containment rule keys on the `.hxx` extension
and every candidate backend ships `.h` headers, so no rule would have fired on a backend include
anywhere in the tree. Rule 5 was added, given a permanent fixture test, and **proven to fire**
before being trusted.

Two would-be defects were caught before they were written: the attribution rule as first drafted
would have **refused to mesh a box** (primitives take no namer, so their faces carry no name), and
`optional<FaceName>` per facet would have been wrong in both directions after the first boolean.

### Questions that must be answered

```text
Does BetterCAD already contain an engineering mesher?

Is existing triangulation display-only or engineering-grade?

What generates a closed surface mesh?

What generates a volume mesh?

Which element types are supported first?

Are Tet4 elements sufficient for P17 foundation?

Who owns generated mesh state?

When exactly is a mesh invalidated?

Can a mesh survive geometry regeneration?

Are NodeId and ElementId stable across remesh?

How are boundary facets associated with CAD geometry?

Can selections survive remesh?

How are holes and internal voids represented?

What happens with multiple disconnected solids?

What happens with transformed geometry?

What happens under a configuration change?

How does P17 request a mesh?

What is persisted?

What is recomputed?

What backend-specific state is forbidden from leaking into core APIs?
```

### Gate

```text
meshing architecture coherent
+ backend boundary explicit
+ CAD/mesh authority boundary explicit
+ mesh lifetime defined
+ identity semantics defined
+ geometry mapping contract defined
+ P17 consumer boundary defined
+ persistence boundary defined
+ configuration safety defined
+ ADRs complete
```

### Evidence

```text
docs/verification/P16-ARCH-001/
```

### Stop condition

Do not start `P16-DATA-001` until every architecture question is answered.

---

# P16-DATA-001

## Mesh Data Model / Identity / Units

**PASS 2026-09-30.** Evidence:
[docs/verification/P16-DATA-001/](docs/verification/P16-DATA-001/README.md),
[API_AUDIT.md](docs/verification/P16-DATA-001/API_AUDIT.md),
[ADVERSARIAL_REVIEW.md](docs/verification/P16-DATA-001/ADVERSARIAL_REVIEW.md).

`src/meshing/` exists for the first time, which is what makes `layer_meshing 4` — registered by
`P16-ARCH-001` — load-bearing rather than declarative. **Nothing here generates a mesh.**

* [x] Define `NodeId` — a concrete strong type in `bettercad::meshing` over `uint32_t`, and
      deliberately **not** `bettercad::Id`. The brief suggested `Id<NodeIdTag>`; ADR-031 forbids
      it, because that template's documented contract is stable, persisted, never-reused identity
      and a mesh handle is the opposite on all four counts. No second generic ID template was
      introduced either, so both constraints hold
* [x] Define `ElementId` — the same, one identity space across `Triangle3` and `Tetrahedron4`
* [x] Define optional `RegionId` — **IMPLEMENTED, not N/A.** ADR-032's "shares no node between
      regions" is uncheckable unless an element says which region it is in. A strong type, never
      an `int`, and it carries no material value (ADR-028)
* [x] Define node coordinate representation — `Point3D {Length x, y, z}`, the existing core type
      reused, which `core/geometry/Mesh.hpp` already uses. No `gp_Pnt` anywhere
* [x] Coordinates use strong length quantities — `Length` per component, SI internally. Signed
      volume returns `Volume` (L³) and area returns `Area` (L²), so the dimension is checked
      rather than asserted in a comment
* [x] Define triangle connectivity — three distinct handles, orientation by the right-hand rule
      on the **stored** order, never canonicalised
* [x] Define tetrahedral connectivity — four distinct handles, same rule
* [x] Define element type enum — `Triangle3`, `Tetrahedron4`, and nothing else. `Tet10`, `Hex8`
      and `Wedge6` are **absent rather than reserved**: an enumerator with no arity, no
      orientation and no validation would be a promise the code does not keep
* [x] Define element orientation convention — `V = 1/6 (p2-p1)·((p3-p1)×(p4-p1))`, **positive is
      valid** (ADR-032). Negative is REJECTED, never renormalised: ADR-032 says a violation is a
      failure, and reordering destroys the only evidence a generator produced an inverted
      element. **No `abs()` exists in the module**
* [x] Define mesh-local identity semantics — valid for one generation of one mesh. A `MeshStamp`
      lives on the mesh, not on every handle, and `Mesh::owns()` refuses a foreign stamp, the
      default stamp, and the same `MeshId` with a bumped generation
* [x] Define deterministic node enumeration — ascending `NodeId`. Handles are **strictly
      increasing**, which makes a duplicate *unrepresentable* rather than merely rejected, keeps
      storage contiguous, allows gaps, and therefore forces lookup through identity: `findNode`
      is a binary search and never `nodes_[id.value()]`
* [x] Define deterministic element enumeration — ascending `ElementId` per kind; canonically
      every `Triangle3` then every `Tetrahedron4`. **No unordered container exists in the
      module**
* [x] Define immutable/read-only solver-facing mesh view — every accessor is `const` and returns
      `std::span<const T>`; `build()` returns by value. Two compile-fail cases prove a consumer
      can neither move a node nor insert an element. **No `ValidatedMesh`**: ADR-030 gives that
      to the mesher's validating path, and the report method is `dataValid()`, because naming a
      data-level check "solver ready" is the conflation this phase exists to prevent
* [x] Reject invalid connectivity — invalid handle, repeated handle, missing node, missing
      region, and an under-filled braced list (which the compiler *cannot* catch: it is valid
      aggregate initialisation that zero-fills)
* [x] Reject repeated node references — reported as the connectivity defect directly, not
      indirectly as a zero measure
* [x] Reject nonexistent node references — `NotFound`, and never resolved by vector position.
      Pinned with sparse handles 1, 4, 10, where indexing would silently return a different node
* [x] Reject degenerate elements — zero area, zero signed volume, **and a non-finite
      determinant from finite coordinates**, which is tested at 1e300 because an infinity would
      answer a `> 0` comparison with a yes
* [x] Validate positive tetrahedron volume — against hand-computed references: +1/6 for the
      reference tet, −1/6 swapped, 0 coplanar, +25/6 non-axis-aligned, invariant under
      translation and a 37° rotation, sign-flipped under reflection
* [x] Validate finite coordinates — NaN, +Inf and −Inf on each of x, y and z, nine cases, at
      insertion **and** again in whole-mesh validation
* [x] Define mesh bounds — `optional<MeshBounds>`; **nullopt for an empty mesh**, because a zero
      box at the origin is the right answer for a mesh holding one node at the origin and the two
      must stay distinguishable
* [x] Define adjacency foundation — **N/A, derived and deferred.** Nothing needs it: the one
      global check that might have, no-node-shared-between-regions, is computed locally inside
      `validate()` and exposes no structure
* [x] Compile-time/API safety tests — 15 cases, each with a control target that compiles so every
      failure is attributable to its own line. The three that matter most are
      `node-id-as-object-id`, `element-id-as-object-id` and `region-id-as-object-id`: ADR-031's
      central invariant, enforced by the compiler instead of by review
* [x] Determinism PASS — 60 tests × 5 in `release-ext` and `debug-ext`, 0 failures. In process, a
      mesh is rebuilt 8 times and the whole handle sequence, bounds and report compared, and
      reports are compared 8 times **including their messages**
* [x] Adversarial review PASS — 23 attacks from the brief plus 5 more; **7 findings, 0 production
      defects**, 1 defect in shared tooling, 1 deferral documented at its declaration
* [x] Regression PASS — 2875/2875 in `debug-ext`, `release-ext` and `debug-shared-ext` each from
      clean; 9225 executions; 0 failures; **0 compiler warnings in all six build and rebuild
      logs**; no rebuild recompiled or relinked anything; the eight qualified tree IDs identical
      before the first build and after the last test run
* [x] Evidence recorded

**What this milestone found.** No production defects, and two findings worth more than a clean
sheet.

**Reversing a tetrahedron's connectivity does not invert it.** A fixture expecting two inverted
tetrahedra got one, and the code was right: `(1,2,3,4) → (4,3,2,1)` is the permutation `(1 4)(2 3)`
— two transpositions, an **even** permutation — so the signed volume is unchanged. This is a trap
in the domain, not just in a test: **a mesher author "repairing" inverted elements by reversing
their connectivity accomplishes nothing.** the permutation-parity test now pins reversal, one swap and a 3-cycle against hand computation.

**The qualification harness could not express this milestone's own subject.** All three presets
passed and the run then died with `else was unexpected at this time` **before the determinism
stage**: `%REPEAT%` is substituted when cmd *parses* the enclosing `for` block, so a filter
containing `|`, `(` or `)` closes the block early. Latent since P15-QUAL-001, and it survived
three phases only because every earlier subject was a single word. Fixed with delayed expansion,
`verify-harness.cmd` re-run, and the exact filter proven to parse before spending another two
hours.

Two of my own compile-fail tests were also wrong in ways that would have passed for the wrong
reason: one case **could not fail** (an under-filled braced list is valid aggregate
initialisation), and one regex asked for `could not convert` where GCC emits `cannot convert`.

### Core tetrahedron relationship

For nodes:

```text
x1, x2, x3, x4
```

signed volume:

```text
Vtet =
1/6 det(
    x2 - x1,
    x3 - x1,
    x4 - x1
)
```

The orientation convention must make the valid sign explicit.

### Identity rule

```text
CAD ObjectId
!=
NodeId
!=
ElementId
```

And:

```text
remesh
→ NodeId / ElementId may change
```

No downstream solver may treat mesh-local IDs as permanent CAD identities.

### Gate

```text
mesh representation strong
+ connectivity valid
+ element orientation explicit
+ invalid elements rejected
+ mesh identity separated from CAD identity
+ deterministic representation
```

---

# P16-GEOM-001

## Geometry Preparation / Validity / Regeneration Boundary

* [ ] Mesh only authoritative regenerated geometry
* [ ] Reject missing body
* [ ] Reject failed regeneration
* [ ] Reject blocked regeneration
* [ ] Reject stale derived geometry
* [ ] Validate closed-solid requirement
* [ ] Validate shell/open-solid failure behaviour
* [ ] Validate multiple-solid behaviour
* [ ] Validate internal holes / voids
* [ ] Validate transformed geometry
* [ ] Define tolerance source
* [ ] Audit shape-healing requirements
* [ ] Do not silently heal engineering geometry unless contract declares it
* [ ] Detect zero-volume / degenerate bodies
* [ ] Define configuration behaviour
* [ ] Protect against known configuration-regeneration defect
* [ ] Geometry fingerprint / revision foundation for mesh invalidation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical stale-geometry rule

```text
configuration effective value changes
+
geometry not regenerated
→ meshing must REFUSE
```

Forbidden:

```text
stale geometry
→ new mesh reported as current
```

Once the carried regeneration defect is fixed, replace refusal tests with tests
proving the mesh follows configuration geometry.

### Gate

```text
mesh source is authoritative geometry
+ stale geometry rejected
+ invalid/open volume rejected
+ transformations correct
+ holes/voids preserved
+ configuration behaviour safe
```

---

# P16-SURF-001

## Engineering Surface Mesh

* [ ] Generate engineering surface triangulation
* [ ] Keep separate from viewer/display tessellation
* [ ] Triangle node connectivity valid
* [ ] Triangle orientation defined
* [ ] Surface normals consistent
* [ ] Closed-solid surface is watertight
* [ ] No duplicate zero-area triangles
* [ ] Reject degenerate triangles
* [ ] Validate curved surfaces
* [ ] Validate planar faces
* [ ] Validate cylindrical faces
* [ ] Validate holes
* [ ] Validate sharp edges
* [ ] Validate transformed bodies
* [ ] Validate disconnected solids if in scope
* [ ] Surface area consistency check
* [ ] Boundary-edge count validation
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Required distinction

```text
Display triangulation
→ visualisation

Engineering surface mesh
→ numerical meshing / solver boundary
```

One may reuse backend machinery.
They must not silently share quality assumptions or authority.

### Gate

```text
surface triangles valid
+ orientation coherent
+ watertight where required
+ engineering/display roles separated
+ holes and curved faces represented
+ deterministic generation
```

---

# P16-VOL-001

## 3D Volume Meshing

Initial required element:

```text
Tet4 — 4-node linear tetrahedron
```

unless P16-ARCH proves another minimum is more appropriate.

* [ ] Generate tetrahedral mesh from valid closed solid
* [ ] Every element references valid nodes
* [ ] Every tetrahedron has positive qualified volume
* [ ] No inverted tetrahedra
* [ ] No zero-volume tetrahedra
* [ ] No duplicate tetrahedra
* [ ] Boundary conforms to engineering surface
* [ ] Internal voids remain void
* [ ] Mesh occupies solid volume
* [ ] Nodes remain inside/on valid geometry within tolerance
* [ ] Element volumes approximately recover CAD volume
* [ ] Validate disconnected solid policy
* [ ] Validate transformed body
* [ ] Validate small feature behaviour
* [ ] Backend failures propagate explicitly
* [ ] Determinism measured
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Volume conservation check

```text
Vmesh = Σ Ve
```

Compare with authoritative CAD volume:

```text
error =
|Vmesh - Vcad| / Vcad
```

The acceptable tolerance must be justified by the meshing representation.
Do not simply choose a loose tolerance to pass.

### Gate

```text
volume mesh generated
+ all elements valid
+ no inverted elements
+ holes/voids preserved
+ CAD volume agreement validated
+ backend failures explicit
+ solver-ready connectivity
```

---

# P16-SIZE-001

## Global / Local Mesh Sizing

* [ ] Define global target element size
* [ ] Define minimum size if architecture requires it
* [ ] Define maximum size if architecture requires it
* [ ] Define growth-rate control if backend supports it
* [ ] Define curvature control if backend supports it
* [ ] Define local geometry sizing foundation
* [ ] Local sizing references stable geometry selections
* [ ] Validate conflicting sizing controls
* [ ] Define precedence rules
* [ ] Reject non-positive sizes
* [ ] Reject NaN / infinity
* [ ] Unit-safe length inputs
* [ ] Validate global coarse/fine behaviour
* [ ] Validate local refinement behaviour
* [ ] Validate controls survive geometry regeneration where reference remains valid
* [ ] Invalid selection becomes explicit unresolved control
* [ ] No hidden backend default changes engineering meaning
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Required behaviour

For the same geometry:

```text
smaller target size
→ generally more nodes/elements
```

But do not qualify solely by element count.
Also validate geometry conformity and quality.

### Gate

```text
sizing intent explicit
+ invalid sizing rejected
+ precedence deterministic
+ local refinement targets correct region
+ no hidden semantic backend defaults
```

---

# P16-QUALITY-001

## Mesh Quality Metrics / Validation

Define metric conventions before using thresholds.

At minimum audit/support:

```text
tetrahedron signed volume / Jacobian
edge-length statistics
aspect-ratio metric
radius-ratio or equivalent shape metric
minimum dihedral angle where backend/data supports it
maximum dihedral angle where useful
surface triangle quality
```

* [ ] Define every metric mathematically
* [ ] Define good/bad direction for each metric
* [ ] Define valid numeric range where applicable
* [ ] Compute mesh-level min/max/mean where useful
* [ ] Identify worst element
* [ ] Reject inverted elements
* [ ] Reject zero-volume elements
* [ ] Structured quality report
* [ ] Threshold policy explicit
* [ ] Warning vs failure semantics explicit
* [ ] Validate regular tetrahedron analytically
* [ ] Validate intentionally poor tetrahedron
* [ ] Validate sliver element
* [ ] Validate distorted surface triangle
* [ ] Quality report deterministic
* [ ] No automatic "repair" without explicit contract
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Independent reference

A regular tetrahedron must be used to validate metric implementations wherever
closed-form values exist.

Do not use the production quality function to create expected values.

### Gate

```text
quality metrics mathematically defined
+ inverted/degenerate detection PASS
+ poor elements detectable
+ structured report PASS
+ threshold semantics explicit
+ independent analytical checks PASS
```

---

# P16-MAP-001

## Geometry ↔ Mesh Correspondence / Regions

* [ ] Map boundary triangles to originating CAD face where possible
* [ ] Define mapping to edges where required
* [ ] Define mapping to volume region
* [ ] Preserve current stable-reference semantics
* [ ] Do not pretend P21 Semantic Topology already exists
* [ ] Define behaviour after remesh
* [ ] Define behaviour after topology-changing model edit
* [ ] Define unresolved geometry reference state
* [ ] Define named boundary-set foundation
* [ ] Define node-set / element-set foundation if required by P17
* [ ] Selection → mesh-facet query
* [ ] Mesh facet → source geometry query
* [ ] Validate cylindrical face mapping
* [ ] Validate planar face mapping
* [ ] Validate hole-wall mapping
* [ ] Validate transformed geometry
* [ ] Validate local sizing uses same mapping contract
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical boundary

P16 may guarantee:

```text
current regenerated geometry reference
↔ current generated mesh entities
```

P16 must NOT claim:

```text
topology-changing future model edit
→ semantic face identity preserved forever
```

That broader problem belongs to P21 Semantic Topology unless existing BetterCAD
stable-reference infrastructure already solves it.

### Gate

```text
geometry/mesh correspondence explicit
+ current boundary mapping correct
+ unresolved references explicit
+ solver boundary sets possible
+ no false permanent-topology guarantee
```

---

# P16-VIZ-001

## Mesh Visualisation / Inspection

* [ ] Display surface mesh
* [ ] Display volume-mesh boundary
* [ ] Optional interior element inspection
* [ ] Wireframe / edge display
* [ ] Node inspection
* [ ] Element inspection
* [ ] Element ID display
* [ ] Quality inspection
* [ ] Worst-element navigation
* [ ] Boundary-region highlighting
* [ ] CAD ↔ mesh selection linkage
* [ ] Mesh visibility toggle
* [ ] Clear stale-mesh visual state
* [ ] Distinguish current vs invalidated mesh
* [ ] Do not duplicate canonical mesh data in GUI
* [ ] Rendering does not mutate mesh
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
mesh inspectable
+ stale mesh obvious
+ quality defects inspectable
+ CAD/mesh mapping visible
+ GUI remains adapter over core mesh state
```

---

# P16-CMD-001

## Commands / Undo / Redo

Canonical history contains:

```text
meshing controls
local sizing intent
boundary/region intent
```

Canonical history does NOT contain generated mesh as authority.

* [ ] Create meshing-settings command
* [ ] Edit global sizing command
* [ ] Add local sizing command
* [ ] Edit local sizing command
* [ ] Remove local sizing command
* [ ] Boundary/region command where required
* [ ] Undo restores exact meshing intent
* [ ] Redo restores exact post-command intent
* [ ] Failed commands atomic
* [ ] Redo invalidation correct
* [ ] Geometry references preserved
* [ ] Generated mesh invalidated after control change
* [ ] Generated mesh recomputed after undo/redo when requested
* [ ] No node/element arrays stored as canonical command history
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical rule

```text
edit mesh size
→ undo history stores size intent

NOT

edit mesh size
→ undo history stores 100,000 generated tetrahedra
```

### Gate

```text
commands mutate canonical meshing intent
+ undo exact
+ redo exact
+ failed mutations atomic
+ generated mesh excluded as authority
```

---

# P16-PERSIST-001

## Meshing Intent Persistence

Persist:

```text
global meshing settings
local sizing controls
geometry references
named region/boundary intent
backend-independent options that are canonical
```

Do NOT persist generated mesh as engineering authority.

* [ ] Define persisted meshing schema
* [ ] Persist global controls
* [ ] Persist local controls
* [ ] Persist geometry-selection references
* [ ] Persist boundary/region definitions
* [ ] Preserve units
* [ ] Preserve unresolved references explicitly
* [ ] Exclude generated nodes/elements as authority
* [ ] Validate malformed files
* [ ] Validate invalid sizes rejected
* [ ] Validate duplicate control IDs if IDs exist
* [ ] Validate deterministic serialization
* [ ] Validate backward compatibility
* [ ] Validate full round trip
* [ ] Regenerate mesh after load
* [ ] Compare regenerated mesh semantics
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Derived-state rule

```text
save
→ meshing intent

load
→ regenerate mesh from current geometry + intent
```

If an optional mesh cache is ever introduced:

```text
cache must carry geometry/settings fingerprint
+ cache is disposable
+ cache is never canonical
```

Do not introduce that cache unless measured performance justifies it.

### Gate

```text
meshing intent preserved
+ geometry references preserved
+ units preserved
+ derived mesh excluded as authority
+ malformed files rejected
+ deterministic serialization PASS
```

---

# P16-CLI-001

## Headless Meshing Workflows

At minimum provide commands equivalent to:

```text
mesh settings
mesh generate
mesh info
mesh quality
mesh validate
mesh boundaries
```

Use actual BetterCAD CLI naming conventions.

* [ ] CLI inspect meshing settings
* [ ] CLI set global mesh size
* [ ] CLI add local sizing
* [ ] CLI remove local sizing
* [ ] CLI generate mesh
* [ ] CLI query node count
* [ ] CLI query element count
* [ ] CLI query element types
* [ ] CLI query volume
* [ ] CLI quality report
* [ ] CLI validation report
* [ ] CLI geometry/boundary mapping query
* [ ] Structured diagnostics
* [ ] Correct process exit codes
* [ ] Missing/stale geometry failures propagate
* [ ] Invalid controls fail non-zero
* [ ] CLI/core mesh equivalence
* [ ] Multi-process save/load/remesh workflow
* [ ] No CLI-only meshing semantics
* [ ] Fresh-binary proof
* [ ] Zero-match test-filter protection
* [ ] End-to-end scripted workflow PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
CLI uses core meshing APIs
+ core/CLI mesh results equivalent
+ quality results equivalent
+ diagnostics structured
+ failures propagate
+ no CLI-only meshing implementation
```

---

# P16-REFMOD-001

## Meshing Reference Models

Define a committed qualification suite.

```text
RM-MESH-01   Rectangular block
RM-MESH-02   Cylinder
RM-MESH-03   Plate with through-hole
RM-MESH-04   Hollow tube
RM-MESH-05   Thin-feature / aspect-ratio challenge
RM-MESH-06   Transformed asymmetric solid
RM-MESH-07   Local-refinement model
RM-MESH-08   Invalid/open geometry failure
```

Add configuration case if safe under the final configuration/regeneration
contract.

* [ ] Define reference suite
* [ ] Rectangular block
* [ ] Cylinder
* [ ] Plate with hole
* [ ] Hollow tube
* [ ] Thin-feature case
* [ ] Transformed body
* [ ] Local refinement case
* [ ] Invalid/open geometry case
* [ ] Validate element orientation
* [ ] Validate positive element volumes
* [ ] Validate CAD-volume agreement
* [ ] Validate boundary conformity
* [ ] Validate holes/voids
* [ ] Validate local sizing
* [ ] Validate quality metrics
* [ ] Validate geometry ↔ mesh mapping
* [ ] Validate model-change remeshing
* [ ] Validate settings-change remeshing
* [ ] Validate save/load/regenerate
* [ ] Validate CLI
* [ ] Independent analytical validation
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

### RM-MESH-01 — Rectangular Block

Use simple dimensions with exact analytical volume.

```text
Vcad = abc
```

Validate:

```text
closed boundary
positive Tet4 volumes
Σ Ve ≈ abc
correct face mapping
deterministic quality report
```

Use an asymmetric block so orientation/axes bugs are not hidden.

### RM-MESH-02 — Cylinder

```text
Vcad = πr²h
```

Validate:

```text
curved-boundary conformity
end-face mapping
cylindrical-wall mapping
volume convergence
```

### RM-MESH-03 — Plate With Through-Hole

Analytical volume:

```text
Vcad =
L W t
-
πr²t
```

Required:

```text
hole remains empty
hole wall receives boundary facets
outer plate remains connected
mesh volume follows analytical solid
```

Forbidden:

```text
mesher fills the hole
```

### RM-MESH-04 — Hollow Tube

```text
Vcad =
π(Ro² - Ri²)h
```

Validate:

```text
inner void preserved
inner wall mapped separately from outer wall
positive element volumes
```

### RM-MESH-05 — Thin Feature

Use deliberately challenging geometry.

Validate:

```text
mesher either
A. produces valid mesh meeting declared quality policy

or
B. fails explicitly with structured diagnostic
```

Never accept an inverted/sliver-invalid mesh silently.

### RM-MESH-06 — Transformed Asymmetric Solid

Generate local mesh and transformed-body mesh.

Validate:

```text
same topology/counts where backend contract supports it
node positions follow transformation
element volumes unchanged under rigid transform
quality invariant under rigid transform
geometry mapping preserved
```

### RM-MESH-07 — Local Refinement

Apply local sizing to a known geometric region.

Require:

```text
target region refined
non-target region not globally collapsed to fine size
boundary mapping remains correct
quality remains acceptable
```

Do not qualify local refinement only by total element count.

### RM-MESH-08 — Invalid Geometry

Use:

```text
open shell
or
failed/stale body
```

Required:

```text
mesh generation FAILS explicitly
```

Forbidden:

```text
0-element "successful" mesh
```

### P16 Reference Validation

For every valid reference model record:

```text
CAD volume
mesh volume
relative volume error

node count
element count
minimum element volume

quality minimum
quality maximum
worst element ID

boundary facet count
geometry mapping result
```

Where meaningful also record:

```text
coarse mesh
medium mesh
fine mesh
```

and show geometric approximation behaves consistently.

Do not declare convergence from element count alone.

---

# P16-QUAL-001

## Full P16 Qualification

* [ ] Freeze final P16 tree
* [ ] Audit every P16 milestone
* [ ] Verify all P16 TODO items complete
* [ ] Verify all P16 ADRs
* [ ] Audit mesh architecture
* [ ] Audit CAD/mesh authority boundary
* [ ] Audit mesh identity
* [ ] Audit surface mesh
* [ ] Audit volume mesh
* [ ] Audit sizing controls
* [ ] Audit quality metrics
* [ ] Audit geometry mapping
* [ ] Audit stale-geometry protection
* [ ] Audit configuration behaviour
* [ ] Audit visualisation
* [ ] Audit undo / redo
* [ ] Audit persistence
* [ ] Audit CLI
* [ ] Re-run all reference models
* [ ] Independent analytical volume validation
* [ ] Validate holes / voids
* [ ] Validate transformed geometry
* [ ] Validate local refinement
* [ ] Validate invalid geometry failures
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Cross-preset equivalence
* [ ] Final adversarial review
* [ ] Confirm 0 unexpected warnings
* [ ] Confirm qualified tree == committed tree
* [ ] Evidence in `docs/verification/P16-QUAL-001/`
* [ ] Mark P16 qualified

### P16 Final Gate

```text
all P16 milestones PASS

+ meshing architecture PASS
+ CAD geometry remains authority
+ generated mesh remains derived
+ stale geometry protection PASS

+ mesh data model PASS
+ NodeId / ElementId semantics PASS

+ surface mesh PASS
+ volume mesh PASS
+ positive element volume PASS
+ no inverted elements PASS

+ sizing controls PASS
+ local refinement PASS

+ quality metrics PASS
+ invalid mesh detection PASS

+ geometry ↔ mesh mapping PASS
+ boundary-region foundation PASS

+ visual inspection PASS

+ undo/redo PASS
+ generated mesh excluded from history

+ persistence PASS
+ generated mesh excluded as persisted authority

+ CLI PASS
+ CLI/core equivalence PASS

+ reference models PASS
+ independent analytical validation PASS

+ holes/voids PASS
+ transformed geometry PASS

+ Debug PASS
+ Release PASS
+ Debug-shared PASS

+ determinism PASS
+ cross-preset equivalence PASS
+ adversarial review PASS
+ 0 unexpected warnings

+ qualified tree == committed tree
```

### P16 Final Adversarial Questions

At minimum attack:

```text
Can viewer tessellation accidentally become solver mesh?

Can a stale CAD body produce a nominally current mesh?

Can a failed regeneration leave an old mesh marked valid?

Can NodeId be mistaken for ObjectId?

Can ElementId survive a remesh when it should not?

Can an inverted tetrahedron be accepted?

Can zero-volume elements survive validation?

Can a hole be silently filled?

Can a hollow tube lose its void?

Can local sizing attach to the wrong face after regeneration?

Can an unresolved geometry reference silently target another face?

Can backend defaults change engineering meaning?

Can changing mesh size leave the old mesh current?

Can undo restore generated mesh instead of meshing intent?

Can save/load trust stale node/element arrays?

Can CLI implement its own meshing algorithm?

Can CLI report a successful zero-element mesh?

Can mesh quality ordering differ across presets?

Can a transformed body change tetrahedron volume?

Can surface orientation flip unpredictably?

Can a zero-test filter pass qualification?

Can stale binaries generate apparently valid evidence?

Can a tracked change occur after final qualification?
```

Every credible defect:

```text
reproduce
→ regression test
→ root cause
→ general fix
→ requalify affected milestone
→ rerun P16-QUAL
```

### P16 Final Evidence

Create:

```text
docs/verification/P16-QUAL-001/
```

Include at minimum:

```text
README.md

FINAL HEAD
FINAL TREE
origin/main
working-tree state

milestone audit
ADR audit

backend architecture
mesh data model
surface meshing
volume meshing
sizing controls
quality metrics
geometry correspondence
visualisation

commands
persistence
CLI

reference-model matrix
analytical volume tables
quality tables
determinism tables

Debug qualification
Release qualification
Debug-shared qualification

warning audit
adversarial review

qualified tree vs committed tree
known limitations
final result
```

### P16 Final Reference Matrix

```text
Model        Surface   Volume   Positive   CAD Volume   Mapping   Quality   Save/Load   CLI   PASS
                       Mesh     Elements   Agreement

RM-MESH-01
RM-MESH-02
RM-MESH-03
RM-MESH-04
RM-MESH-05
RM-MESH-06
RM-MESH-07
RM-MESH-08
```

Use:

```text
PASS
FAIL
N/A
```

Never mark unsupported behaviour PASS.

### P16 Completion

Only after the full qualification gate passes:

```text
P16-QUAL-001 → [x]

P16 — Meshing — QUALIFIED
```

Then:

```text
Next:
P17 — Structural FEA
```

Do NOT start P17 until P16 is fully qualified.

---

# Future Phases

```text
P16  Meshing                         CURRENT
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
P16-GEOM-001 — Geometry Preparation / Validity / Regeneration Boundary
```

`P16-DATA-001` passed on 2026-09-30: 2875/2875 in three presets from clean, 9225 executions, 0
failures, 0 warnings in all six logs, and the qualified tree identical to the committed tree.
Evidence: [docs/verification/P16-DATA-001/](docs/verification/P16-DATA-001/README.md).

The mesh data model now exists and is qualified. What `P16-GEOM-001` must honour, so it is not
re-derived:

```text
a mesh is BUILT, not mutated    MeshBuilder is the only construction path, and a
                                finished Mesh is immutable by API
handles are strictly increasing gaps allowed, duplicates unrepresentable, lookup
                                by identity and never by index
one frame, no transform         ADR-032. A mesh is in its body's frame
positive Jacobian               ADR-032. Inverted is REJECTED, never repaired
regions                         one per solid, no node shared between them
no CAD reference yet             attribution to a FaceName is P16-MAP-001's
```

What `P16-GEOM-001` owns, and the guard it already has:

```text
geometry validity      Body::isValid() and TopologySummary exist; P16 defines no
                       second validity notion
the regeneration
boundary               a mesh must never be built from stale geometry. The
                       configuration-override refusal in
                       src/features/material/MassProperties.cpp is the shape to
                       reuse UNCHANGED (ADR-030), and Document::revision() is
                       MONOTONIC -- every write is ++revision_ and undo
                       increments it too -- so a build stamp cannot match a
                       different state
the surface gap        geometry::triangulate() is watertight (15 assertions
                       across 11 files) but NOT topologically conforming: a
                       vertex on a shared edge appears once per face. Welding it
                       is P16-SURF-001's. Poly_MergeNodesTool is NOT the route --
                       it splits nodes at sharp corners
```

Open from the P16-ARCH-001 review, still owed:

```text
F6   a MeshControl whose body is deleted must become explicitly UNRESOLVED and
     must not mesh nothing and report success. P16-DATA-001 created no
     MeshControl, so this remains P16-GEOM-001's or P16-CMD-001's
```

Deferred from P16-DATA-001, with the reason recorded at its declaration:

```text
a topological duplicate -- two tetrahedra on the same four nodes -- is not
rejected. Which of the two is right is not answerable from connectivity alone,
one ordering may be inverted, and detecting the pair needs a canonical key over
node sets that is a whole-mesh question. P16-QUALITY-001's.
```

`P16-VOL-001` still carries an entry condition nothing before it does:

```text
the volume-meshing backend dependency is approved, or the fallback is chosen
deliberately. BetterCAD has NO licence, so admitting a copyleft backend
constrains one that has not been chosen -- a project decision, not Claude's.
See docs/verification/P16-ARCH-001/BACKEND_MATRIX.md.
```

Carried defects, neither P16's, and the first can lose a user's work:

```text
the FileIo replace defect     a document save can still lose to a file
                              synchroniser. Arguably ahead of any new milestone.

configuration regeneration    a configuration override does not rebuild the
                              geometry it changes. Mass properties REFUSE under
                              one, and P16 meshing will refuse for the same
                              reason with the same guard. Fixing the
                              regeneration removes the guard and its tests.
```

Also carried from earlier phases: hole POSITION dimensions are unsupported, GD&T symbols are
not fully embedded in PDF/DXF, and cross-preset export byte identity is not guaranteed for
drawings.
