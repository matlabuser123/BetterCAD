# P16-CLI-001 — the scripted end-to-end workflow

```text
SUBJECT:  the real executable, driven as separate processes, with every exit
          code asserted and the output parsed
SCRIPT:   tests/cli/MeshingWorkflow.cmake
TEST:     cli.mesh.workflow
RESULT:   21 steps, every exit code as expected
```

## 1. Why a script and not more unit tests

The in-process tests call `cli::run()` and prove the command logic. They
cannot prove the **executable** works: a real command line, a real path, the
runtime closure loading, stdout clean enough to parse, and the exit code
reaching the shell.

It fails fast. Each step asserts its exit code before the next runs, and a
mismatch is a `FATAL_ERROR` carrying the command, stdout and stderr. A script
that carried on after a failure and then reported success would be worse than
no script — which is why the brief asks for it explicitly.

It also records the binary it ran, by absolute path and size, so the evidence
names the executable that actually executed rather than the one that was meant
to.

## 2. The valid workflow, as it ran

```text
CLI:  C:/Users/uqhas/AppData/Local/bc-build/debug-ext/bin/bettercad-cli.exe
size: 330081339 bytes

step  1  exit 1   mesh-settings <doc>                  no control yet, and
                                                       stdout is EMPTY
step  2  exit 0   mesh-control-add <doc> --size 6mm
step  3  exit 0   mesh-set-global-size <doc> 5mm
step  4  exit 0   mesh-local-add <doc> face:Extrude001:end_cap 1.5mm
step  5  exit 0   mesh-settings <doc> --json
                      value     = 0.005
                      unit      = "m"
                      reference = "face:Extrude001:end_cap"
step  6  exit 0   mesh-generate <doc> --json
                      nodeCount    = 81
                      elementCount = 127
step  7  exit 0   mesh-info <doc> --json
                      nodeCount    = 81
                      elementCount = 127
                      state        = "current"
                      value        = 9.374866560399053e-05
step  8  exit 0   mesh-quality <doc> --json
                      invalidElements   = 0
                      structurallyValid = true
step  9  exit 0   mesh-validate <doc> --json
                      dataValid = true
step 10  exit 0   mesh-boundaries <doc> face:Extrude001:end_cap --json
                      resolved   = true
                      facetCount = 40
step 11  exit 0   mesh-info <copy> --json
                      the payload is BYTE-IDENTICAL to step 7's
step 12  exit 0   mesh-local-remove <doc> face:Extrude001:end_cap
step 13  exit 0   mesh-info <doc> --json
                      the payload CHANGED, as it must
```

Three assertions in that sequence are the substance rather than the shape.

**Step 1 prints nothing to stdout.** A query that cannot answer puts its
diagnostic on stderr and leaves stdout empty, which is the stream discipline
P15-CLI-001 established and the reason a pipeline can trust what it reads.

**Between steps 10 and 11 the saved document is searched** for `tetrahedra`,
`"nodes"`, `elementCount` and `connectivity`, and must contain none of them
while still containing `mesh-control`. So the byte-identical payload at step
11 came from the intent round-tripping, not from a mesh having been written.

**Step 13 must differ from step 7.** Removing the refinement has to change the
mesh. Without this the suite could pass against a CLI that ignored local
sizing entirely — the sort of thing that makes a green run meaningless.

## 3. The negative workflow

```text
step 14  exit 1   mesh-set-global-size <doc> -- -1mm    core refuses the value
step 15  exit 2   mesh-set-global-size <doc> -- 1zz     unreadable quantity
step 16  exit 2   mesh-local-add <doc> node:4 2mm       not a face reference
step 17  exit 1   mesh-local-remove <doc> face:...:9999 no such control
step 18  exit 1   mesh-info <absent.bcad>               no such document
step 19  exit 2   mesh-frobnicate <doc>                 unknown command
step 20  exit 0   new <empty.bcad> --force --name Empty
step 21  exit 1   mesh-control-add <empty.bcad>         nothing to mesh
```

After steps 14–19 the document is read again and must be **byte-identical** to
before them. Six refused commands, nothing written: `runEdit` saves only on
success, so this is a property of the edit spine — and the script measures it
rather than assuming it.

The exits are split 1 / 2 on a real distinction. Exit 1 means the core
refused the value and its diagnostic is the core's; exit 2 means the command
line never became a quantity or a reference at all. A tool that returned the
same code for both would make a script unable to tell "you asked for something
impossible" from "I could not read what you typed".

## 4. What the script does not do

It does not assert mesh handles. `NodeId`, `ElementId` and facet identities
belong to one generation; the comparisons are counts, volumes and validity.

It does not test a stale mesh, because a one-shot process cannot hold one —
see COMMAND_CONTRACT.md §2.

It does not use `PATH`. The executable is passed in by absolute path from
`$<TARGET_FILE:bettercad_cli>`, so each preset tests the binary it built.
