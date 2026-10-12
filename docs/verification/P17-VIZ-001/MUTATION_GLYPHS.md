# P17-VIZ-001 — the glyph mutation probes

```text
TWO BATCHES, and the second exists because the first found two real gaps.

  batch 1   the glyph overlays           21 probes
            17 killed, 3 survived, 1 VOID
  batch 2   the gaps found by READING    7 probes
            7 killed, including re-runs of both survivors
  P5        the last unprobed new test   1 probe

PROBES RAN BEFORE THE QUALIFICATION, not after it. That order was settled
earlier in this milestone at the cost of two aborted runs.
```

## Why there are two batches

Batch 1 probed the glyph code. While it ran, its four source files were being
rewritten every two and a half minutes, so the only safe thing to read was the
**pristine copies** — and reading those is what produced batch 2.

Four test gaps came out of that reading, and **not one of them was a mutation
batch 1 contained.** Two more came from batch 1 survivors. The two methods
found disjoint sets:

```text
found by READING the contract       4   none reachable by any mutation in
                                        batch 1
found by a SURVIVING mutation       2   G12, G18
```

That is the argument for doing both, stated as a measurement rather than a
preference.

## Batch 1 — the glyph overlays

21 probes, 22:57 to 23:50. Each verdict is the number of the 60 cases that
failed.

| Probe | Mutation | Verdict |
| --- | --- | --- |
| G1 | the head and tail swapped | KILLED (4) |
| G2 | the shaft ignores the length | KILLED (2) |
| G3 | only TWO barbs | KILLED (5) |
| G4 | the barbs start from the tail | KILLED (1) |
| G5 | the perpendicular seed is fixed | KILLED (1) |
| G6 | the barb fraction is one | KILLED (1) |
| G7 | a zero force is drawn | KILLED (1) |
| G8 | the tooSmall guard removed | KILLED (1) |
| G9 | skipped never counted | KILLED (1) |
| G10 | glyphs never counted | KILLED (6) |
| G11 | a missing node is dropped | **VOID** |
| G12 | the scale guard removed | **SURVIVED** |
| G13 | the axis table permuted | KILLED (1) |
| G14 | the bar is not centred | KILLED (2) |
| G15 | every component gets a bar | KILLED (1) |
| G16 | the bar length guard removed | KILLED (1) |
| G17 | the component mask unions all | KILLED (1) |
| G18 | the numbering check removed | **SURVIVED** |
| G19 | the millimetre conversion gone | KILLED (1) |
| G20 | the overlay becomes selectable | **SURVIVED** |
| G21 | the empty-overlay guard gone | KILLED (1) |

Three of these are standing regressions for defects found and fixed during the
adversarial review, and each dies to exactly one case — the one written for it:
**G5** (an axis-aligned arrow keeps its barbs), **G13** (a restraint bar lies
along the component it depicts) and **G19** (the overlay is drawn at the mesh's
own scale).

The restore stage rebuilt and re-ran: 60/60, and all four source hashes back to
their baselines.

## The void probe, and why it is not a kill

**G11 was written `'if (node != nullptr \&\& node == nullptr)'`.** Inside single
quotes those backslashes are literal, so gcc reported

```text
error: stray '\' in program
```

A failed build leaves the **previous** binary in place, so the harness checks
the build result before believing any test outcome — which is the only reason
this was recorded as VOID rather than banked as a kill.

The mutation was badly chosen as well: `node != nullptr && node == nullptr` is
always false, so control would fall through and dereference a null node. That
is undefined behaviour, not a test verdict.

**G11b replaced it** with the plausible wrong implementation — drop the arrow
silently (`++out.skipped; continue;`) instead of refusing it. It died to an
**existing** case, `AnOverlayRefusesWhatItCannotPlace`. So the refuse-don't-drop
property was covered all along, and the escaping bug had been concealing a pass
rather than a gap.

## The three survivors, each resolved rather than logged

### G12 — the scale guard. A real gap, but only in one case.

`arrowOverlay` opens with `if (!isUsableArrowScale(scale))`. Removing it passed
all 60 cases, and the reason is that **`displayArrowLength` repeats the
identical check** — the same predicate, the same `ErrorCode`, a message that
renders the same. For a non-empty arrow list the early guard is redundant and
removing it is an equivalent mutant: the refusal simply moves from before the
loop to the first arrow inside it.

**With an empty list the loop never runs.** A bad scale then returns a
*successful empty overlay* instead of refusing, and the caller's bug is
swallowed. That is the only observable difference, and it is why the guard is
kept rather than deleted as duplicate: the contract is argument validation, not
work-dependent validation.

Closed by `AnEmptyArrowListStillRefusesABadScale`, which also covers the
identically-shaped `reference` guard and the reaction entry point that shares
the implementation.

### G18 — the numbering identity guard. A real gap, and it refuted a claim of mine.

Removing `if (!numbering->describes(mesh))` from `inspectNode` passed all 60.

**I had recorded this guard as unreachable through the public API**, reasoning
that `prepareStructuralRestraints` binds restraints to the numbering they were
prepared with, so a disagreeing numbering would arrive with disagreeing
restraints and the restraints check would fire first.

That was wrong. `describes` is

```cpp
mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size()
```

and a `MeshStamp` identifies the **builder**, so a numbering from an
*independent document* is stale with respect to this mesh however similar the
two meshes are — while the restraints still describe it. The restraints check
passes and the numbering check is the only thing left.

The defect it admits is not a crash. `indicesOf` returns indices from the wrong
numbering, the `ConstraintSet` is asked about *those*, and a plausible-looking
mask comes back for degrees of freedom that are not the node's — so a support
symbol is drawn for components that are not held.

Closed by `AStaleDofNumberingIsRefusedNotTrusted`, which asserts the premise
rather than assuming it, including `held->describes(mesh)` — the line that makes
the case actually reach the numbering check instead of being refused earlier.
**P4 then showed the same guard in `restraintSymbolOverlay` was equally
unprobed**, and the same case covers it.

### G20 — an equivalent mutant. No test added, and that is the finding.

`Display(presentation, 0, -1, false)` becomes `Display(presentation, 0, 0,
false)`, making the overlay selectable. But
`OverlayPresentation::ComputeSelection` is **deliberately empty**, so in either
mode the presentation owns no sensitive entity and can neither be picked nor
block a pick. Nothing observable differs.

The two are redundant **by design**: the empty `ComputeSelection` delivers the
property and `selMode -1` is defence in depth for the day someone populates it.
Writing a test here would mean populating `ComputeSelection` to make a probe
fail — changing production code to serve a test. `DisplayedObject` carries
`{id, source, kind, visible, stale}` and no selectable flag, so there is nothing
to assert from outside the adapter either.

Recorded as equivalent, with the reason, rather than left looking like an open
gap.

## Batch 2 — the gaps found by reading

7 probes, 00:01 to 00:20, against the 66-case suite. **The baseline was
verified green first**, because a probe batch on a red baseline reports KILLED
for every mutation — the pre-existing failure is counted as the kill. That gate
fired twice on my own broken test and refused to run the probes both times.

| Probe | Mutation | Verdict | Killed by |
| --- | --- | --- | --- |
| G12 | the scale guard removed | KILLED (1) | `AnEmptyArrowListStillRefusesABadScale` |
| G18 | inspectNode numbering check | KILLED (1) | `AStaleDofNumberingIsRefusedNotTrusted` |
| P4 | overlay numbering check | KILLED (1) | `AStaleDofNumberingIsRefusedNotTrusted` |
| P1 | null numbering assumes fixed | KILLED (1) | `WithoutTheNumberingTheComponentsAreUnknownNotFixed` |
| P2 | displayOverlay takes any id | KILLED (1) | `ADisplayRefusesAnInvalidSourceHandle` |
| P3 | every role in one colour | KILLED (1) | `AReactionIsNotDrawnInALoadsColour` |
| G11b | a missing node is skipped | KILLED (1) | `AnOverlayRefusesWhatItCannotPlace` |

Each mutation is the **plausible** wrong implementation, not an implausible one.
P1 is the clearest example: "if we do not know which components are held,
assume the worst" is what a developer would actually write, and it survives
every probe in batch 1 because G17 replaces the component loop
*unconditionally* — which the two numbering-supplied cases already kill.

P3 needed `(void)&reactionColour; (void)&restraintColour;` to keep `-Werror`
from turning it into a compiler kill, the same trap G11 fell into from the other
direction.

## The six cases added, and the guards they close

```text
WithoutTheNumberingTheComponentsAreUnknownNotFixed
    null numbering leaves restrainedComponents EMPTY. The branch RAN in an
    existing case; nothing held the mask.
ADisplayRefusesAnInvalidSourceHandle
    displayOverlay AND displayMeshInternal. No test covered an invalid source
    handle for ANY display entry point.
AReactionIsNotDrawnInALoadsColour                              [frame]
    a colour per role, which ADR-045 promises and nothing measured.
ARestraintOverlayRefusesAnotherMeshsRestraints
    restraints.describes(mesh) in the overlay. inspectNode's crossed-mesh case
    cannot cover it -- that one is refused on the FIELDS first.
AnEmptyArrowListStillRefusesABadScale
    the scale and reference guards with nothing to draw.
AStaleDofNumberingIsRefusedNotTrusted
    numbering.describes(mesh) in BOTH entry points.
```

Four of the six are guards that were **written, documented, and never reached**,
because the only callers in the tests produced correct input by construction.
That is the fourth time this shape has appeared in P17-VIZ-001.

## Two fixture failures that were the product refusing, not a defect

The stale-numbering case took three attempts, and the first two failures are
worth recording because both looked like defects in the guard under test:

```text
1. change the mesh control, requireReport, then solve
     "meshing control 3 has no mesh that describes the model:
      stale: the meshing intent changed"
   requireReport regenerates the DOCUMENT, not the mesh.

2. also call mesher.generate, then solve
     "load:1: its node:2 belongs to a different mesh generation, so it
      names no node of this one"
   endLoadList builds one NodalForceLoad per boundary node, each carrying a
   MeshStamp AND a NodeId. Installed loads cannot survive a remesh, so
   re-solving on a second mesh is not possible with that fixture at all.
```

Both are P17-VALID-001's solve gate working exactly as designed. The third
version drops the remesh entirely and takes its stale numbering from an
independent document.

## Accounting

```text
probe RUNS                     30   21 + 7 + P5 twice
distinct mutations             27   G1-G21, G11b, P1-P5
  killed                       25
  equivalent mutant             1   G20, documented
  void, superseded              1   G11 -> G11b

cases                     60 -> 66  (14 need a GPU, 52 headless)
real test gaps closed             6
guards newly covered              8   (two cases cover two call sites each)
```

### P5 — the last new test to get a probe

Every other case added here had its teeth proven by a probe.
`ARestraintOverlayRefusesAnotherMeshsRestraints` covers
`restraintSymbolOverlay`'s `restraints.describes(mesh)` guard, which no probe in
either batch touched. A remesh repeats node ids, so without that guard the bars
would be drawn at the **wrong nodes** and look entirely plausible.

```text
P5   if (!restraints.describes(mesh))  ->  if (false)
```

**P5 SURVIVED on its first run, and what it caught was my own test.**

`ARestraintOverlayRefusesAnotherMeshsRestraints` passed
`prepared.numbering`, which belongs to the FIRST mesh. So with the restraints
check deleted the call **still refused** -- by the numbering check one line
below it -- and the case passed under the very defect it was written for.

The assertion made it worse rather than catching it:

```text
CHECK_THAT(message, ContainsSubstring("different mesh"))

  matches BOTH:
    "these prepared restraints were resolved against a different mesh"
    "this degree-of-freedom numbering was built for a different mesh generation"
```

That is the MISDIRECTED shape: an assertion that is TRUE, and that passes under
the defect it exists to detect. It is the third time this shape has cost a
probe in P17, and the first time it has appeared in a test written *to close a
gap a probe found* -- so the probe validated the test, which is the whole point
of writing one per case.

**Two fixes, because either alone is still weak:**

```text
1. a FRESH numbering built for the SECOND mesh, so the restraints check is
   the only guard that can fire. Asserted, not assumed:
     REQUIRE(secondNumbering->describes(second));
     REQUIRE_FALSE(prepared.restraints->describes(second));
2. a substring UNIQUE to the restraints message: "prepared restraints".
```

The sibling case `AStaleDofNumberingIsRefusedNotTrusted` was already specific
-- it asserts `ContainsSubstring("mesh generation")`, which only the numbering
message carries -- which is why G18 and P4 both died to it.

**The re-run KILLED it**, by exactly the case it names:

```text
P5  overlay restraints check  KILLED  (1 tests failed out of 66)
      2906 - unit.StructuralViz_ARestraintOverlayRefusesAnotherMeshsRestraints
```

So every case added here has had its teeth proven by a probe, which is the
standard the rest of this milestone was held to.

## The P5 lesson, stated plainly

A test written to close a gap a probe found can itself pass under the defect it
names. The probe is what catches that, and only if the probe is written. Six new
cases needed seven probes, because one of them was wrong the first time.
