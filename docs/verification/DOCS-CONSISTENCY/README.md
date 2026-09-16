# DOCS-CONSISTENCY — Project Documentation Consistency Pass

## What this is, and what it is not

This records a **documentation consistency pass**. It proves that the five
project documents agree with each other and with the repository's own
verification evidence.

**It is not a CAD qualification.** Nothing here was built, tested, measured or
regenerated. No test was run, no geometry was validated, no build was
performed, and no milestone changed state. The CAD system's qualification is
[P11-QUAL-001](../P11-QUAL-001/README.md) and this document neither extends nor
restates it.

No checkbox in `TODO.md` was ticked, added or removed by this pass, and no
production code was touched.

## Revision Inspected

```text
449d5fdbccdb2a840f8af22e580b583a9e956b3b
BetterCAD: qualify P11 production part modeling
branch    main, up to date with origin/main
tree      clean at the start of the pass
```

Date: 2026-09-16.

## Files Reviewed

Read in full:

```text
README.md
ROADMAP.md
TODO.md
CLAUDE.md
ARCHITECTURE.md
docs/architecture.md
docs/verification/P11-QUAL-001/README.md
```

Inspected for corroboration: `docs/verification/` directory listing,
`examples/models/reference/`, `src/`, `include/bettercad/`, `apps/`, `tests/`,
`.gitattributes`, the git tag `v0.1.0` and the commit log.

Changed by this pass:

```text
README.md
ROADMAP.md
TODO.md
CLAUDE.md
ARCHITECTURE.md
docs/verification/DOCS-CONSISTENCY/README.md   (new)
```

`docs/architecture.md` was read but deliberately not modified: it is the
as-built architecture record and was already accurate.

## Authority Hierarchy

Established explicitly, and now stated in `README.md` and repeated in the
header of each document it governs:

| Question | Authority |
| --- | --- |
| What is BetterCAD? | `README.md` |
| What are we building long-term? | `ROADMAP.md` |
| What is actually complete? | `TODO.md` + `docs/verification/` |
| What should be implemented next? | `TODO.md` |
| How must the system be structured? | `ARCHITECTURE.md` |
| How must the work be executed? | `CLAUDE.md` |
| What proves completion? | `docs/verification/` |

Two rules make the hierarchy operative rather than decorative, and both are
recorded in `CLAUDE.md` §0.1:

```text
Authorization comes only from TODO.md.
    A capability listed in ROADMAP.md is not authorized by being listed,
    by being next in the dependency order, or by being easy.

Documentation follows evidence.
    Never change ROADMAP/TODO/ARCHITECTURE to make an implementation
    appear complete. When a document and the evidence disagree,
    the evidence is right.
```

## Roadmap / TODO Numbering

**The problem.** `ROADMAP.md` numbered its chapters `P0`–`P26` while `TODO.md`
numbers its milestones `P0`, `P11`, `P11-FEAT-003` and so on. The two sequences
diverged as soon as implementation did, and then actively contradicted each
other: roadmap chapter "P10" was Production Part Modeling while milestone `P11`
was Production Part Modeling, and roadmap chapter "P11" was Assemblies. A bare
`P11` meant two different capabilities depending on which file the reader was
in.

**The resolution.**

```text
Completed TODO milestone IDs        unchanged. Not renumbered.
                                    P0-P11, P11-FEAT-001..009, P11-REF-001,
                                    P11-QUAL-001 all keep their IDs and
                                    their evidence links.

Roadmap chapter numbers             retired. ROADMAP.md capabilities are
                                    now named, never numbered.

A bare P<number>                    now means a TODO.md milestone,
                                    everywhere in the repository, with no
                                    second meaning.

ID allocation                       stated in both files: IDs are allocated
                                    by TODO.md, in sequence, at the moment a
                                    capability is authorized. Never in
                                    advance, never by ROADMAP.md.
```

`ROADMAP.md` keeps a capability → milestone → status mapping table, and records
in prose that its chapters used to be numbered `P0`–`P26`, so a reader holding
an older copy can still resolve a reference.

**No milestone number was assigned to Assemblies or to any other unauthorized
capability.** The previous `TODO.md` heading `### P12 — Next Phase`
pre-allocated the next ID to an unauthorized phase; it was removed. The next ID
will be allocated when a scope decision is made.

## Stale Statements Corrected

### ROADMAP.md

1. The milestone mapping table said `P11 in progress` and
   `P10 Production Part Modeling | P11 | in progress`. `P11` has been complete
   and qualified since `449d5fd`. Now **QUALIFIED**.
2. *Immediate Development Priority* gave the current work order as `P0` →
   `P9 Minimal Desktop CAD Workflow`. Every item in that list is delivered.
3. The same section said *do not start assemblies, FEA, CFD, AI, CAM or cloud
   collaboration until the parametric modeling foundation is reliable*. That
   condition is now satisfied as written, so the sentence had stopped
   restricting anything. Replaced with a restriction that does not expire:
   listing a capability does not authorize it; only `TODO.md` does.
4. *The first major objective is simple: create a small CAD system that can
   build, edit, regenerate, save, reload and export a real parametric
   mechanical part without breaking* — met, and now recorded as met rather
   than as a pending goal.
5. Release milestones carried no status, so v0.1 (released) was
   indistinguishable from v0.9 (not started). Each now carries one, and the v0.2
   and v0.3 rows say exactly which of their scope `P11` delivered and which
   parts remain outstanding.
6. The reference-model list was entirely aspirational
   (`01_simple_block` … `10_machine_frame`) and did not mention the six models
   that exist. Now split into what is in the repository and what is planned.

### README.md

7. The front door linked only `TODO.md` and `docs/architecture.md`.
   `ROADMAP.md`, `ARCHITECTURE.md` and `CLAUDE.md` were unreachable from it.
8. Requirements said *From milestone P3 on: Open CASCADE Technology 8.0* — a
   milestone-relative condition that stopped being informative once `P3`
   shipped. Now an unconditional requirement.
9. The placeholder status of the desktop application appeared once, in passing,
   in a code comment. It is now stated explicitly in *Current Limitations*.
10. There was no statement anywhere that STEP **import** does not exist, and no
    *Current Limitations* section at all. Both added, with export and import
    distinguished everywhere they appear.

### TODO.md

11. `### P12 — Next Phase` pre-allocated a milestone ID to an unauthorized
    phase. Removed; see *Roadmap / TODO Numbering* above.
12. `Release v0.1.0 (commit 2da8966, tag v0.1.0)` appeared to contradict
    `P11-QUAL-001`, which records `v0.1.0` at `93d84f0`. Both are correct:
    `93d84f0` is the annotated **tag object**, `2da8966` the **commit** it
    points at, confirmed with `git rev-parse v0.1.0` and
    `git rev-parse v0.1.0^{commit}`. Stated explicitly so the two documents no
    longer read as disagreeing.
13. `P11-REF-001` listed five reference-model checkboxes while its own
    acceptance text and all of `P11-QUAL-001` describe six models. The U-bolt
    had evidence at `docs/verification/P11-REF-001/u_bolt/` but nothing linked
    to it. A note now names it and links its evidence — deliberately **not** as
    a sixth checkbox; see *Checkbox Integrity* below.
14. Limitations existed only as per-milestone notes scattered through 500 lines.
    Consolidated into a `## Known Limitations` section, without deleting the
    per-milestone notes.

### CLAUDE.md

15. §2 *Project Priority* gave the current work order as `P0` → `P9`, all
    delivered, and made authorization conditional on *the roadmap reaching
    those phases*, which contradicts the authority hierarchy.
16. §25 *Reference Models* listed ten aspirational model names, six different
    ones exist. Replaced with the rules that apply to reference models, and a
    pointer to the documents that own the list.
17. §29 *Evidence* prescribed a `results/` tree with `validation/`,
    `benchmarks/`, `regression/` and `release/` subdirectories. No such
    directory exists; evidence lives in `docs/verification/`, one directory per
    milestone. Corrected to describe what is actually done.
18. §39 *Current Definition of Success* ended with *until this works reliably,
    prioritize foundation work over advanced features*. It works and is
    qualified. Recorded as met, with the success criteria that now apply per
    milestone.
19. §22 gave a third module layout, differing from both `ARCHITECTURE.md` and
    the real tree. Replaced with the rules a developer actually needs and a
    pointer to the owning documents.

### ARCHITECTURE.md

20. §4 presented a target repository layout as though it described the
    repository. `benchmarks/`, `results/`, `docs/adr/`, `src/assembly/`,
    `src/drawing/`, `src/simulation/` and `src/versioning/` do not exist, and
    `src/renderer/` and `src/scripting/` are empty. Now labelled as the target,
    with the absent directories named.
21. §85 directed ADRs to `docs/adr/` without noting that it does not exist.
22. §87 *Immediate Priority* was an implementation schedule inside the
    architecture document — exactly the scheduling content this document must
    not own.
23. The repository held two architecture documents, `ARCHITECTURE.md` and
    `docs/architecture.md`, with nothing explaining how they relate. They now
    cross-link, with their roles stated: target versus as-built.

## Additions

* `ARCHITECTURE.md` §1.1 — fourteen **architectural invariants** in one place,
  each labelled as exercised today or reserved for a subsystem that does not
  exist yet.
* `ARCHITECTURE.md` §2 — a layered diagram from applications down to OCCT, with
  every unbuilt subsystem marked `[future]`.
* `ARCHITECTURE.md` §87 — **Architecture vs Roadmap**, stating which document
  defines structure, direction, authorization and execution.
* `CLAUDE.md` §0 — the read order before working, the authority table, and the
  two rules quoted above.
* `CLAUDE.md` §41 — **stop conditions**, including "TODO.md records no open
  milestone → stop", plus commit rules.
* `README.md` — *Verified Capabilities*, *Architecture at a Glance*, *Test*,
  *Verification Philosophy*, *Project Documents* and *Current Limitations*.
* `TODO.md` — *Status*, *Current*, *Next*, *Planned / Not Authorized*,
  *Recently Completed* and *Known Limitations*.
* A *Project Documents* navigation block in all five documents.

## Current Verified Project Status

As recorded by the evidence, unchanged by this pass:

```text
P11 Production Part Modeling    QUALIFIED at revision 79dab04
                                20 gates, 20 passed, 0 failed, 0 blocked
                                738/738 tests, 0 warnings, 274 TUs,
                                in each of Debug, Release and Debug-shared
                                301/301 legacy tests unchanged
                                six reference models bit-identical across
                                three configurations and six processes

Released                        v0.1.0 (P0-P10). P11 is qualified but not
                                released; there is no v0.2.0.

Documentation                   CONSISTENT (this pass)

Current implementation          None
Next                            Awaiting explicit scope decision
```

Source: [P11-QUAL-001](../P11-QUAL-001/README.md). This pass verified that the
five documents now state this and nothing stronger.

## Checkbox Integrity

Verified before and after, since documentation work must not change completion
state:

```text
                                    before    after
TODO.md checked items, total          215       215
TODO.md checked items, P11 section    179       179
TODO.md unchecked items, P11 section     0         0
TODO.md unchecked items, elsewhere      14        20
```

The P11 count of 179 is 171 feature and reference-model items plus the 8 items
of `P11-QUAL-001` itself, which agrees exactly with `P11-QUAL-001`'s own
evidence audit ("`TODO.md` records 171 checked P11 items with none unchecked",
written before its own checkboxes were added). **No checkbox was ticked, added
or removed.**

The U-bolt was therefore *not* given a sixth checkbox under `P11-REF-001`, even
though it has evidence and would arguably deserve one: adding it would move the
count to 172 and make `P11-QUAL-001`'s frozen evidence read as wrong. A
non-checkbox note with its evidence link records it instead. This is a
deliberate trade — historical evidence is not invalidated to tidy a list — and
it is recorded here so it is not mistaken for an oversight.

## Claims Deliberately Not Made

Checked line by line, because documentation cleanup cannot create engineering
completion:

```text
Assemblies                  NOT complete, NOT authorized, no ID assigned
Semantic topology           NOT complete, NOT authorized
Drawings                    NOT complete
STEP import                 NOT claimed anywhere; export/import distinguished
                            in all five documents
DXF, IGES, OBJ              NOT claimed
FEA, CFD, thermal           NOT claimed
Optimization                NOT claimed
Python API                  NOT claimed
AI                          NOT claimed
CAM                         NOT claimed
Production GUI              NOT claimed; the desktop application is stated
                            to be a placeholder shell in README and TODO
CI, coverage, sanitizers    NOT claimed; their absence is stated
Cross-platform results      NOT claimed; one platform is stated
```

A grep for `in progress` across the five documents returns three hits, all
negations ("Nothing is in progress"). A grep for `P12`–`P29` returns one hit,
the historical note explaining why roadmap chapter numbers were retired.

## Cross-Links Checked

Every relative Markdown link and heading anchor in the six documents was
resolved against the filesystem with a script that skips fenced code blocks:

```text
relative links checked   164
broken paths               0
broken anchors             0
```

Covering, among others:

```text
README        -> ROADMAP, TODO, ARCHITECTURE, CLAUDE, docs/architecture.md,
                 docs/verification/, LICENSE, examples/, P11-QUAL-001,
                 P11-REF-001, TODO.md#known-limitations
ROADMAP       -> README, TODO, ARCHITECTURE, CLAUDE, docs/verification/,
                 P11-QUAL-001
TODO          -> README, ROADMAP, ARCHITECTURE, CLAUDE, and one evidence
                 directory per milestone (P0-001 ... P11-QUAL-001, including
                 each reference model's values-release.txt)
CLAUDE        -> README, ROADMAP, TODO, ARCHITECTURE, docs/architecture.md,
                 docs/verification/
ARCHITECTURE  -> README, ROADMAP, TODO, CLAUDE, docs/architecture.md,
                 docs/verification/
```

All five documents are reachable from every other one.

## Terminology

Audited for competing names for the same subsystem. Each concept now has one
name across all five documents:

```text
BetterCAD    Document    Parameter    Sketch    Constraint    Feature
Body         Component   Assembly     Simulation
Dependency Graph         Regeneration
Geometry Service         Geometry Backend
Open CASCADE (full name) / OCCT (abbreviation)
Engineering Intent       Stable ID    Verification Evidence
B-Rep
```

No document uses `OpenCascade`, `BRep`, or a second name for a subsystem that
already has one. The spelling `modelling` was normalised to `modeling` across
the five documents, matching the milestone name *Production Part Modeling* used
in `TODO.md` and the evidence titles. Evidence files written at earlier
revisions were not edited; they are frozen records and may use the other
spelling in prose.

## Validation Performed

```text
git status                  clean before the pass
git diff --check            no whitespace errors
git diff --stat             5 documents changed
link and anchor audit       164 relative links, 0 broken
checkbox audit              215 checked before and after; P11 unchanged at 179
stale-language grep         "in progress", "P12".."P29", STEP import/export
terminology grep            competing names for one subsystem: none
markdown lint               editor markdownlint; MD025 (multiple H1s) is the
                            long-standing convention in these files and is
                            unchanged
```

No test, build or CAD validation was run. **Pure Markdown changes do not
require pretending that 738 CAD tests validate documentation**, and none of
these changes touches a script, a build file or any build metadata, so nothing
in the test suite is affected by them.

## Known Remaining Documentation Limitations

Recorded so this pass is not read as more than it is.

1. **There are still two architecture documents.** `ARCHITECTURE.md` (target
   structure and invariants) and `docs/architecture.md` (as built). They now
   cross-link and state their roles, but a reader must still know which to
   consult, and the two can drift. Merging or formally splitting them was out
   of scope.
2. **No automated documentation check exists.** The link audit above was an
   ad-hoc script run once, in the scratch directory, not committed and not
   wired into the build or the test suite. Links can rot between passes, and
   nothing will notice. Adding a documentation check would be a milestone, not
   a cleanup.
3. **The U-bolt has no checkbox of its own**, for the reason given under
   *Checkbox Integrity*.
4. **List-marker style differs between files** — `-` in `README.md` and
   `TODO.md`, `*` in `ROADMAP.md`, `CLAUDE.md` and `ARCHITECTURE.md`. Each file
   is internally consistent; they were not unified, because changing markers
   across three large files would bury the substantive diff.
5. **`docs/architecture.md` was not reviewed for completeness**, only for
   conflicts with the five documents. It was accurate where it overlapped.
6. **Earlier evidence documents were not rewritten.** They are frozen records
   of the revision they measured. Where one of them describes `TODO.md`'s
   contents (the 171-item audit in `P11-QUAL-001`), this pass preserved the
   property it asserts rather than editing the record.
7. **`ROADMAP.md`'s "Outstanding" lists are a judgement**, assembled from the
   original roadmap scope minus what `TODO.md` records as delivered. They are
   intended to be conservative — an item is listed as outstanding unless the
   evidence clearly covers it — but they are not themselves evidence-backed the
   way the delivered column is.

## Result

```text
PASS
```

The five project documents are mutually consistent, connected, non-duplicative
and evidence-backed. Each answers one question and defers to the others for the
rest. No implementation claim was added that the evidence does not support, no
milestone changed state, and no checkbox moved.

Documentation: **CONSISTENT**. Current implementation: **none**. Next:
**awaiting explicit scope decision**.
