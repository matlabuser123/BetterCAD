# P17-VIZ-001 — qualification freeze

```text
RESULT:  PASS. Every stage exited 0, 0 stages failed.
         qualify.cmd exit 0
         3:44:46 -> 6:57:49 on 2026-10-12, 3h13m
```

## The harness

Byte-identical to the one P16-SIZE-001 settled on and every milestone since
has used — **twenty-three in a row** counting this one. `verify-harness.cmd`
beside it is its regression and was not edited, so it was not re-run.

```text
qualify.cmd          git hash-object  d313a64070718c44fae290ac042fe259d1a03c8b
                     sha1sum          244b38de8feac0475af7cdc56ba9415d27d8dd29
verify-harness.cmd   git hash-object  11855b8ff891093b021d1e0351761283d0eedd24
                     sha1sum          afd0197d4f4726db7f533c2f27f4f6e069ba83c6
```

**BOTH FORMS ARE RECORDED, because they differ and a hash without its
algorithm cannot be checked.** `.gitattributes` sets `*.cmd text eol=crlf`, so
git stores LF while the working tree is CRLF. A note carrying only the
git-blob value makes a `sha1sum` check fail against a harness that is
*correct*, and this milestone lost ten minutes to exactly that before
realising which hash it was holding.

The count was then derived rather than carried from prose: that `sha1sum`
appears in **22** of the `qualify.cmd` copies under `docs/verification/`
EXCLUDING this milestone's own copy, and in **23 of 95** including it. Stating
which is which matters -- the first draft of this line said "22 of 94" and was
already stale, because copying the harness into this directory had changed
both numbers.

## The qualified trees

Recorded by `qualify.cmd` **before the first build** and recomputed after the
last test run. All eight identical:

```text
apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           b541fd58dc7f5d909062daffa384635e2cdbcc24
src               9058a9485c6b27bf7216566dcab15a26358f8f1b
tests             c66668e045fa6f4e8b9ea3e55e7e84896b25fe93
examples          75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Cross-checked by a second, independent method — `find … | sha1sum` over each
path, computed before the run and again after — which also reports all eight
unchanged. **The tree that was qualified is the tree that is committed.**

Qualification candidate (the commit this work is based on):
`b2bd8e37acdf93f8b12a6a2fe7228dcbc68c4154`.

## Stages

| preset | configure | clean | build | no-op rebuild | ctest |
| --- | --- | --- | --- | --- | --- |
| debug-ext | 0 | 0 | 0 | 0 | **3792 / 3792** |
| release-ext | 0 | 0 | 0 | 0 | **3792 / 3792** |
| debug-shared-ext | 0 | 0 | 0 | 0 | **3792 / 3792** |

```text
build warnings   0 in all three presets, with -Werror on
build errors     0 in all three presets
StructuralViz_   33 tests run and passed in EACH preset
```

The **no-op rebuild** stage is what proves the binaries tested are the ones
just built from the frozen tree — not a stale artefact. It exited 0 for all
three.

## Determinism

Each repeat stage runs the filtered set **five times**:

| preset | selected | result |
| --- | --- | --- |
| release-ext | 937 | **937 / 937** |
| debug-ext | 937 | **937 / 937** |

937 was counted with `ctest -N` against the filter before the run, not
estimated, and the harness's own count agrees — it records
`repeat <preset> selects 937 test(s)` and treats a filter matching nothing as a
failed stage, twice over.

Two presets rather than one, because a verdict that depended on optimisation
level would be a defect: `-O0` and `-O2` reassociate floating point
differently, and every value this milestone publishes is a double.

The filter is P17-VALID-001's plus `unit\.View`. Its composition and the reason
for each component are in
[qualification/run-qualification.cmd](qualification/run-qualification.cmd),
recorded there so the run is reproducible rather than described. All six
renderer prefixes are inside it, verified by counting:

```text
unit.StructuralViz   33      this milestone
unit.MeshView        13      the render buffers and the index-to-id bridge
unit.MeshDisplay     15      the single metres-to-millimetres conversion
unit.MeshScene        9      where a type-name collision was found
unit.MeshHighlight    9      the path both highlight entry points delegate to
unit.Viewer          22      the library that gained three sources
```

## What this run also discharges

**P17-VALID-001 is requalified by it.** `StructuralSolveOutcome` gained
`fields` and `reactions` during this milestone, because `StructuralResult`
stores `Strain6` and `Stress6` but **not** von Mises, the principal stresses,
the principal strains, the hydrostatic stress or `|u|` — so a viewer
restricted to the published result would have had to recompute them, which is
the duplicate numerical authority this milestone is forbidden to create. Both
objects were already computed one statement earlier and discarded, so the
change is additive and computes nothing new.

That debt was recorded as outstanding in an earlier revision of
[REMAINING_SCOPE.md](REMAINING_SCOPE.md). It is discharged here, by a run that
rebuilt from clean in three presets and repeated P17-VALID's own set five times
in two of them — not assumed away.

## Pre-freeze checks

Cheap before expensive, because all three of P15's voided qualifications came
from the other order:

```text
git diff --check                            clean
ctest -R StructuralViz_ --repeat until-fail:5   33/33 x 5
debug-shared-ext configure + build + run    links clean, 33/33
full debug-ext suite                        3792/3792 in 1502 s
  (the pre-freeze run measured 1520 s; the figure above is the
   QUALIFICATION's own debug-ext stage, which is the one this document is
   about -- the two were briefly conflated)
```

The shared build is the only preset that catches a DLL-boundary defect, and it
mattered here because a new header carries export macros. Checked by grep
first — no macro on any `constexpr` or `inline`, and `ContourLegend`'s two
out-of-line members each carry it at member level, the pattern
`RenderedImage::at` already uses — and then confirmed by the link.

One whitespace-only correction landed before the freeze:
`src/renderer/CMakeLists.txt` was CRLF in the working tree while HEAD has it
pure LF and `.gitattributes` says `* text=auto eol=lf`. An earlier session's
edit had converted it. Normalised back, with a content check that would have
failed on anything but the carriage returns. `git diff` shows only the four
intended lines.

## The error log

`run-qualification.err` holds **268** lines, **every one of them** a git
`CRLF will be replaced by LF` warning about pre-existing working-tree files.
Zero non-warning lines.

P17-VALID-001's `.err` holds 270, and the difference is accounted for rather
than waved at: a diff of the two file lists shows the only delta is
`src/renderer/CMakeLists.txt`, present TWICE in P17-VALID's and absent from
this one -- because this milestone normalised that file to LF, and git emits
the warning once per invocation while `qualify.cmd` fingerprints the tree both
before the first build and after the last test. 270 - 2 = 268.

That is how "zero non-warning lines" was established as the expected state
rather than assumed to be one.

## How the run was launched

Through [qualification/launch.cmd](qualification/launch.cmd), which performs
the redirect itself so that no quoting crosses a process boundary. The first
attempt used `Start-Process` with the redirect inside its argument list; the
quoting was mangled, cmd exited immediately and created nothing. **Redirects,
never pipes** — a pipe loses the harness's exit code, which is the whole
mechanism by which a failed stage fails the command that ran it.

`run-qualification.cmd` was checked before the run, as the freeze requires:
every line begins with `rem`, `set`, `call`, `echo`, `setlocal`, `endlocal` or
`@echo`, and it carries no bare heredoc terminator — the defect that reached
P17-ASSEMBLY-001's and P17-SOLVE-001's copies. It is CRLF, because cmd.exe
reads a batch file by byte offset and `.gitattributes` requires it.

It was not edited while it was running. Not even a comment.
