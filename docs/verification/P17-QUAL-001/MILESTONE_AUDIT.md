# P17-QUAL-001 — milestone audit

```text
SUBJECT:  whether every P17 predecessor is GENUINELY qualified, read from its
          recorded result and its harness artefact rather than from a checkbox
METHOD:   for each milestone -- the TODO boxes counted from the file, the
          evidence directory, the recorded RESULT, the harness's own
          "N stage(s) failed" line, the candidate it names, the regression
          figure it recorded, and the commit the chain carries
```

## The matrix

```text
| MILESTONE        | TODO  | EVID | RESULT | HARNESS LINE                              | CANDIDATE | SUITE | COMMIT  |
| P17-ARCH-001     | 20/20 | yes  | PASS   | Wed 07/10/2026  1:54:27  0 stage(s) failed | 4a41c4f   | 3401  | a4290cf |
| P17-DATA-001     | 19/19 | yes  | PASS   | Wed 07/10/2026  6:11:17  0 stage(s) failed | a4290cf   | 3434  | 57b4ddf |
| P17-MAT-001      | 14/14 | yes  | PASS   | Wed 07/10/2026 15:55:45  0 stage(s) failed | 57b4ddf   | 3453  | 9e65b51 |
| P17-DOF-001      | 12/12 | yes  | PASS   | Wed 07/10/2026 20:28:06  0 stage(s) failed | 9e65b51   | 3493  | 28b0d5a |
| P17-ELEM-001     | 18/18 | yes  | PASS   | Thu 08/10/2026  1:03:58  0 stage(s) failed | 28b0d5a   | 3522  | dd2aca6 |
| P17-LOAD-001     | 18/18 | yes  | PASS   | Thu 08/10/2026  6:15:42  0 stage(s) failed | dd2aca6   | 3545  | caf0f1e |
| P17-BC-001       | 16/16 | yes  | PASS   | Thu 08/10/2026 12:36:02  0 stage(s) failed | caf0f1e   | 3583  | 3b471a9 |
| P17-ASSEMBLY-001 | 18/18 | yes  | PASS   | Thu 08/10/2026 22:33:43  0 stage(s) failed | 3b471a9   | 3615  | bd098c0 |
| P17-SOLVE-001    | 17/17 | yes  | PASS   | Fri 09/10/2026  4:04:56  0 stage(s) failed | bd098c0   | 3647  | 5e06595 |
| P17-POST-001     | 17/17 | yes  | PASS   | Sat 10/10/2026  3:40:15  0 stage(s) failed | 5e06595   | 3694  | 5682b98 |
| P17-REACTION-001 | 13/13 | yes  | PASS   | Sat 10/10/2026 18:00:57  0 stage(s) failed | 5682b98   | 3721  | c5609fe |
| P17-VALID-001    | 17/17 | yes  | PASS   | Sun 11/10/2026  5:12:31  0 stage(s) failed | c5609fe   | 3759  | b2bd8e3 |
| P17-VIZ-001      | 23/23 | yes  | PASS   | four runs, every one 0 stage(s) failed     | see below | 3835  | e795acb |
| P17-CMD-001      | 19/19 | yes  | PASS   | Tue 13/10/2026 20:06:17  0 stage(s) failed | e795acb   | 3883  | bde6f8b |
| P17-PERSIST-001  | 20/20 | yes  | PASS   | Wed 14/10/2026  6:58:57  0 stage(s) failed | bde6f8b   | 3921  | 7e18175 |
| P17-CLI-001      | 24/24 | yes  | PASS   | Thu 15/10/2026  8:02:22  0 stage(s) failed | 7e18175   | 3949  | 460321a |
| P17-REFMOD-001   | 22/22 | yes  | PASS   | Thu 15/10/2026 16:28:21  0 stage(s) failed | 460321a   | 4006  | a99f0b6 |
```

**17 of 17 PASS.** Every one has a complete checkbox set (307 boxes ticked
across the seventeen, counted from `TODO.md`), an evidence directory with a
recorded `RESULT: PASS`, and its own `qualification/qualification-times.txt`
ending in `0 stage(s) failed` — the harness's own verdict, where the exit
code *is* the number of failed stages.

The CANDIDATE column is what each harness file names on its own line 3: the
**parent** commit plus the working tree that was then committed as the COMMIT
column. The chain is unbroken: every milestone's candidate is the previous
milestone's commit, read from `git log 137bad1..a99f0b6` rather than from the
evidence that cites it.

### P17-VIZ-001, qualified four times

The one milestone whose row does not fit a single line, because it was
qualified four times and each run covered what the one before it did not:

```text
| RUN                      | CANDIDATE | SUITE | HARNESS LINE                              | COMMIT  |
| the inspection layer     | b2bd8e3   | 3792  | Mon 12/10/2026  6:57:49  0 stage(s) failed | b04a9e1 |
| the display surface      | b04a9e1   | 3806  | Mon 12/10/2026 19:48:49  0 stage(s) failed | ec68076 |
| the load/restraint glyphs| ec68076   | 3825  | Tue 13/10/2026  4:09:36  0 stage(s) failed | 974b62b |
| the element contours     | 974b62b   | 3835  | Tue 13/10/2026  9:05:52  0 stage(s) failed | e795acb |
```

Two further `qualification-times.txt` files exist under
`qualification-display/aborted-attempt/` and `aborted-attempt-2/`; both name
the same candidate `b04a9e1` and neither carries a verdict line, because each
was killed before it finished — `FREEZE_DISPLAY.md` records why there were
three attempts. They are kept rather than deleted, which is the right practice,
and they are not the run that counts.

## The suite grew monotonically

```text
P17-ARCH-001     3401     P17-SOLVE-001    3647     P17-VIZ-001 (4)   3835
P17-DATA-001     3434     P17-POST-001     3694     P17-CMD-001       3883
P17-MAT-001      3453     P17-REACTION-001 3721     P17-PERSIST-001   3921
P17-DOF-001      3493     P17-VALID-001    3759     P17-CLI-001       3949
P17-ELEM-001     3522     P17-VIZ-001 (1)  3792     P17-REFMOD-001    4006
P17-LOAD-001     3545     P17-VIZ-001 (2)  3806
P17-BC-001       3583     P17-VIZ-001 (3)  3825
P17-ASSEMBLY-001 3615
```

3387 at the P16 gate to 4006 at P17-REFMOD-001, with no decrease anywhere —
a weak check that no milestone quietly removed tests, and labelled as weak.
This qualification adds three and freezes at 4009.

## One apparent gap, which was the evidence and not my extraction

An audit that only reports agreement has not audited anything, so this is
recorded with what actually happened.

**P17-VIZ-001's README.md still says `STATUS: PARTIAL, 18 of 23 boxes` and
carries a section headed "Why this is PARTIAL and not PASS"; its
REMAINING_SCOPE.md says the element-contour boxes "stay unticked only until
the three-preset qualification covers them" and that "the milestone stays
open".** Both were true when written, after the second of the four runs. They
stopped being true at the fourth: `FREEZE_ELEMENTS.md` records the run that
"takes P17-VIZ-001 to 23 of 23", `TODO.md` ticks all 23 with the element
contours' evidence linked, and the commit chain carries `e795acb`.

So the milestone IS qualified and its README's status line is STALE — a
documentation defect in a qualified milestone, not a qualification gap.
Resolved the way P16-QUAL-001 resolved ADR-031: the two documents carry a
dated addendum at the top saying what changed and where the final figures
are, and the original text is left in place rather than rewritten. Recorded in
`ADVERSARIAL_REVIEW.md` as finding D1.

Nothing else disagreed. In particular the regression figures were taken from
each milestone's **own** `FREEZE.md` or `README.md` summary table and not from
the prerequisite audit that quotes its predecessor — the trap P16-QUAL-001
fell into with P16-MAP-001 — and each one reconciles with its successor's
"what this milestone added" count.

## Known limitations carried by the milestones, re-read

Each milestone's recorded limitations were re-read to separate a documented
boundary from an unfinished requirement.

```text
MILESTONE        LIMITATION                                   KIND
P17-ARCH-001     linear static, Tet4, one solid; no contact,  the scope ADR-034
                 shells, beams, dynamics, nonlinearity        decided, by design
P17-ARCH-001     configuration-sensitive geometry REFUSES     carried constraint 2;
                 under an active override                     refusal is the contract
P17-MAT-001      density is required only by the gravity      intent, not a gap:
                 mode; a mode is the user's choice            P15's consumer model
P17-LOAD-001     a NodalForceLoad is mesh-local and cannot    ADR-048 decision;
P17-PERSIST-001  be saved -- the save is REFUSED              refusal tested
P17-LOAD-001     a drilled hole's cylindrical wall carries    P16's carried naming
P17-BC-001       no FaceName, so it cannot be loaded or       limit; refused with
                 restrained                                   no geometric fallback
P17-SOLVE-001    Eigen's licence is an owner decision          carried constraint 8
P17-REACTION-001 the 1e-12 moment threshold was measured on   P17-REFMOD-001's F1:
                 well-conditioned blocks with a recorded      measured, crossed by a
                 caveat                                        correct bending solve
P17-VALID-001    the warning envelope is the measured         ADR-042: an envelope,
                 qualified range and rejects nothing          not a rejection
P17-VIZ-001      the Qt inspector surface is not wired;       REMAINING_SCOPE.md;
                 LoadGlyph (pressure/traction/gravity) is     apps/bettercad has
                 data, not drawn; no log scale                0 structural refs
P17-CMD-001      no GUI command wiring (no GUI undo for       EXPLICITLY OUT, as
                 structural edits)                            P16-CMD-001 recorded
P17-CLI-001      one analysis per document is selected       the CLI refuses an
                 implicitly; two need a selector it lacks     ambiguity, tested
P17-REFMOD-001   F2 linear Tet4 locks in bending (4-5% of    measured; ADR-034
                 Timoshenko on the committed meshes)          names Tet4's limit
P17-REFMOD-001   F3 OCCT never subdivides a developable wall  P16's planar-face
                 along its ruling; wall tets are needles      limit, curved form
toolchain        no ASan/UBSan on MinGW; the dependency       carried constraints
                 download path is unverifiable here           6 and 7
```

**None of these is an unfinished P17 checklist item.** The three that an
owner must decide on — F1, F2, F3 — are carried into `README.md`'s known
limitations with their measurements, and F1 is examined in
`ADVERSARIAL_REVIEW.md` because it bears on a qualified threshold.

## TODO audit

```text
open boxes in the whole of TODO.md            40, as audited, before PASS --
                                              all of them under # P17-QUAL-001
  afterwards                                  0
P17 sections with any open box                none but this milestone
ticked boxes across the 17 P17 milestones     307, counted from the file
BLOCKED / FIXME / "known issue" mentions      P17-VIZ-001's element contours
                                              were BLOCKED for two milestones
                                              and are recorded as UNBLOCKED
                                              and done (ELEMENT_CONTOURS.md);
                                              nothing is live
```

`Current` / `Next` were consistent at the start: Current milestone
P17-REFMOD-001 COMPLETE, Next P17-QUAL-001 NOT AUTHORIZED until the owner's
"start P17-QUAL-001" of 2026-10-15, which is the only thing that could
authorize it.
