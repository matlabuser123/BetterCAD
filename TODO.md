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
           P16-ARCH-001 — Meshing Architecture

Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data

Next:
           P16-ARCH-001

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

* [ ] Audit all existing tessellation / triangulation code
* [ ] Audit OCCT meshing capabilities already used by BetterCAD
* [ ] Audit any existing volume-mesh capability
* [ ] Audit current geometry-validity APIs
* [ ] Audit stable geometry-reference infrastructure
* [ ] Audit configuration/regeneration interaction
* [ ] Define surface-mesh vs display-tessellation boundary
* [ ] Define volume-mesh backend interface
* [ ] Decide initial supported element types
* [ ] Define canonical meshing controls
* [ ] Define canonical vs derived mesh state
* [ ] Define mesh ownership
* [ ] Define mesh lifetime / invalidation
* [ ] Define NodeId semantics
* [ ] Define ElementId semantics
* [ ] Define geometry-selection mapping strategy
* [ ] Define material-region boundary
* [ ] Define transformed-body behaviour
* [ ] Define configuration behaviour
* [ ] Define persistence boundary
* [ ] Define downstream P17 consumer contract
* [ ] Define module / dependency layering
* [ ] Record required ADRs
* [ ] Architecture adversarial review PASS
* [ ] Evidence recorded

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

* [ ] Define `NodeId`
* [ ] Define `ElementId`
* [ ] Define optional `RegionId` / boundary-set identity if required
* [ ] Define node coordinate representation
* [ ] Coordinates use strong length quantities or qualified canonical SI boundary
* [ ] Define triangle connectivity
* [ ] Define tetrahedral connectivity
* [ ] Define element type enum
* [ ] Define element orientation convention
* [ ] Define mesh-local identity semantics
* [ ] Define deterministic node enumeration
* [ ] Define deterministic element enumeration
* [ ] Define immutable/read-only solver-facing mesh view
* [ ] Reject invalid connectivity
* [ ] Reject repeated node references where invalid
* [ ] Reject nonexistent node references
* [ ] Reject degenerate elements
* [ ] Validate positive tetrahedron volume
* [ ] Validate finite coordinates
* [ ] Define mesh bounds
* [ ] Define adjacency foundation only if required
* [ ] Compile-time/API safety tests
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

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
P16-ARCH-001 — Meshing Architecture
```

Claude should begin by auditing the repository.

Do NOT begin by selecting or integrating a new meshing library.

First determine:

```text
what meshing/tessellation capability already exists
what is display-only
what can be reused
what P17 actually needs
what geometry-reference guarantees already exist
what determinism the available backend can provide
what persistence boundary is appropriate
```

Only then freeze the P16 architecture and proceed.
