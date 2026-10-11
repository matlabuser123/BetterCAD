# P17-VIZ-001 — FEA Visualisation / Inspection

```text
STATUS:  PARTIAL. The headless inspection layer is complete, mutation-tested
         and QUALIFIED in three presets. The DISPLAY SURFACE is not built, so
         the milestone is NOT complete and its display checkboxes stay
         unticked. See REMAINING_SCOPE.md.

         Qualified: 3792 / 3792 x 3 presets, 937 x 5 in two, 0 warnings.
         This run also REQUALIFIES P17-VALID-001.
```

A trustworthy inspection layer over the already-qualified P17 result, and
nothing more. The invariant:

```text
deformed display = a visual transform of the current derived displacement

NOT  a mutation of the P16 mesh, the CAD geometry, the document parameters
     or the result, and NOT a second solver authority
```

## Evidence

| document | what it holds |
| --- | --- |
| [VIEWER_AUDIT.md](VIEWER_AUDIT.md) | what P16 already provides, done before any production code, with the capability table and the one finding that changed a predecessor |
| [UNIT_BOUNDARY.md](UNIT_BOUNDARY.md) | metres vs millimetres, the single conversion, and what is still owed |
| [MEASUREMENTS.md](MEASUREMENTS.md) | every measured table: channels, cardinality, ranges, extremes, deformation, source guards, immutability, currentness, inspection, overlays, the public kernel, the brief's prohibitions, and the two shared-build traps |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | the gate, and the defect it found: a counter declared, gated on, asserted and never written, hiding a complete uniformly zero field that all 19 earlier tests accepted |
| [MUTATION.md](MUTATION.md) | 61 probes, 54 killed, 7 unreachable — and the **ten real test gaps** they found, with why nothing caught each |
| [FREEZE.md](FREEZE.md) | the three-preset qualification, the eight tree hashes before and after, and the pre-freeze checks |
| [ODR_ROOT_CAUSE.md](ODR_ROOT_CAUSE.md) | a type-name collision I introduced, why every cheap measurement exonerated it, and the one command that found it |
| [REMAINING_SCOPE.md](REMAINING_SCOPE.md) | what is **not** done, and why the milestone stays open |

## The audit's conclusion, which shaped everything after it

**P16 already provides most of this milestone**, and the correct move was to
reuse rather than rebuild:

```text
MeshView                 render buffers in SI metres, stamp-keyed, with the
                         render-index -> NodeId/ElementId bridge already
                         built -- so selection provenance needs no spatial
                         search anywhere
toModelLength            the project's ONLY m -> mm conversion, already
                         centralised in the presentation adapter
MeshDrawStyle            a stale draw style already exists
highlightFor             boundary highlighting through GeometryMeshMap,
                         including the drilled hole wall via a face-INDEX
                         overload that exists precisely because that wall
                         has no FaceName
resultCurrency,          the ONE place that answers "does this result still
analysisState            describe the model"
Viewer::createOffscreen  a working offscreen harness with RenderedImage,
                         at() and coverage() -- which is what makes the
                         remaining display work tractable rather than risky
```

What was genuinely missing was the structural half: result channels, a scalar
classifier, colour banding, deformed positions, result currentness, and the
overlays.

## The finding that changed a predecessor

`StructuralResult` carries `Strain6` and `Stress6` but **not** von Mises, the
principal stresses, the principal strains, the hydrostatic stress or `|u|`. A
viewer restricted to it would have to recompute them, which the brief forbids
as a second von Mises formula and a second eigensolver.

`RecoveredFields` already holds every one as a stored value, with
identity-safe lookups and a `MeshStamp` guard — and `solveStructuralAnalysis`
computed it, extracted two arrays, and threw it away.

```text
StructuralSolveOutcome gained `fields` and `reactions`.
Additive; both were already computed one statement earlier.
P17-VALID-001 REQUALIFIED by this milestone's three-preset run.
```

## The three traps, and where each is actually closed

```text
UNITS        mesh and displacement are SI METRES, OCCT model space is
             MILLIMETRES. P17-VIZ adds NO conversion: the deformed buffer
             leaves this layer in metres and passes through the same
             adapter as the undeformed one, so the two are numerically
             consistent by construction. grep for `1000` and `toModel` in
             the new production code, comments stripped: 0, all four files.

CLASSIFIERS  a classifier that classified zero entities is NOT an all-valid
             visualisation. Closed AND CHECKED at `ScalarField::complete()`
             (`expected > 0`) and at `extremeOf`'s completeness gate -- both
             reachable, because ScalarField is a public aggregate a test can
             hand an empty domain. Probes V14 and V18 die there.

             `field()`'s own three refusals are UNREACHABLE duplicates
             behind the possession invariant, and an earlier revision of
             this evidence wrongly credited them with closing the trap.
             Probes V11, V12 and V13 record that by surviving, and the
             source comments now name the door already closed in front of
             each.

ABSENT != 0  the adversarial review's finding. A value reader that answers
             0.0 where it has nothing to say makes a field of silence look
             like a valid uniform result -- 0.0 is an ordinary displacement
             and an ordinary stress. `channelValue` returns an absent
             optional instead, every enum case is written out in both
             readers, and exactly one reader serves each channel over the
             whole enum.
```

## Totals

```text
33 targeted cases                tests/renderer/StructuralResultViewTests.cpp
3792 / 3792 x 3 presets          debug-ext, release-ext, debug-shared-ext
937 x 5 in two presets           determinism, release-ext and debug-ext
0 warnings, 0 errors             all three builds, -Werror on
61 mutation probes               54 killed, 7 unreachable, 0 void
10 real test gaps found          and closed, each with its reproducer
```

Every scalar is a read of a stored member. The production files contain no
stress formula, no eigensolver, no nodal average, no node move, no `abs()` on a
signed channel, no remesh, no rotational reaction DOF, no geometric recogniser
and no tolerance change.

## Why this is PARTIAL and not PASS

Of `TODO.md`'s 23 checkboxes, **11 are ticked and 12 are not**. The rule
applied, stated so it can be checked: *tick a box only where this milestone's
own evidence demonstrates the deliverable end to end.*

```text
TICKED      inspection (element, node, reaction, principal stress),
            boundary-set highlighting, worst-result navigation, the stale
            visual state, rendering-does-not-mutate, the
            no-duplicate-canonical-data rule, regression, evidence

NOT TICKED  every box whose deliverable is a DRAWN thing: the undeformed and
            deformed displays, the deformation scale factor, the eight
            contours, the load arrows and the restraint symbols
```

A scale factor with nothing to scale visually is not end-to-end, and a contour
whose bands are computed but never drawn is not either. The arithmetic behind
all of them is built, tested and qualified — `StructuralContour` partitions a
field's range into bands with no gap and no overlap, bitwise — but no OCCT
presentation consumes it yet.

That work is named, scoped and de-risked in
[REMAINING_SCOPE.md](REMAINING_SCOPE.md), and it is the next step. It changes a
qualified P16 file and an OCCT adapter, so it deserves its own architecture
pass rather than being appended to the end of this one.
