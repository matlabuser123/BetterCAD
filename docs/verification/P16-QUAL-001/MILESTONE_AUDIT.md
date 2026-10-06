# P16-QUAL-001 — milestone audit

```text
SUBJECT:  whether every P16 predecessor is GENUINELY qualified, read from its
          recorded result and its harness artefact rather than from a checkbox
METHOD:   for each milestone -- TODO boxes, evidence directory, recorded
          RESULT, the harness's own "N stage(s) failed" line, and the
          implementation in the committed tree
```

## The matrix

```text
| MILESTONE        | TODO  | EVID | RESULT | HARNESS LINE         | FINAL  |
| P16-ARCH-001     | 25/25 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-DATA-001     | 26/26 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-GEOM-001     | 20/20 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-SURF-001     | 21/21 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| INFRA-NETGEN-001 | n/a   | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-VOL-001      | 19/19 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-SIZE-001     | 21/21 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-QUALITY-001  | 19/19 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-MAP-001      | 20/20 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| INFRA-VIEWER-001 | 17/17 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-VIZ-001      | 18/18 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-CMD-001      | 18/18 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-PERSIST-001  | 19/19 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-CLI-001      | 25/25 | yes  | PASS   | 0 stage(s) failed    | PASS   |
| P16-REFMOD-001   | 26/26 | yes  | PASS   | 0 stage(s) failed    | PASS   |
```

**15 of 15 PASS.** Every one has a complete checkbox set, an evidence directory
with a recorded `RESULT: PASS`, and its own `qualification-times.txt` ending in
`0 stage(s) failed` — the harness's own verdict, where the exit code *is* the
number of failed stages.

`INFRA-NETGEN-001` has no checkbox block of its own: it is infrastructure
authorised out of band, and its TODO section is narrative. Its evidence and
harness artefact are present and PASS.

## Two apparent gaps, both of which were my own extraction

An audit that only reports agreement has not audited anything, so both of these
are recorded with what actually happened.

**1. P16-MAP-001 appeared to show the same regression figure as
P16-QUALITY-001 (3071/3071), breaking the monotone growth of the suite.** It
does not: MAP's own figure is **3121/3121**, and the 3071 my first extraction
found was MAP's *prerequisite audit* quoting QUALITY's number. Taking the first
four-digit match in a document that legitimately cites its predecessor's
results is not a reading of that document.

```text
P16-ARCH-001     2814     P16-QUALITY-001  3071
P16-DATA-001     2815     P16-MAP-001      3121
P16-GEOM-001     2900     P16-VIZ-001      3219
P16-SURF-001     2931     P16-CMD-001      3273
INFRA-NETGEN-001 2937     P16-PERSIST-001  3301
P16-VOL-001      2983     P16-CLI-001      3328
P16-SIZE-001     3011     P16-REFMOD-001   3381
```

Monotone throughout, which is itself a weak check that no milestone silently
removed tests.

**2. P16-CMD-001 appeared to have no `0 stage(s) failed` line.** It has one, at
`5:10:35.98`. The file I read was
`qualification/interrupted-run/qualification-times.txt` — a *deliberately kept*
record of a first attempt killed at its wrapper's two-hour ceiling, which stops
mid-run exactly as it should. Keeping an interrupted run rather than deleting it
is the right practice and is what made the gap momentarily ambiguous; a
`find | head -1` picked it over the real one.

Neither finding is a defect in the evidence. Both are recorded because the
alternative — quietly fixing my own query and reporting clean agreement — would
leave a reader unable to repeat the audit.

## Known limitations carried by the milestones, re-read

Each milestone's recorded limitations were re-read to separate a documented
boundary from an unfinished requirement.

```text
MILESTONE        LIMITATION                              KIND
P16-ARCH-001     ADR-030's general ValidatedMesh token    deferred to P17,
                 covers only the one mesh kind P16 has    recorded in the ADR
P16-DATA-001     Tet4 only; Tet4 is insufficient for      ADR-031 states it as
                 accurate bending stress                  a known limitation
P16-MAP-001      a drilled hole's cylindrical wall        naming-infrastructure
                 carries no FaceName; edges are not       limit, recorded; P16
                 mapped                                   has no edge consumer
P16-QUALITY-001  no threshold policy ships -- P17 owns    deliberate, stated in
                 what a solver requires                   the header
P16-VIZ-001      no clipping/section plane, no element    display features, not
                 labels in the 3D scene, no heatmap,      meshing requirements
                 one body per document in the GUI
P16-CMD-001      the GUI is not wired to a MeshControl    EXPLICITLY OUT of
                                                          scope, with a reason
P16-REFMOD-001   RM-MESH-09 configuration case deferred;  both reasoned and
                 a planar body's mesh quality cannot be   recorded
                 improved by any P16 control
```

**None of these is an unfinished P16 checklist item.** The two that come closest
are examined in full in `ADVERSARIAL_REVIEW.md` and carried into the final
known limitations:

* **The GUI does not read a `MeshControl`.** `apps/bettercad/MainWindow.cpp`
  calls `volumeMeshFor(document, regenerator, feature, {})` — default controls,
  not the document's canonical intent. P16-CMD-001 recorded this as `OUT` with
  its reason ("needs GUI undo, which TODO.md does not authorize here") and
  cross-referenced its own adversarial finding A3. It is a capability gap, not
  an authority violation: the GUI's mesh is still derived state, it uses the
  product's own `reportOnlyThresholds()` rather than inventing a policy, and it
  owns no canonical node, element, threshold or mapping.
* **ADR-031's connectivity sentence does not match the code.** See
  `ADR_AUDIT.md`; the ADR has been amended to record what was built and why.

## TODO audit

```text
open boxes in the whole of TODO.md            34, as audited, before PASS
  afterwards                                  0 -- all 34 ticked, and the P16
                                              phase block archived
sections holding them                         # P16-QUAL-001 only
P16 sections with any open box                none but this milestone
BLOCKED / FIXME / "known issue" mentions      3, all historical narrative
```

The three `BLOCKED` mentions are records of blocks that were *resolved*:
INFRA-NETGEN-001's `makerls` misdiagnosis, INFRA-VIEWER-001's out-of-band
authorisation after P16-VIZ-001 was blocked, and P16-VIZ-001's own earlier
block at `e7d90e9`. None is live.

`Current` / `Next` are consistent: Current milestone *none*, Next
`P16-QUAL-001`, with every other P16 milestone in the qualified list.
