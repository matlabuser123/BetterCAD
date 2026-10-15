# P17-VIZ-001 — FEA Visualisation / Inspection

> **Addendum, P17-QUAL-001, 2026-10-15.** The status block below was written
> after the second of this milestone's four qualification runs and was not
> updated by the two that followed. The milestone is **COMPLETE at 23 of 23**:
> the glyphs were drawn and qualified at `974b62b` (`FREEZE_GLYPHS.md`,
> 3825/3825 x 3) and the three element contours were drawn and qualified at
> `e795acb` (`FREEZE_ELEMENTS.md`, 3835/3835 x 3, "takes P17-VIZ-001 to 23 of
> 23"), which `TODO.md` records with every box ticked. The text below is kept
> as written because the measurements in it are still the measurements; only
> the status line and the section "Why this is PARTIAL and not PASS" are
> superseded. Recorded in `docs/verification/P17-QUAL-001/MILESTONE_AUDIT.md`
> as finding D1.

```text
STATUS:  PARTIAL, 18 of 23 boxes. The headless inspection layer, the
         DEFORMED display and the NODAL contours are built and
         mutation-tested. The ELEMENT contours are BLOCKED on a mapping
         meshing does not have, and the GLYPHS are not drawn.
         See REMAINING_SCOPE.md and DISPLAY.md.

         QUALIFIED: 3806 / 3806 x 3 presets, 951 x 5 in two, 0 warnings,
         eight tree hashes identical before and after.
         The earlier run at b04a9e1 (3792, 937 x 5) also REQUALIFIED
         P17-VALID-001 and is kept in qualification/..
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
| [MUTATION.md](MUTATION.md) | 83 probes over four batches, 74 killed, 7 unreachable, 2 equivalent mutants — and the **fifteen real test gaps** they found, with why nothing caught each |
| [MUTATION_GLYPHS.md](MUTATION_GLYPHS.md) | the glyph probes: two batches plus P5, and why the two methods found DISJOINT sets -- six gaps from reading the pristine sources, two from surviving mutations, and one equivalent mutant kept rather than tested |
| [ADR-043](../../architecture/decisions/ADR-043-a-deformed-display-is-an-override-position-buffer-through-the-one-existing-conversion.md) | why the deformed buffer is an override position span through the ONE existing conversion, and the two designs that were rejected |
| [ADR-045](../../architecture/decisions/ADR-045-a-glyph-overlay-is-a-list-of-segments-in-si-metres-and-the-glyph-data-carries-no-position.md) | why an overlay is a list of SEGMENTS in SI metres resolved in the headless layer, and the two designs that were rejected |
| [ADR-046](../../architecture/decisions/ADR-046-a-tetrahedron-derived-boundary-view-carries-its-owning-element-so-no-meshing-capability-is-needed.md) | why the element contours needed NO meshing capability: build the boundary from the TETRAHEDRA and ownership is free, with the two routes that were rejected |
| [FREEZE.md](FREEZE.md) | the three-preset qualification at b04a9e1, the eight tree hashes before and after, and the pre-freeze checks |
| [FREEZE_DISPLAY.md](FREEZE_DISPLAY.md) | the display surface's own three-preset qualification — 3806 x 3, 951 x 5, and why there were three attempts |
| [FREEZE_GLYPHS.md](FREEZE_GLYPHS.md) | the glyphs own three-preset qualification -- 3825 x 3, 970 x 5, 3h21m00s, two fingerprint witnesses, and the two false starts where `cmd /c` ran nothing and returned 0 |
| [FREEZE_ELEMENTS.md](FREEZE_ELEMENTS.md) | the element contours own three-preset qualification -- 3835 x 3, 980 x 5, 3h15m17s, the meshing module checked as a GATE to be untouched, and the verdict script that cried VOID fixed and pre-validated against the glyph run |
| [DISPLAY.md](DISPLAY.md) | the deformed display and the contours: the pixel measurements, the 1-versus-4807 colour count, and the element-contour blocker diagnosed from a real failure |
| [DISPLAY_GLYPHS.md](DISPLAY_GLYPHS.md) | the load and restraint glyphs: the conventions a test can hold, the 278-pixel measurement and why it is not tuned away, and the five gaps the review found before a probe ran |
| [ELEMENT_CONTOURS.md](ELEMENT_CONTOURS.md) | the three boxes that were BLOCKED for two milestones on a wrong conclusion from a correct diagnosis, the boundary derived from the tetrahedra, and the coincidence check that validates it against independently built data |
| [DEFECT_RESTRAINT_COMPONENTS.md](DEFECT_RESTRAINT_COMPONENTS.md) | a defect in COMMITTED, QUALIFIED code: inspectNode unioned every restraint resolution, so with two different masks in one analysis every restrained node reported FIXED |
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
47 targeted cases                tests/renderer/StructuralResultViewTests.cpp
                                 37 headless + 10 that need a GPU, tagged
                                 [frame] so they are identifiable
3806 / 3806 x 3 presets          debug-ext, release-ext, debug-shared-ext
951 x 5 in two presets           determinism, release-ext and debug-ext
                                 (3792 and 937 x 5 at b04a9e1, before the
                                 display surface -- that run is in
                                 qualification/, this one in
                                 qualification-display/)
0 warnings, 0 errors             every build, -Werror on
83 mutation probes               74 killed, 7 unreachable, 2 equivalent
15 real test gaps found          and closed, each with its reproducer
```

Every scalar is a read of a stored member. The production files contain no
stress formula, no eigensolver, no nodal average, no node move, no `abs()` on a
signed channel, no remesh, no rotational reaction DOF, no geometric recogniser
and no tolerance change.

## Why this is PARTIAL and not PASS

Of `TODO.md`'s 23 checkboxes, **18 are ticked and 5 are not** — counted from
the file, not from this sentence. The rule applied, stated so it can be
checked: *tick a box only where this milestone's own evidence demonstrates the
deliverable end to end.*

```text
TICKED      the undeformed and deformed displays and the deformation scale
            factor (ADR-043, measured against the CAD solid's own scale),
            the FOUR NODAL contours (ADR-044, 1 colour vs 4807),
            inspection (element, node, reaction, principal stress),
            boundary-set highlighting, worst-result navigation, the stale
            visual state, rendering-does-not-mutate, the
            no-duplicate-canonical-data rule, regression, evidence

NOT TICKED  the three ELEMENT contours -- BLOCKED, not unfinished -- and the
            load arrows and restraint symbols, whose data is complete and
            qualified and which need no new capability to draw
```

Both displays are now end to end, and each is measured against something the
test did not compute itself:

```text
THE DEFORMED BUFFER reaches model space at the CAD solid's own scale

  the CAD solid alone                       42036 covered pixels
  the deformed mesh alone                   42036
  the solid AND the deformed mesh           42036
  the undeformed mesh alone                 42036

  All four exactly equal. The solid's scale comes from the KERNEL, not from
  the test, which is why this cannot agree with a wrong answer the way a
  self-computed expectation can. P16-VIZ-001 measured 32674 and 54120 for
  the defect this shape catches -- one silhouette, two scales.

THE CONTOUR reaches the screen as many colours

  uniform fill, distinct non-background colours      1
  twelve-band contour                             4807

  And 4807 rather than 12 is the second thing it says: the GPU is
  INTERPOLATING between band colours across each facet, which is what a
  nodal channel requires. Clearing the contour returns a BYTE-IDENTICAL
  image.
```

**The three element contours are blocked, not unfinished**, and the difference
is itself tested. `MeshView::volumeBoundaryOf` names Triangle3 **surface**
element ids while an element channel's values are per **tetrahedron**, and
`meshing::Triangle` carries no owning tetrahedron. The boundary view is refused
*by name*; the same function on a `tetrahedraOf` view produces correct flat
per-facet bands. What is absent is a mapping in the qualified P16 meshing
module — a scope decision — not the colouring.

The remaining work is named and scoped in
[REMAINING_SCOPE.md](REMAINING_SCOPE.md) and measured in
[DISPLAY.md](DISPLAY.md).
