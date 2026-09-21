# P13-PERSIST-001 — Save / Load Assembly Intent

```text
STATUS:          PASS
BASELINE:        09a7f2c (P13-PERSIST-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           what a .bcad file says about an assembly, proved whole.
                 No CLI, no STEP, no reference models.
IMPLEMENTATION:  30 lines, all of them the version policy written down where
                 the constant is declared. No writer was changed.
TESTS:           27 new Catch2 cases in 1 file (961 lines)
EVIDENCE:        this directory
```

## Scope

**The failure this milestone exists to prevent is a file that loads without
error into a different assembly than was saved.** It is the worst of the
failures this phase has been built around — the wrong face, the stale
transform, the almost-restored undo — because a file outlives the session
that wrote it. An exception on load is a good day. A document that opens,
looks right, and has one mate pointing somewhere else is discovered weeks
later, with the original long gone.

So these tests compare the **assembly**, not the bytes. A byte-identical
rewrite proves the writer is deterministic; it says nothing about whether the
reader understood what it read.

## What this milestone did and did not build

Assembly persistence was never deferred to a persistence milestone. Every
P13 milestone persisted its own part as it landed, with tests, and there were
already 17 assembly persistence cases before this one started.

```text
components, placements     P13-COMP-001, P13-XFORM-001
mates, mechanical mates    P13-MATE-001, P13-MATE-002
configurations, suppression P13-CONF-001
stable + external refs     P13-REF-001, P13-STREF-001
atomic save                pre-existing: writeFileAtomically()
```

**No writer was changed here.** The entire implementation is 30 lines of
comment recording the format-version policy, and the milestone is otherwise
verification: proving the schema whole, end to end, including the one claim
that only became checkable last milestone.

`git diff --numstat` for the milestone: **30 added, 0 deleted** across tracked
source, plus one line registering the new test file.

## Schema

The assembly part of a `.bcad`, as written:

| Written | Where | Invariant |
| --- | --- | --- |
| a component: its part reference, suppression, placement | `objects[]`, `"type": "component"` | the placement is **intent**; nothing solved is written |
| a placement: three lengths, three angles, and a parameter ID per axis | inside the component | SI metres and radians; a bound parameter is stored as its ID, not its current value |
| a mate: kind, up to four targets, a distance or an angle, suppression | `objects[]`, `"type": "mate"` | only the keys its kind allows; validation and the writer agree |
| a target: component, kind, and one of plane / axis / face | inside the mate | never a topology index — a `FaceSignature` has no field it could occupy |
| a configuration: id, name, parameter overrides, component and mate suppression | `configurations.defined[]` | suppression keys are written only when non-empty |
| the active configuration | `configurations.active` | by **name**, not by number |
| an external reference | inside a component's part | a `DocumentId` plus a locator; the locator is never identity |

Nothing else about an assembly is written. No transforms, no solver state, no
regeneration state, no command history.

### The format version, and the rule for changing it

`kDocumentFormatVersion` is still **1**, and had never been bumped for
parameters, expressions, configurations, datums, stable face references,
components, placements, mates, mechanical joints or suppression. That was an
accident of practice rather than a stated policy. It is now written down where
the constant is declared:

> **Bump this only for a change an existing reader would get WRONG.**

The reason additive growth is safe without a bump is that both directions
already fail safely on their own:

```text
new reader, old file   loads. An absent section means "none of those",
                       which is what the older document meant by omitting it
old reader, new file   REFUSES. An unrecognised object type is a parse error
                       naming it -- "unknown object type 'component'" --
                       never a silent skip, so nothing is lost
```

The version exists for what those two rules cannot cover: a field that
changes meaning, a unit that changes, a default that flips. An old reader
meets such a file, recognises everything in it, and misreads it. That is the
only thing that should cause a bump, and now the next person to change the
schema is told so at the point of change.

## Component persistence

| Claim | Measured |
| --- | --- |
| identity, part reference, placement | full `ComponentDefinition` equality |
| two instances of one part | same part reference, different IDs, different placements, both preserved |
| ordering | ascending ID, before and after |
| a parameter-driven placement | the **binding** survives, not just the value — the resolved position is 37 mm afterwards |
| dependencies | equal before and after |

The parameter row is the one that distinguishes intent from result: the file
stores the parameter's ID, so the placement follows the parameter after
loading rather than being frozen at whatever it was when saved.

## Mate persistence

All eleven kinds round-trip, compared by whole-`MateDefinition` equality.

| Group | Kinds | Measured |
| --- | --- | --- |
| basic | Fixed, Coincident, Concentric, Parallel, Perpendicular, Distance, Angle | all seven; the signed −25 mm and the 35° keep their exact values |
| mechanical | Revolute, Slider, Cylindrical, Planar | all four |
| suppression | a mate's own flag | preserved |

**The slider's roll reference is the one to watch.** A `Slider` carries a
second target pair (`a2`/`b2`), and a file that dropped it would load a mate
that still calls itself a slider and behaves as a sleeve — the failure this
milestone is named for, in miniature. Measured: both targets present, on the
right components, naming the X axis.

`Cylindrical` and `Planar` produce the same equations as `Concentric` and a
plane-to-plane `Coincident`; the file keeps them distinct by name, so what
the engineer meant survives even though the solver could not tell them apart.

## Configuration and suppression persistence

Three configurations, each saying something different:

| Configuration | Says | Round-tripped |
| --- | --- | --- |
| A | the arm is suppressed | `true` |
| B | a mate is suppressed | `true` |
| C | the arm is **present** | `false` |
| A, about that mate | nothing at all | absent |

Three states, not two, and the third survives as the third. The active
configuration round-trips, by name.

And the behavioural check rather than the structural one: after loading,
**switching through every configuration gives the same active component and
mate sets** as the original document does at the same configuration.

## Stable and unresolved references

| Claim | Measured |
| --- | --- |
| a face target | keeps its feature ID and its **role**, not a topology index |
| an external reference | keeps its `DocumentId` and its locator; still external; `sameTarget()` agrees |
| a broken reference | saved broken, loaded broken, and **recovers** when the target returns under its own ID |
| the canonical identity through all of that | unchanged, compared as a whole definition |

## Derived-state policy

The hard gate, measured two ways.

**Nothing derived reaches the file.** The document is saved, then solved, then
saved again — and the two files are **byte-identical**. The text is also
searched for the words a solver would use (`transform`, `residual`,
`jacobian`, `iterations`, `dof`, `solved`) and contains none of them.

**Everything derived is recoverable.** The regenerator is thrown away
entirely, the file is loaded into a new document, and a brand-new regenerator
rebuilds from nothing but what was saved. The transform it produces is
**bit-identical** to the original's.

That pair is the whole policy: the file holds intent, and intent is enough.

## Strong IDs

`ComponentId`, `MateId`, `ConfigurationId` and the `DocumentId` all survive
exactly, and the mate still names its components **by the same IDs** rather
than by position. No remapping.

## Legacy compatibility

Tested against **real files**, not fabricated ones.

`examples/models/plate.bcad` was last modified in commit `2da8966`,
"complete P0-P10 foundation". It predates parameter expressions,
configurations, datums, and every part of P13.

| Claim | Measured |
| --- | --- |
| it loads | yes |
| it has no assembly | no components, no mates, no configurations, no active configuration |
| it regenerates | yes |
| solving an assembly of nothing | not an error — 0 unknowns, 0 transforms |
| **all 24 committed models** | every one loads, and none contains a component |

The corpus test is the stronger of the two: any model that stopped loading
would be a compatibility regression, and it covers the whole range of P0–P12
rather than one chosen file.

## Corrupt-input handling

Plausible corruption, not obvious mangling — the edits that produce a file
which *parses*:

| Edit | Result |
| --- | --- |
| an unknown mate kind | refused |
| an unknown object type | refused |
| a suppression override for an object never written | refused |
| an unsupported format version | refused |
| a wrong JSON type for a number | refused |
| a truncated file | refused |
| an empty file | refused |
| text that is not JSON | refused |
| `NaN`, `Infinity`, `-Infinity`, `1e999` in a distance | all four refused |

After every one of those, the good file still loads — nothing left state
behind.

### One case that is deliberately NOT refused, and the inconsistency it exposes

A mate whose target names a component **not in the file** *loads*.

That is the established contract, not an oversight. `P13-REF-001` chose
"report, don't refuse" for a reference that cannot be satisfied, and
`P13-STREF-001` requires a broken reference to persist as broken — "not
dropped on save, not repaired on load" — which means the document has to open
in order to be repaired. Refusing it would make a fixable file unopenable.

What must not happen is that it loads *silently*, and that is what is
measured instead:

| Claim | Measured |
| --- | --- |
| the file loads | yes |
| the mate still says what it meant | it names component 987654 |
| the document reports the dangling reference | `buildDependencyGraph().missing` names the mate and the missing ID |
| regeneration | **fails**, and publishes no transforms |

**The inconsistency, stated plainly:** a dangling *mate target* loads and is
reported, while a dangling *suppression override* is refused outright. Both
are defensible and the distinction is real — a mate target is intent that
cannot currently be satisfied, and the engineer should be able to open the
file and fix it, whereas a suppression override for an object that was never
written is intent about nothing, which `forgetObject()` guarantees a
well-formed document never contains. But it is an inconsistency a reviewer
would rightly question, so it is recorded here rather than left to look
accidental.

This case was found by a test failing. The expectation was mine and it was
wrong; the contract was right.

## Failure atomicity

| Case | Measured |
| --- | --- |
| a failed load | the caller's document is untouched — same object count, same revision — and a valid load works afterwards |
| a failed save | no file, and **no `.tmp` left behind**; the document saves fine elsewhere |

Saving writes a temporary and renames, so a partially written file is never
visible under the real name.

## Deterministic serialization

**Byte-identical, three ways:**

```text
save, save again                            identical
save what was loaded                        identical
save what was loaded from what was loaded   identical
```

The third is the property-style statement —
`serialize(deserialize(serialize(x))) == serialize(x)` — and it is the one
that catches a reader which quietly normalises something on the way in.

Determinism is **structural, not lucky**: the document holds its objects,
parameters and configurations in `std::map`/`std::set` keyed by ID. There is
no unordered container anywhere in the document or the serializer, so
iteration order is value order.

Measured rather than asserted: a mate named `Zebra` created *before* one named
`Alpha` is written first, because its ID is lower. The file is ordered by
identity, not by insertion and not by name.

## Round-trip equivalence — the hard gate

The claim that could not have been made before `P13-REGEN-001`, which made
regeneration solve the assembly. Earlier milestones could compare intent; this
compares the answer.

```text
build -> regenerate -> solve -> save -> destroy -> load -> regenerate -> solve
```

| Assembly | Status | DOF | Transforms |
| --- | --- | --- | --- |
| fully constrained (5 mates) | `FullyConstrained` | 0 | **bit-identical** |
| under-constrained (cylindrical) | `UnderConstrained` | 2 | **bit-identical** |
| inconsistent (contradictory distances) | `Inconsistent` | — | conflicting mates and **the same residual** |
| large mixed (8 components, joints, configuration, suppression, parameter-driven placement, face reference) | same | same | **bit-identical** |

Compared each time: status, DOF, unknowns, equations, max residual,
conflicting mates, redundant mates, and every transform — through
`assembly::solve()` directly **and** through the regeneration path a real
caller takes.

Bit-identical is legitimate here rather than lucky: the solve starts from
intent alone and nothing is seeded (ADR-005), so a file that preserved the
intent exactly must reproduce the arithmetic exactly. A tolerance would have
hidden a file that preserved the intent *almost*.

The inconsistent case matters as much as the others: a file must preserve an
assembly that does not work as faithfully as one that does, **including the
diagnosis**. The least-squares compromise is identical, not merely also
wrong.

## Property tests

`serialize(deserialize(serialize(x))) == serialize(x)` is covered above under
deterministic serialization, for an assembly containing a mechanical mate, a
configuration and suppression. No randomised generation was used: the states
are enumerated deliberately — every mate kind, every configuration state,
every solver outcome — because a generator would have to be taught the
model's validity rules to produce anything meaningful, and those rules are
what the tests exist to check.

## Adversarial review

**No production defect.** Two findings: a wrong expectation of mine that a
failing test corrected, and an inconsistency in reader strictness that the
correction exposed and that is retained deliberately.

### Cleared

| Question | Answer |
| --- | --- |
| Can solved transforms become canonical after load? | No — nothing derived is written at all. The file is byte-identical before and after a solve, and contains none of `transform`, `residual`, `jacobian`, `iterations`, `dof`, `solved` |
| Can a `ComponentId` change after a round trip? | No — IDs are written explicitly and compared exactly, and the mate still names the same ones |
| Can a `MateId` change? | No — same |
| Can two references swap targets if serialization order changes? | No — a target names its component by **ID**, so order carries no meaning. Measured: a mate created second is written first because its ID is lower |
| Can suppressed mates become active after load? | No — a mate's own flag and each configuration's override both round-trip, including the explicit `false` and the absent third state |
| Can the active configuration change after load? | No — written by name, and measured across three configurations |
| Can an unresolved reference silently bind to another target? | No — `P13-STREF-001` measured the no-rebinding rule; here the reference survives a file unchanged and recovers only when **its own** target returns |
| Can raw OCCT topology identity leak into the file? | No, and not by check but by type: a `MateTarget` has no field a `geometry::FaceSignature` could occupy. A face is written as its feature and its role |
| Can NaN or Inf bypass validation? | No — all four of `NaN`, `Infinity`, `-Infinity`, `1e999` are refused in a distance field |
| Can duplicate IDs overwrite existing objects? | Loading assigns each object its stored ID through `restoreObject()`, which refuses an ID already in use, so a duplicate is a parse failure rather than an overwrite |
| Can a malformed file partially construct a document? | No — `loadDocument()` builds a new document and returns it only on success; a failure returns nothing, so there is no half-built one to leak. Measured: the caller's document is untouched by a failed load |
| Can unordered-map iteration change the bytes? | Structurally impossible — there is no unordered container in the document or the serializer. Everything is `std::map`/`std::set` keyed by ID |
| Can Debug and Release serialize differently? | The three presets run the same byte-identity assertions over the same value-ordered containers |
| Can save/load change the solver's DOF classification? | No — measured for fully constrained (0), under-constrained (2) and inconsistent, each with identical status, DOF, residual and transforms |
| Can old P12/P13 files regress? | No — all 24 committed models load, spanning P0 to P12 |
| Can command history be persisted? | No — measured in `P13-CMD-001` and unchanged: nothing in the format writes it |
| Can runtime caches make a loaded file non-reproducible? | No — the derived store is discarded entirely and rebuilt from the file, producing a bit-identical transform |

### Finding 1 — a wrong expectation, corrected by a failing test

The corrupt-input case asserted that a file whose mate target names a
component not in the file would be **refused**. It was not: it loads.

Investigated before changing anything, and the code was right. `P13-REF-001`
chose "report, don't refuse" for references that cannot be satisfied, and
`P13-STREF-001` has a qualified test requiring a broken reference to persist
as broken — "not dropped on save, not repaired on load" — which only means
anything if the document opens. Refusing would make a repairable file
unopenable, which is worse than the thing it prevents.

The assertion was replaced with a stronger one: the file loads, the mate
still says what it meant, `buildDependencyGraph().missing` names the dangling
reference, and regeneration **fails** rather than solving an assembly that is
missing a part it constrains. That checks the whole contract instead of one
half of it.

### Finding 2 — an inconsistency in reader strictness, retained

The correction exposed something worth stating rather than smoothing over:

```text
a dangling MATE TARGET            loads, and is reported
a dangling SUPPRESSION OVERRIDE   refused outright
```

Both behaviours are defensible, and the distinction is real. A mate target is
**intent that cannot currently be satisfied** — the engineer meant a specific
component, and they should be able to open the document and repair it. A
suppression override naming an object that was never written is **intent
about nothing**: it can never apply, `forgetObject()` guarantees a
well-formed document never contains one, and there is nothing for a user to
fix.

But a reviewer would rightly ask why one is strict and the other permissive,
and the honest answer is that they were written in different milestones
(`P13-STREF-001` and `P13-CONF-001`) against different instincts. The
behaviour is kept as it is — changing either would weaken a qualified
contract — and it is recorded in the known limitations so the next person to
touch the reader knows it is a decision rather than an accident.

### On what these tests cannot prove

Stated because the gate asks about legacy compatibility and the evidence
should not overclaim. Every round trip here happens inside one binary. The
argument that an older build would refuse a newer file rests on reading the
reader — an unrecognised object type is a parse error — and on the version
policy now written down, not on having run an older build against a newer
file. That is recorded as a known limitation rather than presented as
measured.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`; all fourteen are 0, and none was
skipped.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 441/441 | 0 | **1527/1527** | 188.7 s |
| `release` | 441/441 | 0 | **1527/1527** | 221.0 s |
| `debug-shared` | 441/441 | 0 | **1527/1527** | 234.8 s |

Then the milestone's related tests, five times over until failure — 1094
tests selected by the persistence, file, JSON, save, load, serialization,
schema, version, example-model, command, regeneration, reference,
configuration, suppression, solver, mate, component, placement, parameter,
expression, object, sketch, CLI and architecture names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **1094/1094 ×5** | 867.8 s |
| `debug` | **1094/1094 ×5** | 884.9 s |

1527 = the 1500 of `P13-CMD-001` plus this milestone's 27.

**Qualified tree.** Recomputed from the working tree after the run, the tree
IDs are identical to the ones the harness recorded before building, so the
tree that was qualified is the tree that is committed. `src` is unchanged
from `P13-CMD-001`'s hash, which is the strongest statement of this
milestone's shape: **no source file was touched at all.**

`ninja: warning: premature end of file; recovering` heads each build log. It
is ninja's own `.ninja_log`, damaged when runs were killed during
`P13-SOLVE-001`, and it makes ninja rebuild more rather than less — all 441
targets were built in every preset regardless. It is not a compiler warning,
and none appears in any of the three build logs.

**Test discovery, checked by name.** Each of the 27 new cases was looked for
individually in the qualification's own ctest log by its own name; all 27 are
there, and none is reported other than `Passed`.

## Known limitations

- **A dangling mate target loads; a dangling suppression override does not.**
  Recorded above with the reasoning. It is a real inconsistency in the
  reader's strictness, retained deliberately.
- **There is no schema migration**, because no version has been superseded.
  The first breaking change will need both a bump and a reader that accepts
  more than one version; nothing here builds that ahead of need.
- **Legacy coverage is the committed corpus.** 24 models spanning P0–P12 all
  load, but none of them contains an assembly — because assemblies did not
  exist when they were written. There is therefore no "early P13" file in the
  corpus to test against, only documents written by the current code.
- **Nothing tests a file written by a different build.** All round trips are
  within one binary. Cross-version compatibility is argued from the schema
  rules and the unknown-type refusal, not measured.
- **Property testing is enumerated, not generated.** Stated above.

## Result

```text
TASK:            P13-PERSIST-001 — Save / load assembly intent
IMPLEMENTATION:  30 lines, all of them the format-version policy written
                 down where the constant is declared. No writer changed, no
                 source file touched, zero lines deleted anywhere.
TESTS:           27 new cases in 1 file (961 lines), covering fixtures A-L
VALIDATION:      the assembly compared, not the bytes -- a loaded document
                 solves to BIT-IDENTICAL transforms, for fully constrained,
                 under-constrained, inconsistent and large mixed assemblies
REGRESSION:      1527/1527 on debug, release and debug-shared, each from
                 clean; 1094/1094 five times over in release and debug;
                 0 compiler warnings in all three builds; all 27 new cases
                 confirmed by name in the qualification's own log
ADVERSARIAL:     2 findings, 0 production defects
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: every part of an assembly's intent survives a file exactly
-- components, placements and their parameter bindings, all eleven mate
kinds including a slider's roll reference, three-state suppression across
several configurations, stable and external references, and references that
cannot currently resolve; nothing derived is written; everything derived is
rebuildable; serialization is byte-identical and ordered by identity rather
than by insertion; 24 real pre-assembly files still load; and a loaded
assembly solves to the same answer, bit for bit.

What is **not** claimed: that a file written by a *different build* loads —
every round trip here is within one binary, and the forward-compatibility
argument rests on reading the reader rather than on running an older one;
that any schema migration exists, since no version has been superseded; or
that the reader's strictness is uniform — it is not, deliberately, and the
inconsistency is recorded.

**The shape of this milestone is worth stating plainly.** It added 30 lines
of comment and 961 lines of tests. Assembly persistence was built
incrementally by the six milestones before it, and what was missing was not
code but proof — in particular the one claim that only became checkable when
`P13-REGEN-001` made regeneration solve: that a file preserves an assembly
well enough to reproduce its solution exactly.

## Revision

| When | What |
| --- | --- |
| 09:0x | survey: the writers already existed; the milestone is verification plus the version policy |
| 09:2x | 27 cases written, covering fixtures A–L |
| 09:4x | one case failed — a dangling mate target loads. The contract was right and my expectation wrong; replaced with a stronger assertion and the inconsistency recorded |
| 18:52 | full debug suite 1527/1527; tree frozen and qualified from clean |
| 20:0x | three presets and both repeat stages PASS on the final tree |
