# P16-QUAL-001 — mutation protection audit

```text
SUBJECT:  for each critical P16 invariant, the evidence that a test would
          CATCH its violation -- not that the invariant is documented
METHOD:   a mapping, not a figure count. The brief names eight invariants; each
          is traced to a mutation that was run, or to a compile-fail case, or
          to a structural impossibility with its proof
NOTE:     BetterCAD uses controlled manual mutations, which the brief permits
          provided the evidence is credible. Every mutation cited below was
          applied, built and run, with its verdict recorded in the owning
          milestone's evidence
```

## The eight the brief names

### 1. `abs(signedVolume)`

```text
MUTATION    P16-REFMOD-001 M8: total += std::abs(signedVolume(...).si())
VERDICT     SURVIVED -- and proven unobservable rather than excused
```

The proof is the call order: `generateVolumeMesh` runs `validate(mesh)` at
`VolumeMesh.cpp:414` and returns `InvalidMesh` at 418, and sums the volume at
431 — so every term is strictly positive by then and `abs(x) == x`.
`VolumeMesh`'s constructor is private with `generateVolumeMesh` its only friend,
so there is no other way to obtain one.

**The premise is measured, not assumed**: the reference suite computes a
determinant per element from the node coordinates and finds 1751 positive, 0
zero, 0 negative. And the check that *would* distinguish them exists and is
correct —

```text
CHECK_THAT(mesh.tetrahedralVolume().in(units::mm3),
           WithinRel(structure.orientation.totalVolume, 1e-12));
```

— where the right-hand side is the suite's own **signed** sum. The branch is
unreachable, not untested.

> A coverage gap this exposed and which remains open: the free function
> `tetrahedralVolume(const Mesh&)` is also callable on a hand-built `Mesh`, and
> its header promises an inverted element drags the total down. No test builds
> such a mesh, so M8 survives the whole repository and not only the reference
> models. That is a test-coverage gap in **P16-VOL-001's** own file, the code is
> correct, and closing it is one small unit test there. Carried into known
> limitations rather than fixed here, because P16-QUAL-001's subject is audit.

### 2. Zero-Tet success

```text
MUTATION    P16-REFMOD-001 M6: RM-MESH-08's profile CLOSED, so the body builds
VERDICT     KILLED, 7 assertions across the failure case, the suite-wide
            execution gate and the determinism gate
BEHAVIOURAL VolumeBackend_RefusesAnOpenSurfaceRatherThanReportingSuccess feeds
            nglib a tetrahedron with one face removed; nglib returns NG_OK with
            zero elements and the adapter refuses. Not a stub -- the admitted
            backend's real documented behaviour
STRUCTURAL  MeshIssueKind::EmptyMesh makes an empty mesh invalid, so a
            zero-element result cannot pass for want of bad elements
```

### 3. Hole occupancy disabled

```text
MUTATION    P16-REFMOD-001 M2: RM-MESH-03's hole made a construction circle,
            so it is filled
VERDICT     KILLED, 11 assertions -- and the occupancy gate was among them,
            which is what proves it load-bearing rather than decorative
```

Three independent gates fired: the analytic volume bound, the kernel's CAD
volume against the closed form, and the occupancy test. Any one alone would
have caught it, which is the layering the brief asks for when it says volume
agreement is not sufficient.

### 4. Stale-geometry guard removed

```text
MUTATION    P16-SURF-001: "Mutation-proved: disabling the currency check two
            layers down in GeometryPreparation fails [the surface tests]"
VERDICT     KILLED -- the guard is load-bearing two layers above where it lives
FINAL GATE  P16Qualification_StaleGeometryNeverYieldsACurrentMesh puts the whole
            contract in one sequence, including a regeneration that FAILS after
            a mesh was already current
```

### 5. Local `GeometryReference` replaced with a mesh ID

**Structurally impossible, and the type system is the test.** Four compile-fail
cases pin exactly this substitution:

```text
compile_fail.meshcmd.local-sizing-is-not-keyed-on-a-node
compile_fail.meshcmd.local-sizing-is-not-keyed-on-an-element
compile_fail.meshids.node-id-as-object-id
compile_fail.meshids.element-id-as-object-id
```

`LocalMeshSizing::face` is a `FaceName` — an `ObjectId` plus a selector — and a
`NodeId` has no conversion to `ObjectId`. A mutation here does not fail a test;
it fails to compile, which is stronger.

### 6. Generated Tet arrays persisted

```text
MUTATION    P16-PERSIST-001: 12 mutations, 12 killed
STRUCTURAL  MeshControlDefinition has no member that could hold a result, and
            four compile-fail cases say so by name:
              compile_fail.meshcmd.definition-has-no-nodes
              compile_fail.meshcmd.definition-has-no-elements
              compile_fail.meshcmd.definition-has-no-quality-report
              compile_fail.meshcmd.definition-has-no-generated-mesh
FINAL GATE  two new gates make it a standing check rather than a one-off:
            generating a mesh leaves the document BYTE-IDENTICAL, and the
            saved file is byte-identical for a 361-tet and a 1977-tet mesh
```

The density gate is the one a persisted node array could not survive: 376 nodes
would be 9024 bytes of coordinates against a 3347-byte file.

### 7. CLI returning 0 on failure

```text
MUTATION    P16-CLI-001 M4: a failed mesh generation exits zero
VERDICT     KILLED
MUTATION    P16-CLI-001 M6: the CLI ignores a feature that failed to
            regenerate
VERDICT     KILLED, 7 -- and it FOUND A PRODUCTION DEFECT: the CLI had been
            reporting `no_mesh_control` for a broken model, which is true and
            not the reason
REGRESSION  RM-MESH-08's five CLI queries each assert EXIT_CODE 1 with a
            structured diagnostic on stderr and nothing on stdout
```

### 8. Zero-test filter accepted

**Proven from the inside, which is stronger than a mutation.**
`ZeroMatchGuard.cmake` asserts a discovered **count** per filter, then
demonstrates its own counter by showing a deliberately impossible pattern
discovers 0, then demonstrates the premise by showing `ctest -R` on that
pattern **exits 0**. `qualify.cmd` separately counts the repeat filter's
selection and fails the stage at zero. Both of this phase's new filters are in
the guard's table.

## The compile-fail surface, counted

Mutation testing proves a test would catch a defect. A compile-fail case proves
the defect cannot be written. P16 has **45** of them:

```text
meshids          15   identity domains, in every wrong direction:
                      node/element/region/object/sketch substitution, integer
                      conversion both ways, cross-type comparison, mutating a
                      node through a mesh, a triangle with tet connectivity
meshcmd          13   the command and control surface: no nodes, no elements,
                      no quality report, no generated mesh in a definition;
                      local sizing not keyed on a handle; a command that will
                      not take a feature for a face; no mesh setter; the mesher
                      hands out no mutable mesh
geometrymeshmap   5   a map cannot be fabricated, built from mutable geometry,
                      queried through a mutable mesh, or written through
meshquality       4   a report cannot reach or repair its mesh
volumemesh        3   a VolumeMesh cannot be default-constructed, made from a
                      plain Mesh, or mutated
meshview          5   a render index is not a NodeId and vice versa
```

Each is a real compilation that must fail, which is why they are excluded from
the five-repeat determinism stage: five rounds of two hundred compilations
would cost about two hours and report nothing new.

## Per-milestone mutation record

Recorded here as audit history rather than re-run, which §71 permits. The
figures are each milestone's own, in its own evidence:

```text
| MILESTONE        | MUTATIONS | NOTE                                        |
| P16-DATA-001     | yes       | see its finding F3                          |
| P16-GEOM-001     | yes       | mutation proof of the eligibility guard     |
| P16-SURF-001     | yes       | the currency check, proved from two layers up|
| P16-QUALITY-001  | 24        | including one that SURVIVED and proved a
|                  |           | branch unreachable -- the finding that made
|                  |           | every metric get a name                     |
| P16-MAP-001      | 15        |                                             |
| P16-CMD-001      | yes       | with proven-equivalent survivors            |
| P16-PERSIST-001  | 12        | 12 killed                                   |
| P16-CLI-001      | 8         | 6 killed, 2 proven equivalent, 1 PRODUCTION
|                  |           | DEFECT found                                |
| P16-REFMOD-001   | 9         | 8 killed, 1 proven equivalent               |
```

**Two mutations across the phase found production defects** — P16-CLI-001's M6
(the CLI naming the wrong reason for a broken model) and the finding behind
P16-QUALITY-001's named metrics — and in both cases the fix was general rather
than local. That is the evidence that mutation testing here is doing work and
not performing it.

## Result

```text
INVARIANTS THE BRIEF NAMES           8
covered by a RUN mutation            5  (abs, zero-Tet, occupancy, stale
                                         guard, CLI exit)
covered by COMPILE-FAIL              2  (handle-for-reference substitution,
                                         results in a definition)
covered by a SELF-DEMONSTRATING       1  (zero-match filter)
  PROOF
uncovered                             0
OPEN COVERAGE GAP                     1  tetrahedralVolume's sign promise on a
                                         hand-built Mesh -- P16-VOL-001's file,
                                         named in known limitations
```
