# P17-POST-001 — adversarial review

```text
RESULT: PASS
attacks:            31
credible findings:   9
production findings: 2  (one design, one API; both fixed)
test defects:        6  (all fixed)
infrastructure:      1  (harness; fixed, and recorded as a rule)
remaining blockers:  0
```

This is a gate, not a formality. Two of the findings below changed production
code and one changed the public API; six were defects in my own tests that
would have let a real error through.

## The brief's attacks

```text
#   attack                                          answer
---------------------------------------------------------------------------
1   Can POST duplicate B and drift from P17-ELEM?   NO. One
                                                    computeTet4Kinematics call,
                                                    one ->strainFrom, and zero
                                                    occurrences of a shape
                                                    gradient, a 1/(6V) or a
                                                    cofactor determinant.
                                                    Counted in
                                                    RESULT_CONVENTIONS.md
2   Can POST duplicate D with a different shear     NO. One .stressFrom, and
    convention?                                     zero occurrences of
                                                    "lambda", "E * nu" or
                                                    "2 * (1 + nu)"
3   Can engineering gxy be treated as tensor exy?   NO. tensorOf halves it, in
                                                    the ONE place the mapping
                                                    exists. M7 and M10 killed
4   Can XY/YZ/ZX be permuted?                       NO. M8 and M9 killed; the
                                                    mapping test uses six
                                                    DISTINCT values and
                                                    asserts at(0,2) != 5.0
5   Can local Tet displacement order differ from    NO. elementDisplacements
    P17-ELEM?                                       uses localDofIndex, and a
                                                    test reads the twelve
                                                    positions back through the
                                                    numbering. M20 killed
6   Can a solver permutation be mistaken for        NO. SimplicialLDLT does not
    global DofIndex order?                          expose its permutation --
                                                    which is itself why it
                                                    cannot leak -- and
                                                    SolvedSystem::values() is
                                                    documented global order.
                                                    M21 (a rotated gather) is
                                                    the expressible form, and
                                                    is killed
7   Can one current node be missing?                NO. count == node count,
                                                    index-by-index against
                                                    mesh.nodes(), is_sorted and
                                                    adjacent_find. M28 killed
8   Can one Tet be missing from stress output?      NO. Same for elements.
                                                    M27 killed
9   Can rigid translation give nonzero strain?      NO, to 1e-16
10  Can rigid rotation give nonzero strain?         NO, to 1e-16 -- and the
                                                    instrument is guarded: a
                                                    non-rigid variant gives
                                                    gxy > 1e-6
11  Can a 2D von Mises pass because the tests       NO. The mandatory fixture
    only use szz = tyz = tzx = 0?                   has ALL THREE nonzero, and
                                                    the test ASSERTS the
                                                    plane-stress value differs
                                                    by > 5% (measured 11.2%).
                                                    M1-M4 killed
12  Can hydrostatic stress contribute to vm?        NO. Zero for five
                                                    hydrostatic states, and
                                                    unchanged under a shift for
                                                    five values of q. M6 killed
13  Can pure shear give vm = |tau|?                 NO. sqrt(3)|tau| for all
                                                    three components, and
                                                    sqrt(3)tau != tau asserted.
                                                    M5 killed
14  Can principal stresses stay in the              NO. Sorted descending after
    eigensolver's order?                            the solve. The diagonal
                                                    case is GIVEN ascending, so
                                                    passing the library's order
                                                    through would fail.
                                                    M12 and M13 killed
15  Can strain tensor off-diagonals use gxy?        NO. M7 killed; the
                                                    pure-shear principal strain
                                                    gives +-gamma/2
16  Can the ZX component be misplaced?              NO. M9 killed
17  Can NaN stress produce a published result?      NO. M14, M23, M24, M25
                                                    killed. Reachable through
                                                    the public kernel, which is
                                                    why it is public
18  Can u from M1 be post-processed on M2           NO. Stamp identity, not
    because the counts match?                       counts. M18 killed
19  Can repeating numeric ElementIds survive        NO. The remesh test asserts
    a remesh and falsely rebind?                    the new mesh's first NodeId
                                                    and ElementId really are 1
                                                    again, and recovery is
                                                    still refused
20  Can material B be combined with u solved        NO. Identity AND revision.
    under material A?                               M19 killed; both the
                                                    revision-only and the
                                                    different-id cases tested
21  Can POST create nodal-smoothed stress?          NO. Zero occurrences of
                                                    averaging, smoothing,
                                                    extrapolation or a nodal
                                                    stress channel
22  Can MPa/mm units leak into the core?            NO. Stress and Length are
                                                    Quantity types with no
                                                    implicit conversion, and
                                                    four compile-failure cases
                                                    enforce it
23  Can post-processing mutate the mesh or CAD?     NO. Every input is a const
                                                    reference; nothing is
                                                    written through one
24  Can recovered fields become persisted           NO. Nothing here serializes
    authority?                                      a result
25  Can zero targeted tests run while               NO. The selection is
    qualification claims PASS?                      counted from inside each
                                                    preset's own ctest log
26  Can a stale binary or a different tree          NO -- and this one BIT.
    produce evidence?                               See finding I1
```

## The attacks I added

```text
27  Can a summary maximum report its INITIALISER    NO, but only because the
    rather than a measurement?                      quantities are non-negative.
                                                    largestVonMises starts at 0
                                                    and von Mises is >= 0;
                                                    largestDisplacementMagnitude
                                                    likewise. Recorded because
                                                    the SAME pattern WAS a
                                                    defect in my own test --
                                                    see T2 -- where the
                                                    quantity could be negative
28  Can the binary search in displacementOf miss    Only if the mesh's
    if the channel is not sorted?                   enumeration were not
                                                    ascending, which P16
                                                    guarantees. Not assumed:
                                                    is_sorted and adjacent_find
                                                    are asserted on real output
29  Can a caller look up a handle in STALE          IT COULD. Finding P2
    fields and get a plausible answer?
30  Can a refusal exist that no test can reach?     SIX DID. Finding P1
31  Can the rotated-model invariance claim hold     IT DID. Findings T2 and T3
    even if the model were not rotated?
```

## Production findings

### P1 — six refusals could not be reached, so nobody had checked them

The first design had `recoverFields` as the only entry point. But possession
of a `StructuralModel` proves P16 validated the mesh and P15 validated the
material (ADR-036), and possession of a `SolvedSystem` proves every
displacement is finite (ADR-039). So

```text
NonFiniteDisplacement   unreachable
ElementRejected         unreachable
NonFiniteStrain         unreachable
NonFiniteStress         unreachable
PrincipalValueFailure   unreachable
NonFiniteDerivedResult  unreachable
```

Six of thirteen diagnostic values, and the header claimed "the other ten are
each reached by a test" — which was **false**. A refusal nobody can test is a
refusal nobody has checked, and the mutation probes would have survived
against all six.

**Fix: `recoverElementFields` is the public numerical kernel**, exactly as
`solveSymmetricSparse` is P17-SOLVE's, and for the same reason recorded there:
no structural fixture can pose the inputs. `recoverFields` calls it once per
tetrahedron, so the tested path is the production path — not a parallel copy.
It takes no document, no mesh and no source stamp, so it cannot be used to
publish a result and is not a back door.

Six probes now bite (M14, M23, M24, M25, M26, and the geometry refusal), and
the analytical affine case now runs through the production element path rather
than through two sub-calls.

The four values that remain unreachable are named as such in the header, with
the reason and the milestone that will open the path.

### P2 — a lookup could succeed against stale fields

`displacementOf(NodeId)` and `fieldsOf(ElementId)` answered from the recorded
channel without the caller having to prove the fields describe the mesh in
hand. A caller holding fields from mesh M1 could ask for `NodeId(1)` while
thinking of M2 and get a plausible number — the remesh test even documents this
("the handle resolves in the OLD channel").

The sibling API does not allow it: `StructuralResult::displacementOf` takes
`(mesh, node)` and refuses if the mesh is not the result's, "the refusal
ADR-031 requires".

**Fix: both accessors now take the mesh and verify `describes(mesh)` first**,
matching P17-DATA's contract exactly. The misuse brief section 50 names is now
impossible rather than documented.

## Test defects

Six, and each would have let a real error through.

```text
T1  the axial compliance bound asserted a POINT MAXIMUM of u_z, not the mean.
    It FAILED: largest 1.38406e-06 m against FL/(AE) = 9.92e-07 m. The load is
    lumped onto face NODES, so a single node can exceed the uniform-strain
    value; the compliance theorem bounds the AVERAGE. Fixed by measuring the
    right quantity -- mean ratio 0.984015 -- not by widening the bound

T2  peakPrincipal was a std::max against an initialiser of 0.0. Under a purely
    inward pressure every sigma1 is negative, so it reported 0.0 -- the
    initialiser -- and the rotated-model invariance compared 0 against 0. Fixed
    by seeding from the first element, and the test now asserts
    peakPrincipal < 0.0 so it is a measurement

T3  nothing asserted that RM-MESH-06's two documents were actually differently
    placed. The base and rotated models return BIT-IDENTICAL invariants, which
    is the ideal outcome -- and is also exactly what two copies of the same
    document would give. Fixed: the first node's position must differ by more
    than 1 mm

T4  the storage claim compared result bytes against nodes * tets * 8. On a
    19-node, 58-tet mesh that is 8816 bytes against a 10504-byte result, so the
    assertion was FALSE and meaningless at that scale. Fixed by asserting what
    is actually true -- exact cardinality plus a compile-time per-entry cost --
    and moving the linear-GROWTH measurement to the reference suite, where a
    cylinder can be refined

T5  the remesh test expected SolutionSourceMismatch. Wrong: the system and the
    solution are BOTH from before the remesh, so their sources agree and the
    first check cannot fire. MeshMismatch is correct. Fixed, and the test now
    asserts both premises -- that the sources still agree and that the old
    numbering no longer describes the new mesh -- so the reader is not left to
    assume which check fired

T6  the large-mesh smoke refined a BOX. A plane is exactly representable, so a
    box's surface mesh stays at two triangles per face however fine the
    deflection: it produced 9 nodes and 12 tets and measured nothing. Moved to
    RM-MESH-02, a cylinder, at 140 -> 850 nodes and 508 -> 4210 tets

T7  two compile-failure regexes did not match GCC's wording ("invalid
    initialization of reference"). The cases were passing the wrong way --
    reported as failures -- rather than silently; fixed

T8  elementRecoveryProblem(huge, huge, d) passed a Translation3D array where a
    Point3D array was wanted. My own slip, caught by the compiler
```

All six share a shape: **an assertion that could not fail**. T2, T3 and T4 were
vacuous; T1 and T5 asserted the wrong thing; T6 measured a degenerate fixture.
Four of this project's recorded lessons are the same shape, which is the
argument for probing rather than reading.

## Infrastructure finding

### I1 — a background probe harness outlived its completion notification

The probe set was launched with `nohup ... &`. The task-completion notification
arrived while `run.sh` was **still running**, and for the next twenty minutes
it:

```text
overwrote my source edits with its pristine copies, on every probe -- twice,
    silently, while I was editing the same two files
left one mutation applied when it was finally stopped
produced collided builds: "ld returned 1", "Permission denied",
    "final link failed: file truncated"
left a 0-BYTE bettercad_tests.exe that ninja considered up to date, so the
    next ctest run reported failures from a MUTANT binary -- which looked
    exactly like my own fix being broken
```

The source files were **untracked**, so `git status` showed nothing and
`git diff` could not show the mutation. It was found by diffing against the
pristine copies.

```text
fixes applied
    the harness now runs ONE BATCH PER INVOCATION, in the foreground
    the process list is checked for the harness before anything else is done
    the harness's own kill classification now treats "truncated" and
      "Permission denied" as VOID, not as a compiler kill
    mutations that drop a term now (void)-cast what they orphan, so
      -Werror=unused-variable cannot turn a test question into a compiler kill
```

Three of this project's existing rules describe pieces of this —
"stopping a task does not stop its child", "a restored source is not a restored
binary", "a collided build reports a linker error" — and this is the first time
all three fired at once. It is recorded as a memory rule.

## Where the gates sit, and why three source checks and not one

```text
check                                    catches
---------------------------------------------------------------------------
solution.source() == system.source()     a solution from a different
                                         assembled system: a different body,
                                         control, geometry revision, mesh,
                                         material or material revision
numbering.describes(mesh)                a numbering built for another mesh --
                                         and the REMESH case, since the model
                                         moves while the system and solution
                                         do not
solution.describes(mesh)                 a displacement field of the wrong
                                         size for this mesh
material id AND revision                 current material B against u solved
                                         under material A
```

A fourth comparison -- the system's source against the model directly -- was
considered and **rejected as unreachable**: a `StructuralModel`'s possession
already proves its mesh is current for its geometry revision, so a system whose
geometry differed would necessarily have a different mesh stamp, which the
second check catches. Adding it would have been a fifth diagnostic value no
test could reach, which is the defect P1 was about.

## What this review did NOT find

```text
no hidden global state            the module has none; every function is
                                  stateless or takes its inputs
no behaviour that exists only
  for tests                       the kernel is production's own path, called
                                  once per element by run()
no weakened tolerance             the only bound that MOVED moved TIGHTER:
                                  the axial mean stress from 10% to 1e-9, on
                                  the strength of the equilibrium theorem
no disjunction hiding a probe     every problem-value assertion is an exact
                                  equality. Three earlier P17 milestones lost
                                  a kill to a disjunction; this one has none
no partial publication            the vectors move into RecoveredFields only
                                  after the last element passes
```
