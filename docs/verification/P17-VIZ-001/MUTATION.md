# P17-VIZ-001 — mutation probes

```text
RESULT (the result-view and inspection layer, 35 probes)

    29 KILLED
     6 SURVIVED, every one of them unreachable with the reason derived
     0 VOID
     0 anchor failures
     0 compiler kills in the final run

The probe set found FIVE real test gaps. All five are closed, and the
probes that found them now kill.
```

A test that cannot fail proves nothing, and "this test would catch that" is an
expectation rather than a measurement. Each probe below is a single defect
introduced into the production source, built, and run against the milestone's
own test filter. A probe that **survives** is either a genuine gap or a branch
nothing can reach — and the difference is **derived** for every survivor, never
assumed.

## Protocol

```text
ONE DRIVER, and nothing else touching the build root
  A harness has outlived its own completion notification in this project
  before, and spent twenty minutes overwriting source edits. The driver's
  process tree is verified dead by PID before any other work resumes --
  never by the notification.

RESTORE FROM A HASH-VERIFIED PRISTINE COPY, before every probe and after it

ONE VERIFIED LITERAL SUBSTITUTION
  The harness dies on a missing OR AMBIGUOUS anchor rather than editing the
  wrong line or editing two. Every anchor is dry-run against the baseline
  before any build time is spent, because an anchor that silently failed
  would report a survivor for a mutation that was never applied. That dry
  run earned its keep three times: it caught three stale anchors in the
  contour batch -- one whose line the narrow-range fix had changed, and two
  pointed at the .cpp for a function defined in the header.

MUTATIONS MUST COMPILE
  `-Werror=unused-variable` turns "drop a term" into a COMPILER kill, which
  says nothing about the tests. V20 was exactly that in the first run:
  dropping `mesh` from `describes()` orphaned the parameter. `(void)`-cast,
  it became a real test kill.

A COLLIDED BUILD IS VOID, NOT A KILL
  "ld returned 1" on a one-token mutation means something else touched the
  build root, not that the mutation was caught.

AND THE RUN ENDS WITH A RESTORE BUILD
  A restored source is not a restored binary: a harness that restores
  without rebuilding leaves the MUTANT executable in place, and the next
  ctest failure looks exactly like the fix being broken. Both runs ended
  with a restore build, a re-hash of all four files, and the filter green.
```

### The guard that was wrong, and how it was caught

The script that refuses to edit source while a probe runs used
`ps aux | grep probe.sh`. **It returned zero with three driver shells
running** — that `ps` cannot see them. It would have let an edit land
mid-probe, which is the collision that produces a void "linker error".

Replaced with a `Win32_Process` query. The first version of *that* then
matched **its own invoking shell**, because the tool's command line contained
both `probe.log` and `guard.sh` and the pattern was a loose substring. Anchored
to the end of the command line, and then verified in **both** directions: it
reports CLEAR with nothing running, and BUSY against a real driver.

## Results

### A — units and the deformed buffer

| probe | result |
| --- | --- |
| V1 the deformation scale ignored | KILLED |
| V2 deformed multiplies the position | KILLED |
| V3 a negative scale allowed | KILLED |
| V4 scale 0 no longer undeformed | KILLED |
| V5 a metres-to-mm factor smuggled in | KILLED (2 tests) |

### B — channels

| probe | result |
| --- | --- |
| V6 tau_yz and tau_zx transposed | KILLED (2 tests) |
| V7 `\|u\|` returns a component | KILLED (3 tests) |
| V8 a signed channel plots `abs()` | **gap → now KILLED (3 tests)** |
| V9 von Mises declared nodal | KILLED (7 tests) |
| V10 a norm declared signed | KILLED |

### C — the classifier

| probe | result |
| --- | --- |
| V11 an empty domain accepted | SURVIVED — unreachable |
| V12 an incomplete field accepted | SURVIVED — unreachable |
| V13 a non-finite value accepted | SURVIVED — unreachable |
| V14 `complete()` drops `expected > 0` | KILLED |
| V15 the extremum seeded at zero | KILLED (3 tests) |

### D — extremes and source guards

| probe | result |
| --- | --- |
| V16 ties go to the last entity | KILLED |
| V17 `MaximumMagnitude` loses the sign | **gap → now KILLED** |
| V18 `extremeOf` drops the completeness gate | KILLED |
| V19 `describes()` ignores the source | **gap → now KILLED** |
| V20 `describes()` ignores the mesh | compiler kill → **now KILLED** |

### E — inspection, overlays and state

| probe | result |
| --- | --- |
| V21 restraint read from the displacement | **gap → now KILLED** |
| V22 the reaction sign reversed | KILLED |
| V23 the pressure resultant negated | KILLED |
| V24 every restraint drawn as fixed | KILLED |
| V25 a zero vector given a direction | KILLED |
| V26 the arrow scale ignored | KILLED |
| V27 a stale result drawn as current | KILLED |
| V28 stale-on-current ignores the mesh | SURVIVED — unreachable |
| V29 stale reasons never populated | KILLED |
| V30 a load target silently empty | KILLED |

### F — the adversarial-review fix

| probe | result |
| --- | --- |
| V31 a nodal absent becomes 0.0 | KILLED |
| V32 an element absent becomes 0.0 | KILLED |
| V33 the `skipped` refusal removed | SURVIVED — unreachable |
| V34 `complete()` drops `skipped == 0` | **gap → now KILLED** |
| V35 `ChannelUnavailable` not `Internal` | SURVIVED — unreachable |

## The five gaps, and why nothing caught them

Four of the five are one family: **a defect that transforms every value is
invisible to a test that checks only structure.** The cardinality test counts
values and brackets them by the range; the sweep test recomputes the extremes
*from the published values*. Neither can see `abs()`.

### V8 — `abs()` on a signed channel

`StructuralViz_ASignedChannelKeepsItsNegativeValues` iterates seven channels
and ends:

```cpp
REQUIRE(channelsWithNegatives > 0);
```

An **existential standing in for seven assertions**. With `abs()` on Ux alone,
the other six are still negative somewhere, so the count is still positive. The
non-vacuity guard was doing duty as the assertion.

And the systematic half: **nothing in the suite compared a channel's values
against their source entity by entity.** Even the new kernel case missed it,
because its fixture's `x` was positive and `abs()` was the identity on it.

### V17 — `abs()` on the value `extremeOf` reports

The extreme test checked *which* entity a magnitude extreme names and that the
magnitude is the largest — never that the value keeps its sign. For a
compressive peak that is the whole answer: "the most stressed element is at
5.0e6 Pa" and "at −5.0e6 Pa" are different engineering statements and only one
is true. The production comment already called it a contract — *"the magnitude
was the search key and not the answer"* — and nothing asserted it.

### V19 — `describes()` ignores the source

Every existing source-guard case changes the **mesh**, so the mesh stamp alone
catches it. The case the header is explicit about was untested:

> two results of the same mesh can have identical node and element counts and
> come from different materials

A documented contract with no test.

### V21 — restraint read from the displacement

`StructuralViz_RestraintComesFromTheRestraintsNotTheDisplacement` is thorough,
and **every one of its sections agrees with the mutation**:

```text
a FIXED support holds all three components, so a restrained node sits at
exactly zero and the guess says restrained      -- right by accident
a free node under load has a non-zero magnitude and the guess says
unrestrained                                     -- right by accident
the "without the restraints" section never executes the mutated line
the cross-mesh section is refused before reaching it
```

Its third section has the right idea: it counts the free nodes that happen to
sit at zero displacement — and only `INFO`s the count. **A discriminator that
is printed rather than asserted is a diagnostic, not coverage.**

### V34 — `complete()` drops `skipped == 0`

Reachable, because `ScalarField` is a public aggregate — just untested. This is
the difference between an unreachable guard and an **untested** one, and
`skipped` was the latter.

## How the five were closed

```text
StructuralViz_EveryChannelsValuesAreItsSourcesValues
    all 15 channels, every value compared EXACTLY against the stored member
    read straight from RecoveredFields via displacementOf and fieldsOf -- a
    second, INDEPENDENT statement of the channel-to-member mapping, because
    asking the view for the mapping under test would compare the production
    path with itself. Identity comes from the field's own parallel id array,
    never a positional index. Non-vacuity is a product, not a sum:
        comparisons == 4 * nodes + 11 * elements

StructuralViz_EverySignedChannelKeepsTheSignOfEveryValue
    per channel, not "at least one of seven": Ux and Uy must each span BOTH
    signs. Plus the direction safe for all 15 -- a channel the descriptor
    declares non-negative must not produce a negative value.

StructuralViz_AMagnitudeExtremeReportsTheSignedValue
    a hand-built field whose largest magnitude is NEGATIVE and sits at
    neither end of the array, so a defect returning the first, the last, or
    the signed maximum cannot pass by position.

StructuralViz_AViewRefusesAResultOfTheSameMeshFromAnotherSource
    a MATERIAL edit, which leaves the mesh current -- so the two results
    share a MeshStamp, a node count and an element count and differ only in
    source. Asserted as preconditions, so the case cannot silently stop
    testing what it claims.

StructuralViz_RestraintSurvivesANonZeroDisplacement
    a COMPONENT-ONLY restraint: Uz alone on a side face, beside the fixed
    cap so the model stays constrained. Restrained nodes then have a
    non-zero magnitude, and the guess and the truth finally separate. The
    count of such nodes is REQUIREd, not printed.

StructuralViz_ASkippedEntityMakesAFieldIncomplete
    a hand-built field where every other counter is consistent, so only the
    skip can make it incomplete -- plus the two consumers that gate on
    completeness, so the term is load-bearing and not merely observable.

three fixtures given mixed signs
    kernel displacement (-1, +2, -3)e-6, was (+1, -2, +3)e-6
    element yz = -55 Pa, was +55; |yz| != |zx| either way, so a
    transposition still cannot pass
```

### Why the 23 earlier kills were not re-run

Adding a test makes the suite a strict superset, and a mutation the old suite
killed is still killed by a superset. **So kills are preserved under test
addition and only survivors need re-probing** — 12 probes rather than another
35. The re-run confirmed all five gaps now kill and the six unreachables still
survive.

## The six survivors, each unreachable — derived, not assumed

```text
V11  an empty domain
     `of()` refuses a result whose nodal OR element domain is empty, so
     holding a StructuralResultView already proves both counts positive.
     `field()`'s own check cannot fire.

V12  an incomplete classification
     Unreachable by ARITHMETIC, which is stronger than an invariant. Every
     entity goes down exactly one of three paths, so
     classified + skipped + nonFinite == expected always; the other two
     counters have already returned non-zero above; therefore
     classified == expected here, necessarily.

V13  a non-finite value
     A qualified result cannot contain one -- StructuralResult::create and
     P17-VALID-001 both refuse it -- and RecoveredFields cannot be
     synthesised past them: private constructor, exactly one friend
     (ADR-036).

V28  stale-on-a-current-mesh ignoring the mesh
     Derived through four links, each read rather than assumed:
         mesh not Current
           -> prepare() fails with InputProblem::MeshStale
           -> requireStructuralModel fails
           -> currentResultSource fails
           -> analysisState returns InputsUnavailable, NEVER SolvedStale
     So (mesh stale AND analysis == SolvedStale) cannot occur, and the
     `meshDescribesModel()` conjunct cannot be reached independently.

V33  the `skipped > 0` refusal
V35  ChannelUnavailable mapped to Internal rather than FailedPrecondition
     Both sit behind `channelValue` never returning absent on the domain
     `field()` chose it for -- which the whole-enum agreement assertion now
     guarantees. The BEHAVIOUR is reachable and tested directly on the
     public kernel; only the composed refusal is not.
```

Every one is kept rather than deleted, and the reason is the same in each case:
each guards against a disagreement between two tables, or a future caller that
reaches this layer another way. Deleting them to reach "35 of 35" would remove
real defence and report a worse result as a better one — the disposition
P17-POST-001 recorded for its own six unreachable diagnostics.

**And the evidence was corrected where it credited the wrong guards.** An
earlier revision presented V11, V12 and V13 as this milestone's closure of the
P16 "classified nothing, therefore all valid" trap. They are not: the trap is
closed **and checked** at `ScalarField::complete()`'s `expected > 0` and at
`extremeOf`'s completeness gate, both reachable through hand-built fields, and
probes **V14 and V18 are killed there**. The three comments in the source now
say so, naming the door already closed in front of each and the probe that
records it.

## Contour and legend batch

26 probes against `StructuralContour.cpp` and its header, same protocol, same
filter (the whole 33-case `StructuralViz_` set rather than a contour-only
alternation — it costs ~30 s a probe and it reveals a contour mutation caught
by a non-contour test).

```text
25 KILLED
 1 SURVIVED, unreachable
 0 VOID
 0 compiler kills

Five real gaps, all five now killing.
```

| probe | result |
| --- | --- |
| C1 the last band rounds its top | KILLED (3 tests) |
| C2 boundaries computed per band instead of shared | KILLED |
| C3 every band closed above | KILLED (3 tests) |
| C4 no band closed above | KILLED (3 tests) |
| C5 the partition check allows gaps | KILLED |
| C6 the partition check ignores the ends | **gap → now KILLED** |
| C7 two closed bands allowed | KILLED |
| C8 the one-band collapse removed | KILLED (2 tests) |
| C9 the `uniform` flag never set | KILLED (2 tests) |
| C10 an incomplete field banded | KILLED (2 tests) |
| C11 the band-count range dropped | KILLED |
| C12 the ceiling kept, the floor dropped | KILLED |
| C13 the legend mismatch ignored | KILLED |
| C14 the range ignored, the name checked | KILLED |
| C15 the partition not checked at use | KILLED |
| C16 an unbanded value clamped | SURVIVED — unreachable |
| C17 `bandOf` computed arithmetically | KILLED |
| C18 `contains()` closes every band | **gap → now KILLED** |
| C19 `contains()` opens every band | KILLED (3 tests) |
| C20 the ramp flattened to one colour | **gap → now KILLED** |
| C21 the band fraction loses its `+0.5` | **gap → now KILLED** |
| C22 the legend borrows no unit | KILLED |
| C23 the range re-scanned from the values | **gap → now KILLED** |
| C24 the band floor drops to one | KILLED |
| C25 the narrow-range collapse removed | KILLED |
| C26 the collapse fires too eagerly | KILLED (3 tests) |

### C20 — my own assertion was a disjunction

The worst of the five, because the rule it broke is one this project has
already recorded twice:

```cpp
REQUIRE((a.red != b.red || a.green != b.green || a.blue != b.blue));
```

Flattening **only** the red channel leaves green and blue varying, so the
disjunction holds and the probe survives. Replaced by the negated conjunction
— "not all three equal" — and by a per-channel count: red, green and blue must
each take at least two distinct values across the bands.

That is the third time a weakened assertion has cost this project a mutation
kill, and the first time it was written *after* the lesson was recorded.

### C18 and C19 are asymmetric, which is why only one of them survived

```text
contains() OPENS every band   -> the maximum belongs to nothing
                              -> bandOf(maximum) == bands.size()
                              -> KILLED by the partition test

contains() CLOSES every band  -> a value exactly on an interior boundary
                                 belongs to TWO bands
                              -> invisible, because the partition test counts
                                 holders for the FIELD'S values and none of
                                 them lands on a seam
```

Closed by asserting the half-open contract **on the boundaries themselves**:
every band's lower and upper bound is held by exactly one band, a seam belongs
to the band *above* it, and the last band owns both of its own.

### C6, C21 and C23 were each a documented contract with no test

```text
C6   partitionsItsRange() ignoring its two END conditions. The existing
     broken-partition cases break CONTIGUITY and the closed-band rule, which
     are different conditions. Closed with legends whose first band starts one
     ulp above the minimum and whose last ends one ulp below the maximum --
     and the test asserts that this really does orphan the extreme value,
     which is why it matters.

C21  the header says a band is painted from its MIDPOINT "so the first band is
     not pure blue and the last not pure red". Dropping the 0.5 survived.
     Closed by asserting that neither end band equals a ramp end stop, and
     that the first band is as far from the ramp's start as the last is from
     its end -- which `i / n` cannot satisfy, because it puts the first band
     exactly on the start.

C23  the header says the legend "restates that range rather than re-scanning
     the values and possibly disagreeing with the extremes the navigation
     uses". Re-scanning survived, because for every field the production path
     builds the stored range and the values' range agree. Closed with a
     hand-built field whose STATED range is wider than its values span -- what
     a field over a sub-selection of a domain looks like -- and the test
     asserts the bands are 25 wide rather than 6.67.
```

### C16 — the one unreachable

`contourBandsOf` refuses a value that falls in no band. A value drawn from the
field lies in `[minimum, maximum]` by construction, the bands partition that
interval, and `partitionsItsRange()` is checked immediately before — so the
refusal cannot fire. The production comment already says so
("UNREACHABLE, AND CHECKED ANYWAY"), and it is kept because a miss would mean
the two checks above passed while the partition was still wrong. Clamping into
the nearest band instead would colour a defect as data.

## Combined total, both layers

```text
61 probes
54 KILLED
 7 SURVIVED  -- V11 V12 V13 V28 V33 V35 C16, every one unreachable with the
                reason derived and recorded above
 0 VOID
 0 compiler kills

10 real test gaps found and closed:
   V8 V17 V19 V20 V21 V34   the result-view and inspection layer
   C6 C18 C20 C21 C23       the contour and legend layer
```

Derived from the harness logs by difference rather than by counting the prose:
each probe's final verdict is the re-run's where it was re-run, and the totals
are taken from that table.

## Pre-freeze checks, cheap before expensive

All three of P15's voided qualifications came from running a cheap check after
an expensive one, so these ran first:

```text
git diff --check                                    clean
  (one warning, diagnosed: src/renderer/CMakeLists.txt was CRLF in the
   working tree while HEAD has it pure LF and .gitattributes says
   `* text=auto eol=lf`. An earlier session's edit converted it. Normalised
   back to LF, with a content check that fails if anything but the carriage
   returns changed: 61ca714e, identical.)

ctest -R StructuralViz_ --repeat until-fail:5       33/33 x 5
  (fixtures do not rerun between passes, so a test that mutated shared input
   would fail here; each of these builds its own)

debug-shared-ext, configure + build + run           links clean, 33/33
  (the ONLY preset that catches a DLL-boundary defect. The new header's
   export macros were checked by grep before this -- no macro on a
   constexpr or inline, and ContourLegend's two out-of-line members each
   carry it at member level, the pattern RenderedImage::at already uses --
   and the link confirms it.)
```

## The raw logs

```text
logs/mutation-run1.log                     35 probes, the first run
logs/mutation-run2-survivors.log           the 11 survivors re-probed after
                                           the five gaps were closed, plus
                                           V20 once its orphaned parameter
                                           was (void)-cast
logs/mutation-contour-run1.log             26 contour probes
logs/mutation-contour-run2-survivors.log   the 6 contour survivors re-probed
logs/probe.sh                              the harness for the first set
logs/probe-contour.sh                      the harness for the contour set
logs/guard.sh                              the busy-guard, and the comment
                                           in it records both ways it was
                                           wrong before it worked
```

Each probe's **final** verdict is the re-run's where it was re-run, and the
totals in this document were derived from those four logs by difference rather
than counted from this prose.
