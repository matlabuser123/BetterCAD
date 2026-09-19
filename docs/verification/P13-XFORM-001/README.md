# P13-XFORM-001 — Component Transforms

```text
STATUS:          PASS — 1301/1301 on debug, release and debug-shared
                 from clean; qualified tree == committed tree
BASELINE:        08b2ec1 (P13-XFORM-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           a component's placement -- the canonical intent, its
                 resolution to a transform, its dependencies and its
                 persistence. No mates, no solver, no grounded flag, no
                 placement relative to another component, no transformed
                 bodies, no CLI change.
IMPLEMENTATION:  ComponentPlacement in core, resolution in assembly, the
                 placement on ComponentDefinition, its JSON mapping
TESTS:           19 new Catch2 cases in 2 files
EVIDENCE:        this directory
```

## Scope, and what was deliberately not built

The milestone gives a component a position and nothing else. Absent on
purpose:

| Not built | Where it belongs |
| --- | --- |
| `grounded` | `P13-SOLVE-001` — see "Known limitations" |
| placement relative to another component or a datum | `P13-REF-001` |
| mates, constraints, a solver | `P13-MATE-001`, `P13-SOLVE-001` |
| components producing transformed bodies | a later milestone; this one changes no body and no export |
| `info` showing where a component sits | `P13-CLI-001` |

The `apps` tree ID of the qualified tree is `fdecf1a8…`, byte-identical to
the one `P13-COMP-001` qualified, which is the mechanical proof that no CLI
code changed.

## ADR contract

[ADR-005](../../architecture/decisions/ADR-005-placement-is-intent-transforms-are-derived.md)
decides the whole shape of this milestone, and
[ADR-006](../../architecture/decisions/ADR-006-assembly-module-and-layer.md)
decides where the pieces live.

| Contract | Where it is honoured |
| --- | --- |
| A component persists **placement intent**, never a solved transform | `ComponentPlacement`; no transform field anywhere |
| Intent is modelled on `CoordinateSystemDefinition`, literal or parameter-driven | `core/document/Placement.hpp` |
| The solved `RigidTransform3D` is **derived** | `assembly::placementOf()`, a function with no store |
| `.bcad` contains no transforms for components | `PlacementFile_HoldsIntentAndNeverASolvedTransform` |
| Placement follows the active configuration | `effectiveParameterValue`, tested |
| The placement **value type** lives in `core`, beside `References.hpp` | ADR-006 names "the placement intent" explicitly |
| Resolution lives in `assembly` | `src/assembly/Placement.cpp` |

ADR-006 is the reason the intent type is not in `assembly`: it puts "the
reference and definition value types that carry no geometry -- the mate
reference kinds and the placement intent" in `core`, so that `io` and the CLI
can name a placement without depending on assembly internals. Following the
obvious instinct and putting `ComponentPlacement` next to `Component` would
have contradicted an accepted decision.

## Implementation

```text
include/bettercad/core/document/Placement.hpp   ComponentPlacement, validate,
                                                isIdentity, referencedParameters
src/core/document/Placement.cpp                 those three
include/bettercad/assembly/Placement.hpp        resolvePlacement, placementOf
src/assembly/Placement.cpp                      resolution, parameter reading
include/bettercad/assembly/Component.hpp        placement on the definition
src/assembly/Component.cpp                      validation, dependencies
include/bettercad/assembly/Components.hpp       setComponentPlacement
src/assembly/Components.cpp                     it, through the checked path
src/io/json/ComponentJson.cpp                   the placement mapping
```

275 lines in four new files, and 150 lines added to existing ones. The 19
deleted lines are not a rewrite of anything: doc comments the placement made
stale, the one-element `return {definition_.part};` that `dependencies()`
replaced, and the `requireObject` key list in the component reader that now
admits `placement`.

## Transform representation

**No new transform type was written, and none was needed.** `RigidTransform3D`
(`core/math/RigidTransform.hpp`) already existed, is already used by patterns
and mirrors, and already states its contract:

```text
p' = A p + t          A orthogonal, stored row-major, 3x3
rotation(axis, angle) Rodrigues, right-handed, the axis' points fixed
after(first)          this(first(x)) -- explicit composition order
Translation3D         three Lengths, so a translation carries units
```

A placement composes that type and adds nothing to it. `det A = +1` always,
because a placement never constructs a reflection — checked by
`reversesOrientation()` being false, including for a three-axis rotation.

The brief asked for "rotation + translation" with an unambiguous rotation
representation. That is what `RigidTransform3D` is: the rotation is held as
the matrix itself, so there is no quaternion to normalise, no Euler triple to
interpret, and no representation to go stale. The **intent** is stored as
three angles, but those are inputs, not the mathematical object.

## Local placement

`ComponentDefinition` gains one field:

```cpp
ComponentPlacement placement{};
```

It belongs to the instance, not to the part. Three components of one part hold
three placements and one part definition, which is what makes them instances
rather than copies. `setComponentPlacement()` is the supported way to move
one, and it runs the same checks creation does — the lesson `P13-COMP-001`
learned when its modify path bypassed validation.

## Canonical versus derived state

This is the invariant the milestone exists to establish.

```text
canonical, persisted        derived, never stored
--------------------        ---------------------
ComponentPlacement     ->   RigidTransform3D
  three Lengths             assembly::placementOf(document, id)
  three Angles              computed on every call
  six optional              from the intent and the parameter
    ParameterIds            values in force
```

The derived side is **a function, not a cache**. There is no map of solved
transforms, no invalidation, no lifetime tied to regeneration. This is
stronger than ADR-005 requires and was chosen deliberately: a transform that
is never stored cannot go stale, cannot be persisted by accident, and cannot
disagree with the intent it came from. ADR-005 anticipated solved transforms
living beside bodies; that becomes necessary when a solver produces positions
that intent alone does not determine (`P13-SOLVE-001`), and not before.

Three consequences, all tested:

- editing a driving parameter moves the component with nothing to refresh;
- save/load cannot alter a transform, because no transform is saved;
- two documents with the same intent give the same transform, with no
  dependence on how either was built.

## Composition convention

Rotations are **extrinsic**: each is about an axis of the model's frame
through its origin, not about the axes left by the previous rotation. Applied
X, then Y, then Z, the composed matrix is therefore `Rz * Ry * Rx`. The
translation is along the model's own axes and is applied **after** the
rotations.

This is not a new convention. It is exactly what
`CoordinateSystemKind::Offset` already does for datum coordinate systems
(`DatumResolution.cpp:150-165`, "About the base's fixed X, then Y, then Z
axis, all through its origin"), so the product has one rotation convention
rather than one per subsystem.

Both halves are pinned by tests designed so that the wrong answer is a
*different point*, not a slightly different one:

| Case | This convention | The alternative it rules out |
| --- | --- | --- |
| `rx = rz = 90 deg`, point `(0,1,0)` | `(0, 0, 1)` | Z-then-X would give `(-1, 0, 0)` |
| `rz = 90 deg`, `t = (10,0,0)`, point `(1,0,0)` | `(10, 1, 0)` mm | translate-then-turn would give `(0, 11, 0)` mm |

## Rotation convention

Right-handed and **active**: the point moves, the frame does not.

| Rotation | Sends | Hand-derived by |
| --- | --- | --- |
| +90 deg about X | Y to Z, Z to -Y | right-hand rule |
| +90 deg about Y | Z to X, X to -Z | right-hand rule |
| +90 deg about Z | X to **+Y**, Y to -X | right-hand rule |
| 180 deg about Z | `(1,2,3)` to `(-1,-2,3)` mm | negation of X and Y |

The Z case is the one that separates active from passive: a passive rotation
of the same sign would send X to **-Y**.

The full matrix is checked entry by entry against a hand-computed
`Rz(30 deg)` with `cos30 = sqrt(3)/2`, `sin30 = 1/2`, stored row-major. A
transposed (column-major) implementation puts `-sin30` and `+sin30` in each
other's places and fails.

## Units

Translations are `Length`, rotations are `Angle`, both SI internally, with the
unit only at the boundary.

| Check | Expected | Measured |
| --- | --- | --- |
| 90 deg and pi/2 rad give the same matrix | identical to 1e-12 | identical |
| 1 in of translation | 0.0254 m | 0.0254 m |
| an angle parameter driving a translation | refused | `DimensionMismatch` |
| a length parameter driving a rotation | refused | `DimensionMismatch` |

The last two matter: units here are not a formatting concern but a type
error that is caught rather than silently reinterpreted.

## Multiple instances

`Placement_IsPerInstanceSoMovingOneLeavesTheOthersWhereTheyWere`:

| Expected | Measured |
| --- | --- |
| three components of one part, three placements | 10 mm along X, 20 mm along Y, 90 deg about Z |
| moving one | only that one moves; the other two measured unchanged |
| the part is not duplicated | 1 sketch + 1 extrude + 3 components = 5 objects |
| all three still place the one part | `componentsOf(part) == {a, b, c}` |

## Default and identity placement

A component created without a placement gets `ComponentPlacement{}`: all
translations zero, all rotations zero, no parameters.

| Expected | Measured |
| --- | --- |
| `isIdentity` | true |
| the transform | `isTranslation()`, and every test point unmoved |
| written to the file | **nothing at all** |
| loaded from a file with no placement | the identity, not an absent value |

`isIdentity` compares against a default-constructed placement, so a placement
whose literals are zero **but which is driven by a parameter** is *not*
identity and *is* written. That is load-bearing: treating it as identity
would silently drop the parameter on save. The round-trip test covers exactly
that case.

## Independent validation

The oracle is hand-derived mathematics, never the implementation. Every
expected value below was computed from the stated convention and written into
the test as a literal.

```text
case                          expected                  measured    tol
identity, point (1,2,3) mm    (1, 2, 3) mm              equal       1e-12
translation (10,20,30) mm     (11, 22, 33) mm           equal       1e-12
+90 deg about X, Y            (0, 0, 1)                 equal       1e-12
+90 deg about Y, Z            (1, 0, 0)                 equal       1e-12
+90 deg about Z, X            (0, 1, 0)                 equal       1e-12
rx=rz=90 deg, point (0,1,0)   (0, 0, 1)                 equal       1e-12
rz=90 deg + t, point (1,0,0)  (10, 1, 0) mm             equal       1e-12
Rz(30 deg) matrix, 9 entries  [c,-s,0, s,c,0, 0,0,1]    equal       1e-12
Rz(30 deg), point (1,0,0) mm  (c+5, s-7, 2) mm          equal       1e-12
```

`1e-12` is justified rather than inherited: these are a few products and sums
of values of order 1, so the conditioning is excellent. The tolerance sits far
above the ~1e-16 rounding of a 90 degree sine and far below any error a wrong
convention, a transpose or a swapped composition order would produce — each
of those is an error of order 1, not of order 1e-12.

## Failure atomicity

`Placement_RefusesNonFiniteValuesAndKeepsTheOneItHad`, over four bad
placements (NaN and infinite translation, NaN and negative-infinite rotation):

| Expected | Measured |
| --- | --- |
| the call fails | `InvalidArgument`, every time |
| the component keeps its placement | equal to the one it had |
| the document revision | unchanged |
| a later valid move still works | it does |

Validation happens before anything is written, so a refused move changes
nothing rather than being rolled back.

## Persistence

Intent round-trips; the transform is never written.

| Expected | Measured |
| --- | --- |
| literal placement survives save/load | equal |
| parameter-driven placement survives, parameter included | equal |
| the transform after loading | **exactly** equal to before, `==` on `RigidTransform3D` |
| a driven placement still follows its parameter once loaded | 25 mm |
| re-saving the loaded document | byte-identical |
| an unmoved component | writes no `placement` key at all |
| `transform`, `matrix`, `solved`, `world`, `position`, `basis` | all absent from the component entry |
| format version | still 1; no new top-level key |

Malformed placements are parse errors naming their path, not silent defaults:
a non-numeric translation, an unknown key inside the placement, and a missing
key are each rejected.

The absence assertions are scoped to the component's own entry, using a
brace-balanced extraction of that object. An earlier version of the helper
looked for `"},"` and broke on the last object of the array; that was a defect
in the test, found by running it, and is recorded here because `P13-COMP-001`
made the same class of mistake with an over-broad search.

## Determinism

| Expected | Measured |
| --- | --- |
| the same intent twice | `RigidTransform3D` **bit-identical** (`==`, not a tolerance) |
| asking twice in one document | identical |
| save -> load -> transform | identical to before saving |
| configuration switched and switched back | the original value exactly |

Bit-identical equality is claimed only where it is earned: the same inputs
through the same arithmetic in the same order. No claim is made about
different call sequences producing identical floating point.

## Adversarial review

Run against the implementation before completion, working through the
milestone's own challenge list.

### Cleared, with the evidence that clears it

| Question | Answer |
| --- | --- |
| Composition order reversed anywhere? | No — `rx=rz=90 deg` gives different points for the two orders, and the tested one is correct |
| Row-major and column-major mixed? | No — all nine entries checked against a hand-computed matrix; a transpose fails |
| Active and passive rotations confused? | No — `+90 deg` about Z sends X to **+Y**; passive would give -Y |
| Radians and degrees mixed? | No — `90_deg` and `pi/2` rad give identical matrices |
| Can translation units be lost? | No — `1_in` resolves to 0.0254 m |
| Can two instances share placement state? | No — a value member of the definition; moving one leaves the others measured in place |
| Can save/load alter a rotation? | No — transform equality and byte-identical re-save |
| Can a non-rigid transform slip through? | No — a placement never constructs a reflection; `reversesOrientation()` false |
| Can derived transforms become persisted truth? | No — six forbidden key names checked absent from the component entry |
| Mate-solver behaviour implemented early? | No — no solver, no mates, no `grounded` |
| OCCT transforms treated as canonical? | No OCCT in `assembly` at all |
| Tests using the implementation as their own oracle? | No — every constant hand-derived |
| Can a placement parameter be confused with the part? | No — `dependencies()` returns the part and the parameters, de-duplicated |

### Finding 1 — a deleted placement parameter was untested

A placement parameter is a new kind of dependency edge, and nothing checked
what happens when one is deleted. The risk was specific: a missing parameter
read as zero would **move the component without saying so**, which is exactly
the silent rebinding ADR-003 forbids for parts.

Measured, and the implementation was already correct: the graph records one
missing reference, regeneration fails, and `placementOf` returns `NotFound`
naming *"X translation parameter"*. Regression:
`Placement_WhoseParameterIsDeletedFailsLikeAMissingPart`. The test was written
to fail if the value were defaulted, and it passed as written — no expectation
was weakened to accommodate the code.

### Finding 2 — configuration awareness was claimed but untested

ADR-005 chose a parameter-driven intent partly *because* it makes placement
configuration-aware for free. Nothing tested it. Regression:
`Placement_FollowsTheActiveConfiguration` — base gives 25 mm, the active
configuration gives 80 mm, and switching back gives 25 mm exactly, so the
override is applied to the base value rather than written into it.

### Finding 3 — a guard that cannot currently fire

`resolvePlacement` checks that a parameter's resolved value is finite. That
check is **unreachable through the public API**: `Parameter::create` requires
a finite value, and expression evaluation rejects a non-finite result
(`Expression.cpp:461`). It is recorded as defence in depth rather than
presented as tested behaviour, because writing a test for it would require
reaching past the API that prevents it.

### Process defects found in this milestone's own verification

These are recorded because a verification process that reports a false pass is
a worse defect than a bug in the code.

1. **A stale executable was reported as a pass.** A link failed; the previous
   executable stayed on disk; a later test command ran *that* binary, and its
   result — "17 cases, 590 assertions" — was reported as evidence for two
   tests that were not in it. Detected by comparing the discovered case count
   against the number of tests in the file.
2. **A shell wrapper returned 0 while the build failed**, because its final
   command was `echo`. Every verification command now propagates the exit code
   of the command under test.
3. **Over-filtering destroyed the diagnostic.** The build was filtered through
   `grep -E 'error:|FAILED|ninja: build stopped'`; the actual linker line
   matched none of those and was discarded, costing a cycle spent blind.
4. **The link failure was self-inflicted**: a backgrounded full-suite run held
   `bettercad_tests.exe` open for 23 minutes while a build tried to relink it
   (`cannot open output file … Permission denied`). Running the Catch2 binary
   directly is single-process; the qualification uses `ctest -j 8`, which is
   why ~3 minutes became ~23.

Recovery: the tests were re-run on a freshly linked binary, proven fresh by
timestamp (executable newer than every source file) and proven present by
listing both test names explicitly rather than inferring them from a count.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch,
then the whole suite; then the milestone's related tests five times over in
Release and Debug. Run on the frozen tree, `qualify.cmd` recording every exit
code.

| Preset | Configure | Build | C++ TUs | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | exit 0, 418 steps | 400 | 0 | **1301/1301** | 186.21 s |
| `release` | exit 0 | exit 0, 418 steps | 400 | 0 | **1301/1301** | 197.38 s |
| `debug-shared` | exit 0 | exit 0, 418 steps | 400 | 0 | **1301/1301** | 208.18 s |

Repeats, `ctest --repeat until-fail:5` over the placement, assembly,
persistence, object-model, parameter, configuration, CLI and architecture
tests:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **708/708** | 713.49 s |
| `debug` | **708/708** | 746.84 s |

1301 is `P13-COMP-001`'s qualified baseline of 1282 plus this milestone's 19
new cases; nothing was lost or skipped. All fifteen `Placement_*` cases are in
the repeat selection, and each of the two adversarial-review tests ran its
full five iterations in each preset.

Warnings are errors in every preset, so a build exit of 0 **is** the
zero-warning result. The single `ninja: warning: premature end of file;
recovering` in `build-debug.log` is ninja repairing its own `.ninja_log`,
damaged by the interrupted builds described under "Process defects". It is
not a compiler diagnostic.

**The qualified tree is the committed tree.** The eight tree IDs recorded at
22:58:32 were recomputed after the run finished and are unchanged:

```text
apps              fdecf1a820c84cd7a76b07a66b81b1e8974ba691
include           8c00eb65a5ba3909e60374498fa2d4122a938901
src               fc4126b6798513f4173091acd81046ef6b2fd428
tests             088000e32c6d15ba16ddb719135279b182644fee
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps` and `examples` are byte-identical to the trees `P13-COMP-001`
qualified, which is the mechanical proof that neither the CLI nor the
reference models changed.

### The configure gate, exercised

This is the first qualification run under the `qualify.cmd` fixed in
`P13-COMP-001`, which gates on the configure exit code instead of merely
recording it. All three configures returned 0, so the gate passed rather than
skipped; had one failed, the run would have written `build skipped: the
configure failed` instead of continuing and finishing complete.

## Known limitations

- **No `grounded` flag.** ADR-005 lists it in the intent, but it is
  meaningless without a solver: its purpose is to say which component the
  others solve against, and "at least one must be grounded or the assembly is
  free to translate" is a solver constraint. It is deferred to
  `P13-SOLVE-001`. Adding it later costs nothing at the format level, because
  keys are written only when they differ from the default.
- **A placement is relative to the model's origin only.** It cannot be given
  relative to another component or a datum; that is reference infrastructure
  (`P13-REF-001`).
- **Components still produce no geometry.** A placement moves nothing yet:
  no body is transformed, so validation, STEP and STL export are unchanged.
  The transform is available to whatever consumes it next.
- **`info` does not show where a component sits.** The CLI describes which
  part a component places, not its placement. Deliberate: adding output here
  would be untested CLI code, which is the defect `P13-COMP-001` closed.
  `P13-CLI-001` owns it.
- **`drivenValue` repeats `features::detail::drivingValue`.** The original is
  in a private header (`src/features/SolidSupport.hpp`) that a module above
  `features` may not include. The copy is twelve lines over public `Document`
  API. Promoting the original to `core` would remove the duplication and is
  recorded as a follow-up rather than done here, mid-milestone, to a qualified
  module.

## Result

```text
TASK:            P13-XFORM-001 — Component Transforms
IMPLEMENTATION:  ComponentPlacement in core, its resolution in assembly, the
                 placement on ComponentDefinition, setComponentPlacement,
                 and the JSON mapping
TESTS:           19 Catch2 cases in 2 files
VALIDATION:      hand-derived transform mathematics, expected-versus-measured
REGRESSION:      1301/1301 on debug, release and debug-shared from clean;
                 708/708 five times over in release and debug;
                 0 compiler warnings; qualified tree == committed tree
ADVERSARIAL:     3 findings, 0 production defects; 2 new regression tests
RESULT:          PASS
EVIDENCE:        this directory
```

Every item of the milestone's checklist is demonstrated by a test that fails
if the behaviour is wrong, not by inspection. The two claims worth restating
because they are the ones a later milestone will lean on:

**The transform is derived and is not stored anywhere.** There is no cache to
invalidate, so a placement cannot disagree with the transform it means, and
no `.bcad` file can encode a position its own intent does not produce.

**The composition and rotation conventions are pinned by discriminating
cases**, chosen so that the wrong convention gives a *different point* rather
than a slightly different one. A reversed composition order, a transposed
matrix, a passive rotation or a rotated translation each fail a test by a
margin of order 1, not of order 1e-12.

## Revision

| When | What |
| --- | --- |
| 22:19-22:42 | full-suite run held the test binary open; two builds failed to link |
| 22:20 | the two adversarial-review tests added |
| 22:53 | freshly linked binary; both new tests verified present and passing |
| 22:58:32 | qualifying run began on the frozen tree |
| 00:10:44 | qualifying run finished; all fourteen exit codes 0 |

No result from the stale executable is cited anywhere in this document.
