# P17-POST-001 — finiteness, source compatibility and atomicity

```text
RESULT: PASS
```

## Which refusals exist, and from where each is reachable

This is the honest table, and it was wrong twice before it was right. The
first draft of the header claimed every value was reachable and tested; it was
not. The second draft claimed `PrincipalValueFailure` was reachable through the
kernel; a mutation probe proved it is not.

```text
value                       reachable from              tested      probe
-----------------------------------------------------------------------------
SolutionSourceMismatch      recoverFields               YES         M17 killed
MeshMismatch                recoverFields               YES         M18 killed
MaterialSourceMismatch      recoverFields               YES         M19 killed

NonFiniteDisplacement       recoverElementFields        YES         M23 killed
ElementRejected             recoverElementFields        YES         M26 killed
NonFiniteStrain             recoverElementFields        YES         M24 killed
NonFiniteStress             recoverElementFields        YES         M25 killed

MeshHasNoElements           NOT REACHABLE               no          --
ElementNodeMissing          NOT REACHABLE               no          --
DegreeOfFreedomMissing      NOT REACHABLE               no          --
InvalidMaterial             NOT REACHABLE               no          --
PrincipalValueFailure       NOT REACHABLE               no          M14, M15
NonFiniteDerivedResult      NOT REACHABLE               no          --
```

### Why six are not reachable, and why they are kept anyway

```text
MeshHasNoElements        a VolumeMesh cannot hold zero tetrahedra: P16's
                         validation refuses one
ElementNodeMissing       nor an element naming an absent node
DegreeOfFreedomMissing   the numbering is checked against the mesh first, so a
                         node of the mesh always has three indices
InvalidMaterial          P15 refuses an unusable E or nu AT ENTRY, which
                         P17-MAT-001 established and proved
PrincipalValueFailure    a FINITE symmetric 3x3 cannot produce a non-finite
                         eigenvalue, and SelfAdjointEigenSolver cannot fail on
                         one. The stress finiteness check runs first and
                         catches every state that could have reached here
NonFiniteDerivedResult   von Mises is a sum of squares of finite numbers and
                         hydrostatic stress is their mean, so both are finite
                         whenever the stress is -- which the previous check
                         guarantees
```

They are kept because **the refusal must be structural rather than a comment.**
A later milestone that builds a model from a file, a script or a network
message opens every one of these paths, and a silently skipped element would
publish a softer body with nothing reporting it. Deleting them would also mean
deleting `solver.info()`, which is the one place Eigen is allowed to report a
failure — and ignoring a library's failure signal is the mistake ADR-039 was
written against, inverted.

**Two probes record this honestly rather than hiding it:** M14 (the non-finite
principal check disabled) and M15 (`solver.info()` ignored) both **SURVIVED**,
because no input can reach either branch. That is the correct outcome for a
guard on an impossibility, and it is reported as a survival rather than
dressed up as a kill.

## The finiteness matrix

Every case below is reached through `recoverElementFields`, the public
numerical kernel, because no validated mesh and no qualified solve can produce
them.

```text
field family        non-finite test                      expected                PASS
-----------------------------------------------------------------------------------------
displacement        one corner's uy = NaN                NonFiniteDisplacement   yes
displacement        one corner's uz = -Inf               NonFiniteDisplacement   yes
geometry            two corners swapped (inverted)       ElementRejected         yes
geometry            fourth corner made coplanar          ElementRejected         yes
geometry            a corner coordinate NaN              ElementRejected         yes
                      (via tet4GeometryProblem)            (NonFiniteCoordinate)
stress              corner displacements of 1e300 m      NonFiniteStress         yes
                      -- FINITE input, finite strain,
                      overflowing stress
publication         any of the above                     nothing published       yes
```

### The check order is asserted EXACTLY, never as a disjunction

The order is documented in the header, so a NaN displacement must be reported
as a non-finite **displacement** and not as whatever downstream step notices it
second. Every assertion is an exact equality against one `RecoveryProblem`
value.

Three earlier P17 milestones lost a mutation kill to a disjunction added "for
safety" — P17-BC-001's D4, P17-ASSEMBLY-001's D5, P17-SOLVE-001's M11 — and
this milestone does not repeat it. There is no `||` in any problem-value
assertion.

### The 1e300 case is the interesting one

```text
corner displacements   1e300 m, each component FINITE (asserted in the test)
tetrahedron            ~2e-2 m across
strain                 ~1e302, still finite
stress                 ~lambda * 1e302, OVERFLOWS a double
reported as            NonFiniteStress
```

So the displacement check passes, the strain check passes, and the **stress**
check fires. That is the check order picking out the first quantity that is
genuinely not finite, rather than blaming the input.

## Source compatibility

Four checks, each proving something the others do not.

```text
check                                   catches                              test
--------------------------------------------------------------------------------------
solution.source() == system.source()    a solution from a DIFFERENT           yes
                                        assembled system -- any of six
                                        dependencies moved
numbering.describes(mesh)               a numbering built for another mesh,   yes
                                        and the REMESH case
solution.describes(mesh)                a displacement field of the wrong     yes
                                        size for this mesh
material id AND revision                current material B against u          yes
                                        solved under material A
```

The source comparison is `operator==` on `AssemblySource`, which is
`= default` over all six fields — so adding a seventh dependency without
adding it to the comparison is impossible, because there is no hand-written
comparison to forget.

### The remesh case, and WHICH check catches it

```text
RM: the same body remeshed at a 12 mm global target
new mesh's first NodeId      1          <-- asserted: the ids REPEAT
new mesh's first ElementId   1          <-- asserted
old fields describe it?      NO
```

```text
solution.source() == system.source()    STILL TRUE -- both are from before
                                        the remesh, so their sources agree
                                        with each other. Asserted, so the
                                        reader is not left to assume it
numbering.describes(newMesh)            FALSE -- asserted directly
reported problem                        MeshMismatch, asserted EXACTLY
```

The first draft of this test expected `SolutionSourceMismatch` and failed. That
was my misunderstanding of which object moves: the **model** moves while the
system and the solution do not. Recording which check fires, and why the first
one cannot, is more useful than a disjunction that would have passed either
way.

### Material source

```text
case                                    expected                    PASS
---------------------------------------------------------------------------
same MaterialId, revision moved         MaterialSourceMismatch      yes
                 (E x 3)                 message names "revision"   yes
different MaterialId (a second
  material created and assigned)        MaterialSourceMismatch      yes
```

Both halves matter: a `MaterialId` alone cannot say "the same material with a
changed modulus", which is why `AssemblySource` carries the revision and why
this checks both.

Recovering `sigma = D(new) eps(old)` would produce a number that is **wrong in
a way nothing downstream could detect** — the message says so — so it is
refused rather than computed.

## Atomicity

```text
requirement                            how
---------------------------------------------------------------------------
an element failing at index 927        the vectors are LOCAL to `run` and are
  publishes nothing                    moved into RecoveredFields only after
                                       the last element passes. There is no
                                       path that constructs a RecoveredFields
                                       from a partial set, because the
                                       constructor is private with one friend
no partial success                     Result<RecoveredFields>: either the
                                       complete set or a diagnostic
`fieldRecoveryProblem` and             ONE shared `run`, so the caller asking
  `recoverFields` cannot disagree      which problem there is cannot be told
                                       something different from the caller
                                       asking for the fields. Asserted on both
                                       a well-posed and a failing input
the per-element kernel likewise        ONE shared `recoverOneElement`, used by
                                       `recoverElementFields`,
                                       `elementRecoveryProblem` and `run`
```

Two probes test the atomicity claim from the other side:

```text
M27  one element silently skipped      KILLED -- the cardinality assertions
M28  one node silently skipped         KILLED -- likewise
```

A skipped element is exactly what a non-atomic publication looks like from the
outside, and the cardinality checks are what notice.

## Failure traceability

```text
failure                      names
---------------------------------------------------------------------------
per-element                  the ElementId, through the shared kernel. When a
                             kernel caller has no mesh it passes an invalid
                             handle and the message says "the element"
per-node                     the NodeId, from `run`'s own node loop -- which
                             is why that loop keeps its own finiteness check
                             even though the kernel re-checks the same values:
                             it can name the node and the kernel cannot
geometry                     P17-ELEM's own diagnostic, PROPAGATED with the
                             ElementId prefixed, never restated
source mismatch             the material id and both revisions, in the message
```

These are **current-mesh diagnostics, not persistent CAD identity**, which is
ADR-031's distinction: a `NodeId` or an `ElementId` in a message is a handle
for a human reading a log, never a reference stored anywhere.
