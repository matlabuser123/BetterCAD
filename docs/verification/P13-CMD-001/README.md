# P13-CMD-001 — Commands / Undo / Redo

```text
STATUS:          PASS
BASELINE:        1abdbe6 (P13-CMD-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           assembly edits as commands in the existing history, and a
                 defect in deletion undo that predates this milestone. No
                 persistence milestone, no CLI, no STEP.
IMPLEMENTATION:  six assembly commands, and four lines of capture-and-restore
                 in DeleteObjectCommand. No second history.
TESTS:           17 new Catch2 cases in 1 file (580 lines)
EVIDENCE:        this directory
```

## Scope

**The failure this milestone exists to prevent is an undo that leaves the
document subtly different from before.** Not visibly broken — an exception
would be a kindness — but *almost* restored, with one field nothing thought to
check. The engineer carries on from a state they believe they recognise, and
the divergence surfaces much later as geometry that cannot be explained.

That is not hypothetical here. It is what this milestone found, on its first
afternoon, in code that had been qualified for two phases.

## The defect, found before anything was built on it

`TODO.md`'s authorization for this milestone recorded a suspicion from
reading, and said to verify it before fixing it. The verification was a test,
written first and run against the unmodified code:

```text
a configuration suppresses a component
-> delete the component      (the override is cleared)
-> undo                      (the component returns)
-> the configuration no longer suppresses it
```

**It failed, on both halves.**

| Case | Before the fix | Origin |
| --- | --- | --- |
| a configuration that **suppresses a component** | the suppression is lost on undo | `P13-CONF-001` — this project, four milestones ago |
| a configuration that **overrides a parameter** | the override is lost on undo | `P12-PARAM-002` — a *qualified phase*, and there since |

### Root cause

Deleting an object clears the configuration overrides that named it, which is
correct and deliberate — `Document::removeObject()` calls
`ConfigurationTable::forgetObject()` and `removeParameter()` calls
`forgetParameter()`, because a configuration must never name something that is
gone.

`DeleteObjectCommand` stored only the object, and its `undo()` re-inserted
only the object. Nothing captured what the deletion had cleared, so nothing
could put it back. The object returned; the intent about it did not.

### The fix

Four lines of behaviour, and it is purely additive — **the whole milestone
deletes not one line of existing code**:

```cpp
execute()  overrides_ = document.configurationOverridesFor(id_);   // BEFORE removing
undo()     ... re-insert the object ...
           document.restoreConfigurationOverrides(id_, overrides_); // AFTER
```

Captured before the removal, because the removal is what clears them.
Restored after the re-insertion, because an override may only name an object
the document holds.

Two details that matter:

- **One mechanism, not three.** The capture is keyed by `ObjectId`, and
  parameters, components and mates share that ID space, so a single path
  covers all three kinds.
- **A configuration that has itself been deleted is skipped, not an error.**
  Undoing a deletion after its configuration went restores what it can rather
  than failing.

Both cases are now regression tests and stay in the suite.

### Recorded, not quietly repaired

The parameter half is a defect in `P12-PARAM-002`, a qualified milestone. It
is written down here rather than folded silently into an assembly milestone's
diff, because a phase that was qualified with a hole in it should say so.

## Command contract

```text
canonical only      commands mutate intent: placements, definitions,
                    suppression. Never derived state
derived recomputed  transforms come from regeneration (P13-REGEN-001),
                    never from undo payload
failure             a failed command changes nothing and is not recorded
identity            redo restores the SAME id, so a reference to it survives
                    an undo/redo round trip
```

There is **one** command system and one history. These are commands in it,
not a second mechanism — the brief's "do not create a second assembly-only
history system", honoured by using `core`'s.

## Component commands

`core`'s `AddObjectCommand` and `DeleteObjectCommand` already create and
delete any `DocumentObject`, and a component is one. What they cannot do is
**validate**: `assembly::createComponent()` checks that the part exists and is
a part, and a raw add bypasses that entirely.

So `CreateComponentCommand` wraps rather than replaces — two ways to add a
component, one checked and one not, ends with the wrong one being used.

| Expected | Measured |
| --- | --- |
| a component naming a *sketch* as its part | refused; nothing added, nothing recorded to undo |
| create → undo | the component is gone |
| create → undo → redo | back, with the **same `ComponentId`** and the same definition and name |

Deletion is `DeleteObjectCommand`, now with the override fix above.

## Placement commands

The placement is intent and the solve is derived (ADR-005), so this command
edits where the engineer said a component sits — not where it ended up.

| Expected | Measured |
| --- | --- |
| A → B → undo | exactly A, compared as a whole `ComponentPlacement` |
| → redo | exactly B |
| five undo/redo cycles | no drift in either direction |
| what undo restores when a mate moved the component | the **50 mm intent**, while the solve had put it at 0 |

The last row is the separation stated as a test: the document says 90 mm, the
regenerator says the component solved to the origin, and undo restores the
90 mm — not the 0.

## Mate commands

| Expected | Measured |
| --- | --- |
| create, then edit `Distance` → `Concentric` with different targets | applied |
| undo | the **whole** prior definition: kind, both targets, and the 25 mm value |
| redo | the edited definition again |
| undo twice | the mate is gone entirely |
| a **slider** through undo and redo | its roll reference (`a2`/`b2`) survives — without which a sleeve would come back wearing a slider's name |

Comparisons are whole-`MateDefinition` equality, not field spot-checks.

## Configuration / suppression commands

`P13-CONF-001` recorded in its own limitations that suppression was set
through functions that bump a revision but are not undoable. That closes here.

**Suppression has three states, not two**, and undo must tell the third from
the second:

```text
suppressed    the configuration says "not in this build"
present       the configuration says "in this build"
absent        the configuration says nothing, and the base state stands
```

| Expected | Measured |
| --- | --- |
| absent → suppress → undo | back to **absent**, not to "present" |
| → redo | suppressed again |
| suppressed → set present → undo | back to **suppressed** |
| undo of a suppression | the component re-enters the **solve**, measured as the regenerator publishing 2 transforms again rather than 1 |

The last is the point of the feature rather than of the data structure: undo
puts the component back into the solve, not merely back into a map.

## Stable references through undo/redo

| Expected | Measured |
| --- | --- |
| a mate naming a component that is deleted | the mate is untouched and still names it — unresolved, never rebound (`P13-STREF-001`) |
| undo of that deletion | the mate still names the **same** component, by ID |
| redo of a creation | the same ID, so anything naming it still does |

## Regeneration and solve integration

Commands edit canonical intent; `P13-REGEN-001` decides what to recompute. The
two are tested together rather than separately:

| Command | Measured |
| --- | --- |
| placement edit | the assembly re-solves; the component lands where the new intent implies |
| suppression | transforms fall 2 → 1, and the suppressed component has none |
| undo of a suppression | transforms 1 → 2, and the component has one again |

Nothing stores a solved transform as undo payload. Undoing to a placement
restores the placement and lets the solve recompute from it.

## Undo and redo exactness

Comparisons are whole-object equality — `ComponentDefinition`,
`MateDefinition`, `ComponentPlacement`, and the configuration's own maps —
rather than a check of the field the command obviously touched. That is
deliberate: the defect above passed every spot-check anyone would have thought
to write.

## Multi-step history

A realistic sequence, rewound to baseline and replayed:

```text
create component -> move it -> create a mate -> suppress the component
```

| Position | Measured |
| --- | --- |
| at the top | 4 undoable |
| rewinding, one at a time | the suppression goes, then the mate, then the placement returns to default, then the component is gone |
| at the baseline | nothing undoable, 4 redoable |
| replayed to the top | the same object count, the same placement, the mate present, the suppression back |

## Failed commands

| Operation | Expected | Measured |
| --- | --- | --- |
| a mate relating a component to itself | refused | `undoCount` unchanged, document revision unchanged |
| a placement edit on a component that is not there | refused | `undoCount` unchanged |
| a component naming a non-part | refused | nothing added |
| suppression of a mate that does not exist | refused | `NotFound`, nothing recorded |
| suppression in a configuration that does not exist | refused | nothing recorded |
| the history afterwards | still works | undo returns the earlier placement |

## Redo invalidation

```text
A -> B -> C -> undo to B -> D
```

| Expected | Measured |
| --- | --- |
| after the undo | `canRedo`, 1 redoable |
| after D | **`canRedo` false, 0 redoable** — C is unreachable |
| the history now | A → B → D, walked back to prove it |

## Save/load history contract

The architecture keeps history transient: nothing in the file format writes
it. Measured rather than assumed — the saved file contains neither `undo` nor
`history`, a fresh `CommandHistory` has nothing to undo or redo, and the
canonical state the commands produced is what comes back.

## Determinism

| Expected | Measured |
| --- | --- |
| the same command sequence, twice, independently | same history depth, same object count, same component and mate ID lists, and equal definitions |
| repeated undo/redo | no drift, over five cycles |
| Debug / Release / Debug-shared | the same tests asserting the same values |

## Independent validation

Every expected value is the state the commands were asked to produce, stated
in the test as a literal — a placement of exactly 50 mm, a distance of exactly
25 mm, a suppression of exactly `nullopt` — never read back from the thing
under test.

## Adversarial review

**One production defect, found and fixed**, in two places — and it was found
by looking for it rather than by a test failing on its own. The rest of the
review is below.

### Finding 1 — the defect, above

Recorded in full under *The defect, found before anything was built on it*. It
is repeated here only to say how it was found, because that is the
transferable part.

It was not found by running anything. It was found while reading
`DeleteObjectCommand` alongside `P13-CONF-001`'s deletion rule, during the
authorization of this milestone, and written into `TODO.md` as a suspicion
with an instruction to *verify before fixing*. The verification was a failing
test.

The lesson worth keeping: the hole existed because two correct changes met.
`forgetObject()` is right. `DeleteObjectCommand::undo()` was right when it was
written, for a document that had no configurations. Neither review would have
caught it, because neither change was wrong. Only reading them together did.

### Cleared

| Question | Answer |
| --- | --- |
| Can undo restore derived state but the wrong canonical intent? | No — undo payload is canonical only. Measured on the sharpest case: a component the solve had moved to 0 undoes to its 90 mm intent, not to 0 |
| Can redo allocate a new identity unexpectedly? | No — redo re-inserts the kept object, so the ID is the one anything referring to it already names. Measured for components and mates |
| Can deleting then undoing a component break stable references? | No — the mate is untouched throughout and still names the same component by ID afterwards |
| Can mate undo restore the wrong target? | No — the comparison is whole-`MateDefinition` equality across a kind-and-target change, not a field check |
| Can undo/redo skip regeneration? | No — regeneration is driven by what changed, and a command changes it. Measured as transforms going 2 → 1 → 2 across a suppression and its undo |
| Can stale solver state survive an undo? | No — `P13-REGEN-001` publishes all-or-nothing and re-solves when its inputs move; an undo moves them |
| Can failed commands still enter history? | No — five distinct failures measured, each leaving `undoCount` and the document revision unchanged |
| Can redo survive a divergent edit? | No — A→B→C, undo to B, then D leaves `redoCount` at 0 and C unreachable |
| Can repeated undo/redo drift placements? | No — five cycles, compared as whole placements each time |
| Can command history retain dangling pointers? | The kept object is a `unique_ptr` the command owns, and a cloned one at that; nothing points into the document |
| Can a command outlive the document objects it names? | A command names objects by **ID**, not by pointer, so an ID that no longer exists is a `NotFound` rather than a dangling read |
| Can configuration switching through history corrupt suppression? | No — suppression undo restores the exact prior state including **absent**, which is a third state distinct from "present" |
| Can deleting and undoing restore dependencies incorrectly? | This was the defect. Now measured in both directions, for components and parameters |
| Can Debug and Release differ in history outcomes? | The three presets run the same tests; the history is a vector of commands over ID-keyed maps, with no address- or hash-ordered iteration |
| Can save/load persist undo stacks? | No — measured: the file contains neither `undo` nor `history`, and a fresh history has nothing in it |
| Can one history entry affect a second document? | `CommandHistory` binds to the first document it is used with and refuses any other — `core`'s existing guarantee, not this milestone's |
| Were any prior tests weakened? | No. The milestone **deletes not one line** of existing code or tests: `git diff --numstat` shows 92 added and 0 removed across every tracked file |

### Finding 2 — what this milestone did not do about the hole it found

The parameter half of the defect is in `P12-PARAM-002`, which is a qualified
phase. The fix is in `core`, so it is repaired for everything at once — but
that means an assembly milestone's diff now contains a `core` change that
belongs, historically, to a phase two steps back.

The alternative was to fix only the component half and leave the parameter
half broken in a qualified phase, which would have been worse and dishonest.
The choice is recorded here so the diff is not mistaken for scope creep: the
fix is four lines in one function, and the function is the one that was wrong.

`P12`'s own evidence is **not** retroactively edited. It records what was
measured at the time, and what was measured at the time did not include this.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 440/440 | 0 | **1500/1500** | 198.4 s |
| `release` | 440/440 | 0 | **1500/1500** | 201.5 s |
| `debug-shared` | 440/440 | 0 | **1500/1500** | 205.7 s |

Then the milestone's related tests, five times over until failure — 1076
tests selected by the command, undo, redo, history, regeneration,
configuration, suppression, reference, solver, mate, component, placement,
parameter, expression, object, persistence, sketch, CLI and architecture
names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **1076/1076 ×5** | 757.6 s |
| `debug` | **1076/1076 ×5** | 794.9 s |

1500 = the 1483 of `P13-REGEN-001` plus this milestone's 17. All fifteen
stage exit codes are 0.

**Qualified tree.** The harness records a git tree ID per source directory
before building. Recomputed from the working tree after the run, all eight are
identical, so the tree that was qualified is the tree that is committed:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           9c6d02fa632bedbd582eee7459aa54e66297886b
src               22eebd5895e5be02e450dce01ef6d49c0422b0f4
tests             808d5324055527b640bdabc60781353877386229
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`ninja: warning: premature end of file; recovering` heads each build log. It
is ninja's own `.ninja_log`, damaged when runs were killed during
`P13-SOLVE-001`, and it makes ninja rebuild more rather than less — all 440
targets were built in every preset regardless. It is not a compiler warning,
and none appears in any of the three build logs.

**Test discovery, checked by name.** Each of the 17 new cases was looked for
individually in the qualification's own ctest log by its own name; all 17 are
there, and none is reported other than `Passed`. That includes the two
regression guards for the defect, which is the pair that most needs to keep
running.

## Known limitations

- **Deleting a component leaves its mates unresolved rather than removing
  them.** That is `P13-STREF-001`'s contract and is measured here, but it does
  mean a delete can leave an assembly that will not solve until the mates are
  dealt with. Cascading deletion is not in this milestone.
- **There is no command for creating or deleting a configuration from the
  assembly side.** `core` already provides those, and they are used as they
  are; no assembly wrapper was added because there is nothing
  assembly-specific to validate.
- **`SetComponentPlacementCommand` replaces the whole placement.** There is no
  finer-grained "move along X" command, so two placement edits in a row are
  two history entries rather than one coalesced move.
- **History is not persisted**, by architecture. Reopening a document gives an
  empty history, so an edit made before a save cannot be undone after a
  reload.
- **Command coalescing does not exist.** A drag that produced fifty
  placement edits would produce fifty history entries.

## Result

```text
TASK:            P13-CMD-001 — Commands / undo / redo
IMPLEMENTATION:  six assembly commands (161 + 327 lines), and 92 added lines
                 across core for the override capture-and-restore.
                 Zero lines deleted anywhere in the milestone.
                 No second history, no second command system.
TESTS:           17 new cases in 1 file (580 lines), two of which are
                 regression guards for the defect found
VALIDATION:      whole-object equality across every undo and redo, not
                 field spot-checks -- the defect passed every spot-check
                 anyone would have thought to write
REGRESSION:      1500/1500 on debug, release and debug-shared, each from
                 clean; 1076/1076 five times over in release and debug;
                 0 compiler warnings in all three builds; all 17 new cases
                 confirmed by name in the qualification's own log
ADVERSARIAL:     2 findings, 1 production defect, fixed
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: every assembly edit named in the checklist is a command in
the existing history; undo and redo restore canonical intent exactly, with
identity preserved so references survive a round trip; failed commands change
nothing and are not recorded; a divergent edit clears the redo stack; and the
deletion defect that predates this milestone is fixed in both of the places
it existed.

What is **not** claimed: that deleting a component cleans up the mates that
name it (it leaves them unresolved, which is `P13-STREF-001`'s contract);
that history survives a save (it is transient by architecture, and measured
to be); or that consecutive edits coalesce into one history entry.

**The milestone's real result is the defect.** It was found by reading two
correct changes together — `forgetObject()`, which is right, and
`DeleteObjectCommand::undo()`, which was right when it was written for a
document that had no configurations. Neither review would have caught it,
because neither change was wrong.

## Revision

| When | What |
| --- | --- |
| 07:0x | the suspected defect reproduced by a failing test before any fix — both halves failed |
| 07:1x | fixed by capture-and-restore in `DeleteObjectCommand`; both cases pass and stay as regression tests |
| 07:3x | six assembly commands, wrapping `core`'s rather than replacing them |
| 07:4x | all 17 cases pass |
| 07:50 | full debug suite 1500/1500; tree frozen and qualified from clean |
| 08:57 | three presets and both repeat stages PASS on the final tree |
