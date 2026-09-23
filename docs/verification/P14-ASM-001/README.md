# P14-ASM-001 — Assembly Drawing Views

```text
TASK:      P14-ASM-001
STATUS:    PASS
BASELINE:  db25b90 (P14-TOL-001), clean tree, HEAD == origin/main
```

## TASK

Draw a whole assembly: every active occurrence at the transform the solver
published, with the component in front hiding the one behind it, and with each
drawn line still knowing which occurrence drew it.

And then close the item `P14-HLR-001` left open — "validate assemblies with
occlusion" — which was unreachable while a view drew one component.

The milestone's own gate, from `TODO.md`:

```text
assembly views correct
+ configuration/suppression respected
+ occurrence identity preserved
+ assembly HLR correct
```

## BASELINE

`P14-TOL-001` at `db25b90`, verified before anything was written: 1958/1958 on
`debug`, `release` and `debug-shared`, tree clean, `HEAD == origin/main`.

## SCOPE

### In

```text
a view whose subject is THE ASSEMBLY rather than one object
every active occurrence drawn at its solved transform
one hidden-line problem over all of them, so occlusion between components
    is computed rather than assembled afterwards
occurrence identity on every drawn line, through merging and through a
    detail view's crop
sections across components, each cut in its own assembly position, each
    cut face knowing whose material it is
configuration and suppression, consumed from P13 and not reimplemented
the inter-component occlusion validation P14-HLR-001 left open
```

### Out, and why

```text
BOM tables, balloons, quantities   P14-BOM-001. drawnOccurrences() is a view
                                   query and rolls nothing up.
exploded views, assembly-level     not in this milestone's checklist
  section line policy
per-occurrence hatch angles        the architecture has no hatch-assignment
                                   policy yet; occurrence OWNERSHIP is
                                   preserved so a later policy can tell
                                   adjacent components apart (section 14 of
                                   the brief says not to invent one)
drawing commands / undo            P14-CMD-001
headless workflows                 P14-CLI-001
```

## THE ASSEMBLY-VIEW CONTRACT

```text
assembly canonical intent
  -> P13 solve                      (the Regenerator publishes transforms)
  -> assembly::activeComponents()   (active configuration + both layers of
                                     suppression -- P13's answer, asked now)
  -> part body x solved transform   (per occurrence, in assembly space)
  -> ONE hidden-line problem        (every occurrence in it)
  -> per-occurrence extraction      (provenance)
  -> merge, policy, scale, placement
  -> scene
```

Every step of that is asserted somewhere below, and the two that could be got
wrong invisibly — using the canonical placement instead of the solved one, and
classifying each component against itself — each have a test whose only job is
to fail if it happens.

## ARCHITECTURE

One decision was architecturally significant and is recorded as
[ADR-021](../../architecture/decisions/ADR-021-an-assembly-view-is-one-hidden-line-problem.md):
**an assembly view is one hidden-line problem, not one per component**, with
fusing the occurrences and overlaying per-component results both compared and
rejected there.

The rest follows what is already in force:

| Decision | How this milestone obeys it |
| --- | --- |
| ADR-002 — assemblies live in the document | there is no assembly object to name, so a view says `subject = Assembly` and the occurrences come from the document |
| ADR-005 — a component is drawn where the solver put it | a missing solved transform fails the view; it never becomes the identity |
| ADR-011 — intent canonical, projection derived | nothing drawn is stored; the occurrence list is asked for on every call |
| ADR-012 — the drawing reference vocabulary | unchanged: an assembly view names nothing new, and an occurrence is a `ComponentId`, which ADR-004 already permits |
| ADR-019 — hidden-line removal is exact | the same `HLRBRep_Algo`, given more than one shape |

**No parallel system was created:**

```text
the active occurrence set   assembly::activeComponents() -- P13's, unchanged.
                            Configuration and suppression have ONE
                            implementation, so a drawing cannot disagree with
                            the solver about what is in the assembly.
solved transforms           the Regenerator's, through the same
                            TransformLookup P14-VIEW-001 already took.
the section pipeline        P14-VIEW-002's cutBody and sectionGeometry, per
                            occurrence.
the hidden-line pipeline    P14-HLR-001's, extended from one body to a span.
                            The single-body call now CALLS the span one, so
                            there is one implementation and the part path
                            cannot drift from the assembly path.
```

## SOLVED STATE, AND ITS ATOMICITY

An occurrence is drawn as `part body x solved transform`, and the transform is
the one the solver published. There is no fallback:

```text
no solved transform for an ACTIVE occurrence
    -> the WHOLE view fails, naming the occurrence
    -> "... has no solved transform; the assembly did not solve"
```

Not the identity transform, and not a partial drawing. A drawing missing a
component looks exactly like a complete drawing of a smaller machine, and one
drawn at the authoring origin looks exactly like a machine assembled wrongly.

## OCCURRENCE IDENTITY

`ProjectedEdge` gained `source` — the index of the body it came from, which is
the kernel's own answer — and `DrawnEdge` gained `occurrence`, the
`ComponentId`. It is:

```text
set        for every line of an assembly view, and of a view of one component
empty      for a view of a feature, which has no occurrence to name
kept       through the coincident-line merge: the line that wins under ISO 128
           keeps ITS occurrence and does not inherit the loser's
kept       through a detail view's crop (see ADVERSARIAL REVIEW: it was not,
           and that was a defect found there)
```

This is occurrence identity, not edge identity. There is still no stable edge
name in this codebase (ADR-012) and this does not invent one.

## INDEPENDENT GEOMETRY VALIDATION

Every expected value is arithmetic done in the test.

```text
two 40 mm cubes at x = 0 and x = 60   -> projected extent 100 mm
one of them alone                      -> 40 mm
a third at x = 120, TURNED 90 deg      -> spans 80..120, not 120..160,
  about Z                                 because a rotation is about the
                                          component's own origin: the local
                                          box [0,40]^2 maps to [-40,0]x[0,40]
two cubes straddling the cut plane     -> cut area 2 x 40 x 40 = 3200 mm^2
one of them suppressed                 -> 1600 mm^2
a component moved to x = 160           -> extent 200 mm
```

## INTER-COMPONENT OCCLUSION, AND HOW IT IS ASSERTED

A box hides its own back face, so **"this body has hidden edges" is not
evidence of occlusion between bodies** — every box has them. Two of the first
fixtures written here asserted exactly that and passed for the wrong reason
until the assumption was checked.

So every occlusion test compares against the same geometry drawn alone:

```text
partial       front plate covers x in [0,50] of a rear plate spanning
              [0,100]: drawn alone the rear plate shows lines from x = 0;
              drawn together nothing of it is visible left of x = 50, and
              there is a visible piece starting exactly at 50 -- so the
              kernel SPLIT the edge rather than dropping or keeping it whole
complete      a rear cube directly behind an identical front one has NO
              visible line; drawn alone it has many
repeated      two occurrences of ONE part at two depths: the front one shows,
              the rear one does not; swap their solved depths and the answer
              swaps, with the occurrences still distinct
silhouettes   two shafts, one behind the other: a silhouette is not a model
              edge, and it is not exempt from occlusion either -- the rear
              shaft contributes no visible outline
together      drawing each component separately and concatenating gives MORE
  vs apart    visible lines than drawing them together, asserted directly.
              That is the failure mode this design exists to prevent
```

## P14-HLR-001 CLOSURE

`P14-HLR-001` recorded its open item honestly: occlusion between components was
unreachable because a view drew one component. That is now false, and the
validation it asked for exists:

| What the item asked for | Where it is |
| --- | --- |
| partial inter-component occlusion | `HiddenLine_ABodyBehindAnotherIsHiddenWhereItIsCoveredAndVisibleWhereItIsNot`, `AssemblyView_APartlyCoveredComponentIsVisibleWhereItIsExposed` |
| complete inter-component occlusion | `HiddenLine_ABodyEntirelyBehindAnotherDrawsNothingVisible`, `AssemblyView_AComponentInFrontHidesTheOneBehindIt` |
| repeated instances at different depth | `AssemblyView_SwappingWhichInstanceIsInFrontSwapsWhatIsHidden` |
| suppressed occurrence excluded | `AssemblyView_ASuppressedComponentContributesNothingAnywhere`, `AssemblyView_ASuppressedComponentIsNotDrawnAtTheOrigin` |

The box is therefore ticked. It is ticked because those fixtures exist and
pass, not because assembly projection exists.

## SECTIONED ASSEMBLIES

Each occurrence is cut **in its own assembly position**: the solved transform
is applied before the plane is, so a rotated occurrence is cut where it
actually is. Getting that backwards would be invisible in a picture, so it has
its own fixture.

An occurrence the plane does not cross is not an error:

```text
wholly on the kept side      drawn WHOLE -- uncut material, not a void, and
                             it contributes no cut face
wholly on the removed side   absent
crossing                     cut
every occurrence removed     the view fails rather than drawing nothing
```

Which side is which follows the view, not the stored normal: the viewer stands
on the plane's normal side, so material at larger y goes when the normal is
+Y. Two fixtures here asserted the opposite and were wrong; the implementation
followed the documented convention.

Every cut loop carries the occurrence whose material it bounds, so a later
hatch policy can tell two touching components apart. No such policy was
invented (the brief's section 14).

## DETERMINISM

```text
drawn six times                   identical edges, points, merged and
                                  suppressed counts
occurrence order                  ascending ComponentId, asserted sorted
the canonical edge order          gained `source` as its last tiebreaker,
                                  because two bodies CAN draw the same line
                                  -- two plates meeting face to face do --
                                  and without it the two would sort against
                                  each other by whatever the kernel returned
                                  first
configuration A -> B -> A         the third drawing equals the first, edge
                                  for edge
no clock, no seed, no thread, no unordered container, no pointer order
```

## ADVERSARIAL REVIEW

Two defects in the implementation, one determinism hole closed deliberately,
and — worth recording separately — five mistakes in the FIXTURES that would
have made tests pass for the wrong reason.

### 1. A detail view of an assembly lost which component drew each line

`detailGeometry` crops its parent's lines, and it rebuilds each `DrawnEdge`
field by field. The new `occurrence` field was not among them, so every line of
a detail view of an assembly came back saying it belonged to nobody.

```text
found by   reading the diff and asking which other code constructs a
           DrawnEdge -- not by a test, because there was none yet
why it     the drawing looks perfect. The lines are all there, in the right
matters    places, correctly classified. Only the provenance is gone, and
           P14-BOM-001 and P14-STREF-001 are what would have discovered it
fixed      the crop copies it, with a comment saying it was lost here once
test       AssemblyView_ADetailOfAnAssemblyKeepsWhoDrewEachLine
```

### 2. checkView refused every assembly view

The document-level check demanded that a base view's source be an object of the
document. An assembly view names no object, so every one was refused at
creation. Caught immediately by the tests rather than by reading, and fixed by
saying explicitly that an assembly view has nothing to find — with the reason
that an assembly with nothing in it yet is a drawing not finished rather than
one that is wrong, which `projectedGeometry` is where that is judged.

### 3. Two bodies CAN draw the same line, and the order had no tiebreaker

Not found as a failure — found by asking what happens when two components meet
face to face, which two plates bolted together do. Their touching faces project
onto the same rectangle, so the canonical edge order had two entries equal in
kind, visibility, start, end and length. The order between them was then
whatever the kernel happened to return first, which is exactly the kind of
thing that differs between presets.

`source` is now the last tiebreaker in the canonical sort, and
`AssemblyView_TwoComponentsDrawingOneLineDrawItOnce` asserts that no two drawn
lines are the same line and that every survivor still names an occurrence.

### 4. Five fixture errors, and what they have in common

These are recorded because they are the same class of mistake this milestone
exists to prevent: a test that passes while asserting the wrong thing.

```text
"it has hidden edges"    A BOX HIDES ITS OWN BACK FACE. Three assertions
                         claimed that hidden edges proved occlusion BETWEEN
                         bodies; every box has them alone. Every occlusion
                         test now compares against the same geometry drawn
                         by itself, which is the only thing that isolates
                         the question.
grazing boundaries       the first fixture had both plates spanning the same
                         z range, so their outlines coincided at the top and
                         bottom edges and the split under test was masked.
                         Rebuilt so the front plate overhangs in z.
the section side         two fixtures put the component to keep BEYOND the
                         plane. The viewer stands on the normal's side, so
                         material at larger y is what goes. The
                         implementation followed the documented convention;
                         the fixtures did not.
the rotated cube         a rotation is about the COMPONENT's origin, not the
                         middle of the part, so a cube turned 90 degrees
                         about Z and moved to x = 120 spans 80..120 rather
                         than 120..160. The expected value is now written
                         out with that reasoning, not rounded off.
a section's placement    a section view DERIVES its placement from its
                         parent and requires a spacing. The helper supplied
                         a placement and no spacing, and every section test
                         failed at creation.
```

### 5. A process finding: three stale-binary reads

Three times, a test run reported a result from a binary that had not been
rebuilt, because the build had failed and the failure was hidden by tailing
the combined build-and-test output. The tests then "ran" and reported the old
answer.

This is the same trap `P14-DIM-001` and `P14-ANNO-001` each hit once, in a new
disguise. The working rule is now: **check the build's own result before
running anything**, with the test run chained behind it rather than beside it
(`cmake --build ... && echo BUILD OK && <tests>`), so a failed build cannot be
followed by a test result.

### What was attacked and held

| Question | Answer |
| --- | --- |
| Can a component be drawn at its canonical placement instead of its solved one? | No. `occurrenceBody` requires the published transform and fails by name; the fixture places components where the two differ |
| Can a missing transform become the identity? | No — asserted with a lookup that has lost one occurrence's transform |
| Can a suppressed component appear, or be drawn at the origin? | No. It is not in `activeComponents`, so it is never asked for; asserted that the drawing is 40 wide and not 100 |
| Can two instances of one part collapse? | No — three instances of one part, three identities, three spans, asserted separately |
| Can part identity be confused with occurrence identity? | `occurrence` is a `ComponentId` and is EMPTY for a feature view, asserted both ways |
| Can inter-component HLR be done per component? | The design makes it one problem; the test asserts together < apart |
| Can a rear component show through a front one? | No — complete, partial and swapped-depth fixtures |
| Can fully hidden geometry still emit visible silhouettes? | No — the rear shaft contributes no visible outline |
| Can coincident lines duplicate across components? | No — asserted pairwise over every drawn line |
| Can hidden-line OFF shift the assembly on the sheet? | No — bounds identical with it on and off, while the edge count drops |
| Can sectioning happen before the solved transform? | No — the transform is applied in `occurrenceBody`, before any cut; the rotated-occurrence section is the fixture |
| Can configuration switching leave stale geometry? | No — A then B then A gives the first drawing edge for edge, and the same for the section's area and loop count |
| Can a broken solve leave the previous drawing showing? | No. The drawing is derived on every call, so there is nothing kept; asserted by breaking the solve between two draws |
| Can Debug and Release enumerate occurrences differently? | The list is ascending `ComponentId`, asserted sorted; the three-preset qualification covers the rest |
| Can deleting an unrelated component retarget the others? | No — the survivors keep their identities and their line counts |
| Can merged lines lose provenance? | No — every survivor names one of the two components actually present |
| Can suppressed occurrences leak into the section hatch? | No — every loop belongs to the one unsuppressed component |
| Did this implement any BOM logic? | No. `drawnOccurrences` lists occurrences; nothing counts, groups or numbers them |

## FAILURE PATHS

```text
an active occurrence with no solved transform
    "... has no solved transform; the assembly did not solve"
an assembly with no active components
    "... has no active components; a drawing of nothing is not a drawing"
a section that removes everything it draws
    "... cuts away every component it draws, leaving nothing to show"
a section whose plane crosses no material it draws
    "... cuts nothing: its plane passes through no material it draws"
an assembly view that also names an object
    "an assembly view draws every active component and names no single
     object; use an object view to draw one of them"
a child view carrying its own subject
    "a projected view takes its subject from its parent, not one of its own"
drawnOccurrences() on an object view
    "... draws one object rather than the assembly"
a component whose part is in another document
    "... places a part in another document"
```

Nothing partial is ever published: the occurrence bodies are all resolved
before the kernel is called, so a view either draws every active occurrence or
fails.

## PERSISTENCE

A base view writes `subject`, and an assembly view writes no `source`.

```text
round trip          the definition compares equal, and the reloaded document
                    regenerates and draws edge for edge the same
a file with both    refused: "names no single object"
a file with neither the subject key is ABSENT in every file written before
                    this milestone, and those files drew one object -- so its
                    absence reads as Object, which is what they meant.
                    Asserted by stripping the key and loading
```

## KNOWN LIMITATIONS

```text
1  One kernel problem per view, over the whole assembly, and nothing caches
   it. A view drawn N times runs hidden-line removal N times, and the cost is
   superlinear in the face count. P14-HLR-001 left caching out for the same
   reason: a cache needs an invalidation rule decided deliberately.
2  Whether an occurrence crosses the cutting plane is decided from its
   BOUNDING BOX. That is exact for the two answers it gives -- a body whose
   every corner is on one side is wholly on that side -- but a body whose box
   crosses while its material does not falls through to cutBody(), which
   refuses, and the view fails with that diagnostic rather than drawing the
   body whole. A U-shaped component straddling the plane only in its box is
   the case; no fixture here produces one.
3  Half and offset sections across an assembly use the same rule, which is
   correct for the KEPT side (the removed region is always a subset of the
   viewer side) but decides "wholly removed" from the base plane alone. A
   component wholly on the viewer side of a HALF section's base plane, but
   outside the half being cut, is dropped where it should be drawn whole.
   Full sections -- the ones tested here -- are unaffected.
4  No per-occurrence hatch policy: every cut face is hatched by the view's own
   settings. Occurrence ownership is preserved so a later policy can
   distinguish adjacent components, which is what the brief asked for.
5  A component's part in another document is refused rather than resolved;
   external-reference resolution is P13-REF-001's and is not wired into
   drawing.
6  drawnOccurrences() lists occurrences, not quantities or part numbers.
   Nothing here is a BOM.
```

## TESTS

**41 new test cases**: 34 in `tests/drawing/AssemblyDrawingTests.cpp` and 7 in
`tests/core/geometry/HiddenLineTests.cpp`, where the occlusion between bodies
is validated at the level it is computed.

```text
[drawing]     7549 assertions in 301 test cases   (was 6701 in 267)
[occlusion]    163 assertions in   6 test cases
```

Grouped:

```text
solved state      every active occurrence at its solved place; the solved
                  transform and not the authoring origin; no active
                  components refused; a missing transform fails the WHOLE
                  view
identity          three instances of one part stay three occurrences, each
                  at its own span; a component view names its occurrence; a
                  feature view names none
occlusion         complete, partial, swapped depths, silhouettes, and
                  together-versus-apart -- each against the same geometry
                  drawn alone
configuration     A then B then A, edge for edge; a suppressed component
                  contributes nothing anywhere, including to the bounds; and
                  is not drawn at the origin
regeneration      a component moved; one added after the view was made; an
                  unrelated one removed
sections          every component the plane crosses; one it misses left
                  whole; one wholly removed; a ROTATED occurrence cut where
                  it actually is; following the active configuration
hidden lines      turning them off does not move the drawing
coincident        two components drawing one line draw it once, and every
                  survivor still names an occurrence
detail            a detail of an assembly keeps who drew each line
persistence       round trip; a file naming both a subject and a source
                  refused; a file with NO subject still means one object
failure paths     an assembly view that also names an object; a child view
                  carrying its own subject; drawnOccurrences on an object
                  view; a solve that breaks between two draws
determinism       drawn six times, edges, points and counts identical;
                  occurrences in ascending ComponentId order
```

## REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **1999 / 1999** (325.55 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **1999 / 1999** |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **1999 / 1999** |

```text
qualification finished Wed 23/09/2026 16:35:03.38, 0 stage(s) failed
```

Baseline was 1958 tests; this milestone adds **41** — 34 assembly-drawing and
7 hidden-line, which is exactly the arithmetic.

### The determinism gate

```text
repeat release  exit 0   1998 / 1998, each test run five times (1028.42 s)
repeat debug    exit 0   1998 / 1998, each test run five times
```

`ctest --repeat until-fail:5`. The set is the whole suite bar
`gui.launch.smoke`, which launches a window.

The OneDrive filesystem fault that failed a repeat in `P14-DIM-001` and again
in `P14-ANNO-001` did not recur, in either preset. As in `P14-TOL-001`, that
does **not** close it: an intermittent fault looks exactly like this between
occurrences, and `TODO.md` still carries the decision.

### One regression run was invalid, and is not counted

A full Debug regression was run directly with `ctest --preset debug` and
reported **1998/1999**, failing `cli.new.unicode-path`. That failure was an
artefact of the invocation, not a defect:

```text
the test writes a document named "Plåt ✓" and checks the CLI echoes it
qualify.cmd line 26 runs `chcp 65001`, with the comment "the CLI's Unicode
    test needs it"
run again under code page 65001, the same binary and the same tree:
    1/1 Test #1882: cli.new.unicode-path ..... Passed
```

It is recorded here rather than quietly dropped, because "one test failed and
I decided it did not count" is exactly the shape of a real failure being
waved through. The authoritative runs are the three above, which the harness
performs under the documented code page and which pass 1999/1999.

Separately, a first Debug regression was started and then deliberately
**voided**: the detail-view provenance defect was fixed while it was running,
so the binary under test was no longer the tree on disk. It was stopped rather
than reported, and the tree was rebuilt before the run that counts.

### The harness itself

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist, it gives `QUALIFICATION FAILED: 3 stage(s) failed`
and exit 3.

### The qualified tree is the committed tree

Tree IDs from a scratch index, taken three times — before the first build, by
the harness after the last test run, and again before the commit — identical in
all three:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           8e62f2981db26056ad04852f791047eb97f4c112
src               9cc588b0d902a32564880a40f4d9081c82787bc7
tests             d50ef694fe0962f90a249e15944e078a8480bcb3
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**byte-identical to the P14-TOL-001 baseline**: no CLI was touched, no
reference model changed, and the build configuration was not altered to obtain
a pass.

## RESULT

```text
TASK:            P14-ASM-001 -- Assembly drawing views
IMPLEMENTATION:  ADR-021; ViewSubject so a view can say it draws the
                 assembly; every active occurrence resolved to its part body
                 at its solved transform; hiddenLineDrawing over a SPAN of
                 bodies, one kernel problem, results extracted per shape;
                 ProjectedEdge::source and DrawnEdge::occurrence;
                 per-occurrence sectioning in assembly space with each cut
                 loop owning its occurrence; drawnOccurrences(); the
                 single-body call now routes through the span one
TESTS:           41 new; 1999/1999 in debug, release and debug-shared, each
                 from clean; 1998/1998 five times over in release and debug
VALIDATION:      projected extents of 100, 40 and 200 mm and cut areas of
                 3200 and 1600 mm^2, all arithmetic done in the tests; a
                 turned cube spanning 80..120 rather than 120..160 because a
                 rotation is about the component's origin; every occlusion
                 claim compared against the same geometry drawn alone
ADVERSARIAL:     2 defects found and fixed, 1 determinism hole closed, 5
                 fixture errors corrected, 1 process finding recorded
WARNINGS:        0 in all three builds
DETERMINISM:     drawn six times, bit for bit; occurrences in ascending
                 ComponentId order; the repeat gate clean in both presets
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-ASM-001 -> [x]
                 P14-HLR-001 "Validate assemblies with occlusion" -> [x]
CARRIED OPEN:    none. The only carried item, P14-HLR-001's assembly
                 occlusion validation, is closed here.
OPEN DECISION:   move build and test output off OneDrive. The fault did not
                 recur in this milestone, which does not close it.
NEXT:            P14-BOM-001 -- BOM tables / item balloons
```

## FILES

```text
qualification/qualify.cmd               the harness, carried from P14-TOL-001
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree
                                        IDs before the first build and after the
                                        last test run
qualification/configure-*.log           three presets
qualification/clean-*.log               three presets
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               1999/1999 in each preset
qualification/ctest-repeat-*.log        1998/1998 five times over, debug and release
```
