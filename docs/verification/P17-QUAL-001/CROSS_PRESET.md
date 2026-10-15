# P17-QUAL-001 — cross-preset equivalence

```text
SUBJECT:  whether the three presets compute the SAME structural answers, not
          merely pass the same tests
METHOD:   after the qualification, each preset's own clean-built binaries
          (FREEZE.md records their hashes) ran the runner, the reference
          suite, the three final gates and the CLI on the committed
          reference documents; the outputs were compared byte for byte
          (qualification/postrun/)
```

## The runner: twelve documents, every line

`bettercad_example_reference_models --structural` prints, per document, the
validation stage and findings, the solve's free equations, residual, pivot
ratio and strain energy, the largest displacement and von Mises, the total
and external reactions, the equilibrium ratios, and the model-change gate's
counts and re-solved energies -- every double to 17 significant digits.

```text
debug-ext          reference-run-debug-ext.txt           164 non-timing lines
release-ext        identical, 0 differing lines
debug-shared-ext   identical, 0 differing lines
P17-REFMOD-001     identical to all three, 0 differing lines
```

So the twelve documents solve to the same 17-digit numbers under `-O0 -g`,
under the optimiser, and across a DLL boundary -- and to the same numbers
P17-REFMOD-001 recorded on the parent commit, which is what byte-identical
production code (`src` f3c1245d, `apps` a95876bc, `examples` a955cf65 in
both freezes) predicts.

## The reference suite: fingerprints and measurements

The suite's `Fingerprint` hashes every double's own bit pattern over every
displacement, strain, stress, principal value, von Mises, hydrostatic stress
and reaction, so equality is bit-exact:

```text
                              debug-ext             release-ext           debug-shared-ext
RM-FEA-01                     0x367f5be15a7ad964    same                  same
RM-FEA-02                     0xf7f8eac642d0dbdf    same                  same
RM-FEA-03                     0xdde09da083f54664    same                  same
RM-FEA-04                     0x7080dbacde25e0ec    same                  same
RM-FEA-05                     0xd9bb91489e45f1d2    same                  same
RM-FEA-06 base / placed       0x7711566fd39cb23c / 0xbf2b68f30eadd86   same   same
RM-FEA-07 root / tip          0x482f3f482809b849 / 0x1ffb082549c98bcc   same   same
RM-FEA-09                     0x665456e4bf19f4fe    same                  same

suite                         16 cases, 133 872 assertions in each preset
WARN message lines            337 in each preset, 0 differing between any two
```

The 337 message lines are the measured tolerances, counts, medians and
fingerprints the suite prints; `ANALYTICAL_VALIDATION.md` transcribes them.
They are also identical, line for line, to the P17-REFMOD-001 run.

## The final gates

`[p17qual]`: 3 cases, 400 assertions in each preset. The fingerprints the
gates print -- RM-FEA-04 before and after its seven-edit sequence
(`0x7080dbacde25e0ec` -> `0x8f56696c4cdf9274`) and RM-FEA-09 recovered
after a failed rebuild (`0x665456e4bf19f4fe`) -- are the same three values
in every preset (the order differs only because Catch2 shuffles cases).

## The CLI: payload bytes

`bettercad-cli structural-solve <document> --json`, each preset's CLI on
the document its own runner wrote, stdout captured raw through `cmd.exe`:

```text
                                        bytes   sha256 (first 16)
structural_axial_bar.bcad                 736   a678f085746359f6   x 3 presets
structural_cantilever.bcad               1725   7157138c154795a6   x 3 presets
structural_transformed_placed.bcad        734   207d88a021865406   x 3 presets
```

**Byte-identical across all three presets.** The sizes differ from
P17-REFMOD-001's table (701, 1671, 699) by exactly the line count of each
payload (35, 54, 35): that capture had LF line endings and this one has
CRLF as `cmd.exe` writes the console stream, and the 35 / 54 / 35 lines of
JSON are the same text. The payload holds no path, no timestamp, no
revision stamp and no duration (`postrun/structural_axial_bar-debug-ext.raw.json`),
which is why the comparison is available at all.

## Verdict

```text
cross-preset equivalence   PASS -- measured on this candidate's binaries,
                           recorded as a measurement and not promoted to a
                           general guarantee
```
