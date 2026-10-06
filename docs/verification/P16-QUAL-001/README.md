# P16-QUAL-001 — Full P16 Qualification

```text
STATUS:   PASS
DATE:     2026-10-06
P16:      QUALIFIED
GATE:     all 13 P16 milestones PASS + architecture + CAD authority + derived
          mesh + stale protection + data model + handle semantics + surface +
          volume + positive orientation + sizing + local refinement + quality +
          invalid detection + mapping + boundary regions + visualisation +
          undo/redo + persistence + CLI + CLI/core equivalence + reference
          models + analytical validation + holes/voids + transformed geometry
          + three presets + determinism + cross-preset + adversarial review
          + 0 unexpected warnings + qualified tree == committed tree
```

```text
QUALIFICATION CANDIDATE   d7db08b74cf98c99ea31b69c56cb5be381d04872
SOURCE/TEST FINGERPRINT   8ab30a31695e79c3d02fe222898f8b1ec6274565
                          (the eight paths qualify.cmd records, recorded before
                          the first build and after the last test, unchanged)
QUALIFIED TREE METHOD     see QUALIFIED_TREE.md -- the eight-path fingerprint,
                          because docs/ is written after the run by design
```

```text
Netgen        6.2.2604          OCCT     8.0.1
compiler      GCC 16.1.0, WinLibs POSIX UCRT
build         CMake 4.4.1 / Ninja; debug-ext, release-ext, debug-shared-ext
              BETTERCAD_BUILD_ROOT outside the source tree
harness       qualify.cmd byte-identical to P16-SIZE-001's and to every P16
              milestone's since, 10 in a row ending with this one
              git hash-object d313a64070718c44fae290ac042fe259d1a03c8b
              It is NOT P15-QUAL-001's file (b44335cc): three %REPEAT% uses
              became !REPEAT! during P16, because a filter read without
              delayed expansion inside the preset loop selected nothing.
```

## What this milestone changed

**No production code.** The candidate's eight-path fingerprint differs from
P16-REFMOD-001's in `tests` alone:

```text
apps      b4972247  unchanged      examples  75ce1c68  unchanged
include   528df72c  unchanged      cmake     5a382115  unchanged
src       182a584b  unchanged      CMakeLists.txt / CMakePresets.json unchanged
tests     dfbb3003  MOVED
```

What moved: one new test file holding the six final cross-milestone gates, its
registration, and one more filter in the zero-match guard. The ADR-031
amendment is in `docs/`, outside the fingerprint by construction.

What moved outside the fingerprint, all of it documentation following evidence:

```text
docs/architecture/decisions/ADR-031-*.md   amended where the audit found the
                                           document disagreed with the code
docs/verification/P16-QUAL-001/            this directory
TODO.md                                    34 boxes ticked on PASS; the P16
                                           phase block (2148 lines of
                                           completed-milestone diary) archived
                                           to ROADMAP.md and here, which is
                                           TODO.md's own rule; CURRENT NEXT
                                           STEP rewritten, since it still
                                           described P16-VOL-001 as the
                                           frontier
ROADMAP.md                                 Meshing moved Planned -> Qualified
                                           with a 16-row evidence table; a
                                           stale duplicate "Materials /
                                           Engineering Data -- In Progress"
                                           heading removed
README.md                                  one false clause corrected; see
                                           Known limitations for what was left
```

That is what a qualification milestone should look like. The audit's job was to
find defects, and the one document it had to change was a document.

## Milestone audit

**15 of 15 PASS** — complete checkboxes, an evidence directory, a recorded
`RESULT: PASS`, and each milestone's own `qualification-times.txt` ending in
`0 stage(s) failed`, where the harness's exit code *is* the number of failed
stages.

```text
P16-ARCH-001  P16-DATA-001  P16-GEOM-001  P16-SURF-001  INFRA-NETGEN-001
P16-VOL-001   P16-SIZE-001  P16-QUALITY-001  P16-MAP-001  INFRA-VIEWER-001
P16-VIZ-001   P16-CMD-001   P16-PERSIST-001  P16-CLI-001  P16-REFMOD-001
```

The suite grew monotonically across the phase, 2814 → 3387, with no decrease —
a weak check that no milestone quietly removed tests, and labelled as weak.

Two apparent gaps were **my own extraction, not the evidence**, and both are
recorded: P16-MAP-001's figure (I matched its prerequisite audit quoting its
predecessor; its own is 3121/3121) and P16-CMD-001's harness line (I read the
deliberately kept `interrupted-run/` copy). Detail:
[MILESTONE_AUDIT.md](MILESTONE_AUDIT.md).

## TODO audit

```text
open boxes in the whole of TODO.md    34 as audited, all in # P16-QUAL-001;
                                      ticked on PASS, so 0 now remain
P16 sections with any open box         none but this milestone
BLOCKED / FIXME / known issue          3 mentions, all historical narrative
Current / Next                         consistent
```

## ADR audit

Four ADRs, 33 clauses checked against the code by search rather than by reading
the ADR back to itself. **32 implemented; 1 finding.**

ADR-031 says connectivity is "a variable-length span rather than
`array<NodeId, 4>`"; the code stores fixed arrays, which P16-DATA-001 chose
deliberately and recorded: *"Arity is part of the type, so an element with the
wrong number of handles is mostly a compile error."* CLAUDE.md puts verification
evidence above architecture, so the ADR was stale and **has been amended** — the
code is right. Every *invariant* of that decision holds, which is why this is
not a gate failure; a divergence touching an invariant would be an automatic
FAIL with no reconciliation available. Full reasoning:
[ADR_AUDIT.md](ADR_AUDIT.md).

## Architecture, CAD/mesh authority, mesh identity

Audited by searching for the **failure mode**, not the promise. A grep that
finds the reassuring comment proves nothing.

```text
generateTetrahedra outside a backend adapter       1 caller  VolumeMesh.cpp:283
EngineeringSurfaceMesh built outside meshing       0
nglib / Ng_* outside src/meshing/netgen/           0 files
abs() on a signed volume in meshing                0
NodeId/ElementId in src/io or include/bettercad/io 0
node/element/region tags in core/Id.hpp            0
nearest-face rebinding logic                       0
Netgen parameters transferred but not pinned       0 of 14
mesh compile-fail cases                            45
```

**The authority matrix as one executable statement**, which a table alone cannot
be: generating a mesh leaves the canonical document **byte-identical**, with the
object count and document revision unmoved. And the stronger form —

```text
RM-MESH-02 at 24 mm   114 nodes,  361 tets
RM-MESH-02 at  6 mm   376 nodes, 1977 tets
saved document        3347 BYTES EITHER WAY
```

A node array for the fine mesh alone would be 9024 bytes, nearly three times the
whole file. Detail: [ARCHITECTURE_AUDIT.md](ARCHITECTURE_AUDIT.md).

## Surface, volume, orientation, validity

```text
one approved path         CAD -> requireMeshableGeometry -> generateSurfaceMesh
                          -> generateTetrahedra -> generateVolumeMesh, with no
                          bypass reachable
element orientation       1751 positive, 0 zero, 0 negative, 0 non-finite,
                          computed in the TEST from node coordinates
minimum element volume    > 0 and finite everywhere; smallest 0.202694 mm^3
refusal is structural     validate() at VolumeMesh.cpp:414, InvalidMesh at 418,
                          volume summed only at 431
backend return code       never evidence: NoTetrahedra is a distinct failure and
                          the adapter reads Ng_GetNE/Ng_GetNP before believing
                          NG_OK -- tested against nglib's real behaviour
surface orientation       enclosed volume positive and identical BITWISE over 4
                          generations, winding identical triangle by triangle
```

## Sizing, backend defaults, local refinement

All **14** Netgen fields `Transfer_Parameters` copies are explicitly pinned, and
the three that would silently defeat a user's instruction — `uselocalh`, `minh`,
`grading` — carry the longest reasons in the source. Seven inert fields are
named so nobody "fixes" their absence.
[BACKEND_DEFAULTS.md](BACKEND_DEFAULTS.md).

Local refinement is qualified by a **mirror** — a third mesh refining the
opposite face — and never by total element count:

```text
at F   28.99 mm when F is refined   vs   40.00 mm when G is
at G   28.71 mm when G is refined   vs   42.02 mm when F is
```

## Quality and the regular-tetrahedron check

Under P16-QUALITY-001's own report-only policy; no reference-model threshold was
invented. The independent closed-form check for a unit regular tetrahedron
(`V = sqrt(2)/12`, `3r/R = 1`, internal dihedral `acos(1/3) = 70.5288°`) is
`QualityTet_RegularTetrahedronMatchesItsClosedFormMetrics`, and the ladder
regular → stretched → sliver → inverted → zero-volume is covered by
P16-QUALITY-001 and P16-DATA-001's own suites.

Nothing is hidden: RM-MESH-05's radius ratio is 1355× worse than RM-MESH-01's
and its maximum dihedral is 178.7°, all valid. And the thin plate is **not** the
worst mesh in the suite — the cylinder's chord-polygon boundary gives a lower
minimum dihedral. Tables: [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md).

## Mapping, holes, voids, transforms

```text
mapping                 every model complete: 0 unmapped, 0 attributed twice,
                        0 faces without facets, 0 UNNAMED faces
hole wall               72 facets, all answering in reverse with the hole
                        wall's name, all facing TOWARD the axis
tube walls              inner and outer disjoint, at radii 18 and 30 within
                        1e-9 mm, facing opposite ways
void occupancy          0 violations, on a radius derived from the declared
                        deflection, re-checked after RM-MESH-03's edit
rigid transform         CAD volume invariant to 1.225e-16; boundary nodes follow
                        R x + t below 1e-9 mm; the one interior node is the
                        backend's own choice at 2.66e-4 mm and is judged
                        separately
```

## Stale geometry and configuration

The whole stale contract in one sequence, including the case that matters most:
a regeneration that **fails** after a mesh was already current. Current → edit →
`StaleGeometry` → refused → failed rebuild → refused → recovered.

```text
CONFIGURATION BEHAVIOUR:  BLOCKED BY DESIGN
```

Meshing refuses under an active configuration override —
`ConfigurationOverrideActive` is the *first* check `requireMeshableGeometry`
makes — the diagnostic names which configuration and why, it is not a latch, and
it takes precedence over ordinary staleness so a user is not sent to fix the
wrong thing. Not marked PASS as a capability, because it is not one.
[CONFIGURATION.md](CONFIGURATION.md).

## Visualisation, undo/redo, persistence, CLI

```text
visualisation      a read-only adapter: the GUI owns no canonical node,
                   element, threshold, mapping or currentness logic, and
                   renderer::MeshScene refuses a MeshStamp mismatch
undo/redo          no command's API mentions a mesh, a node array or a handle;
                   four compile-fail cases say a definition has no nodes, no
                   elements, no quality report and no generated mesh
persistence        no mesh in the bytes of any of the nine reference documents;
                   the file is byte-identical for a 361-tet and a 1977-tet mesh
CLI                no nglib, no Ng_*, no mesh formula anywhere in
                   apps/bettercad_cli/, and meshing links Netgen PRIVATE
```

## Reference models

```text
expected 8    executed 8    documents 9    refusals 1
```

Full matrix with PASS / FAIL / N/A per property, and RM-MESH-08's unavailable
mesh properties kept separate from its expected-failure behaviour:
[REFERENCE_MATRIX.md](REFERENCE_MATRIX.md).

## Three-preset qualification

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0          (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3387/3387
release-ext            0       0      0         0           0   3387/3387
debug-shared-ext       0       0      0         0           0   3387/3387

REPEAT (5x each of 698 selected tests, back to back)
release-ext            0                            698/698   694.73 s
debug-ext              0                            698/698   635.73 s

warnings, all three clean builds   0   (-Werror, 596 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       9 DLLs linked
```

17 stages, 2 h 31 min, one uninterrupted detached run: 17:40:10 to 20:11:24
(the superseded first candidate took 2 h 23 min; see F5).

**Cross-preset equivalence**: 3387/3387 in each, and the reference suite's CLI
fixtures assert exact node, element and facet counts and exact SI volume doubles
that pass under `-O0 -g`, under the optimiser and across a DLL boundary. For
these models Netgen's output is byte-deterministic across presets — recorded as
a measurement, not promoted to a guarantee.

## Zero-match protection, fresh binaries, warnings

```text
zero-match      ZeroMatchGuard asserts a discovered COUNT per filter, proves its
                own counter with an impossible pattern, and demonstrates that
                ctest -R on that pattern EXITS 0 -- the premise. qualify.cmd
                separately counts the repeat filter's selection
fresh binaries  every process test takes its executable from $<TARGET_FILE:>,
                which resolves PER PRESET; each preset is cleaned before
                building; the no-op rebuild did 0 compiles and 0 links
warnings        0 unexpected, in every channel, with "unexpected" defined
                before looking -- see WARNING_AUDIT.md
```

## Final adversarial review

```text
QUESTIONS                 35  (23 from the brief, 8 further, 4 of my own)
FINDINGS                   6
PRODUCTION DEFECTS         0 in this milestone
DOCUMENT DRIFT             2  ADR-031; P16-SIZE-001 said "fourteen" of a
                              fifteen-row field list. Both amended, and the
                              second re-derived from both ends. Neither is a
                              gate failure: the property each document was
                              gating on is true
RECORDED SCOPE LIMITS      2  GUI meshing intent; zero-element reachability
PROCESS DEFECTS            1  my own audit, five instances, two of them claims
                              made inside this directory
OWED FINDINGS CLOSED       1  P16-ARCH F6, by a new test -- which voided the
                              first candidate and re-ran all three presets
GATE-BLOCKING              0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Mutation protection

Eight critical invariants, each traced to a mutation that was **run**, a
compile-fail case, or a structural impossibility with its proof — five, two and
one respectively, none uncovered.
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

Across the phase, **two mutations found production defects** and both fixes were
general rather than local: P16-CLI-001's M6 (the CLI naming the wrong reason for
a broken model) and the finding behind P16-QUALITY-001's named metrics.

## Known limitations

Genuine remaining limitations, none of which violates a P16 gate.

```text
Tet4 only, and Tet4 is not enough for accurate bending stress. ADR-031 records
  it as a known limitation of the P16/P17 foundation rather than leaving it to
  be discovered as a validation failure, and it is why the element type is a
  tag from the first day. P17's validation cases must distinguish
  discretisation error from a solver defect.

Adding a second element family costs a new struct, not just an enumerator.
  Connectivity is a fixed-arity array, chosen for compile-time arity. This is
  the cost ADR-031's span was meant to avoid, deferred to whichever milestone
  adds Tet10.

No quadratic elements, no hex meshing, no adaptive or boundary-layer meshing.
  Not P16 requirements; P18/P19 territory.

tetrahedralVolume(const Mesh&) has no test for its documented sign promise on
  a HAND-BUILT mesh. The code is correct and the branch is unreachable through
  the validated path; closing it is one unit test in P16-VOL-001's own file.

The GUI does not read a MeshControl -- it meshes with default controls. Recorded
  by P16-CMD-001 as explicitly OUT of scope ("needs GUI undo, which TODO.md does
  not authorize here"). The GUI's mesh is still derived state and it owns
  nothing canonical.

Configuration-driven meshing is BLOCKED BY DESIGN, not supported. Making it
  supported means making configuration REGENERATION rebuild bodies on
  activation -- a change to the configuration contract, not to meshing.

A drilled hole's cylindrical wall carries no FaceName, so it cannot be the
  target of a forward query. A naming-infrastructure limit; the reference models
  use two-loop extrusions, which is forced rather than chosen. Permanent
  semantic topology is P21's.

Mesh EDGES are not mapped. A decision, not an omission: the alternative is
  geometric classification with a tolerance. P17 has stated no edge requirement.

A planar body's mesh quality cannot be improved by any control P16 offers.
  OCCT gives a planar face two triangles whatever the deflection, and a volume
  target cannot subdivide a boundary. Measured on RM-MESH-05.

The dependency DOWNLOAD path is unverifiable in this environment: CMake's
  bundled curl has no CA trust anchors, so a clean-tree configure uses the same
  SHA256-verified sources from the local cache. The frozen SOURCE tree is fully
  verified; the download is not.

Sanitizer coverage is unavailable on this MinGW toolchain.

README.md has been stale since P13, and this milestone fixed only the clause it
  itself falsified. The "Not implemented" list claimed "No assemblies,
  drawings, materials database, meshing" -- four capabilities this repository's
  own evidence records as QUALIFIED. P16 made the fourth one false, so the
  clause was corrected and pointed at ROADMAP.md. What was deliberately NOT
  touched, because it is a documentation milestone and not this one: the
  "Current Status" table still stops at P12, and "Limits of the evidence" still
  says 1259 tests. Reported rather than quietly fixed, and rather than quietly
  left.
```

## Result

```text
RESULT:   PASS
P16:      QUALIFIED
EVIDENCE: this directory; qualification/qualification-times.txt for the run
TREE:     8ab30a31695e79c3d02fe222898f8b1ec6274565, qualified and committed
```

## Revision

First issue, 2026-10-06.
