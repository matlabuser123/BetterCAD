# P14-STREF-001 closure — qualification logs

The gate itself is the three `debug` / `release` / `debug-shared` runs and the
two repeat stages. `qualification-times.txt` has every stage and its exit code,
and the evidence is
[../README.md](../README.md), under CLOSURE.

## The three `*-local.log` files are a FAILED attempt, not part of the gate

They are the build logs from the attempt to move build output out of the
OneDrive-synced checkout, kept because they are the evidence for a finding that
outlives this milestone:

```text
windeployqt failed (1):
  Unable to find dependent libraries of
  <BETTERCAD_BUILD_ROOT>\gnu-16-mingw-amd64\bin\Qt6Core.dll
```

All three presets failed to link the GUI target the same way. `windeployqt`
resolves the Qt runtime relative to the executable it is deploying, as
`<exe dir>/../../<toolchain key>/bin`, which names the real Qt only when the
build tree sits inside the source tree.

The `-local` presets that produced these were REVERTED rather than committed, so
nothing in the repository refers to them. The configure, clean and repeat logs
from that attempt were deleted: the configures succeeded and say nothing, and
the repeat stages ran against binaries from an earlier build, so a reader could
mistake their exit 0 for a result. These three say what happened.
