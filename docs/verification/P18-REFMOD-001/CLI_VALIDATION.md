# P18-REFMOD-001 — CLI validation

**Item 20.** The reference models through the **real executables**, in separate processes that share nothing but bytes on disk.

```
bettercad_example_reference_models --thermal --out <dir>     # builds, saves, meshes, solves, judges 20 documents (process A)
bettercad-cli thermal-validate|solve|temperature|heat-flux|balance <dir>/thermal_*.bcad --json   # process B…N
```

Two layers, both run by `ctest`:

| | What | Name |
|---|---|---|
| Visible process tests | the runner (its **own exit code** is an assertion: any model that does not build, regenerate, mesh, solve when it must, refuse when it must, or be re-judged stale after an edit, fails it), then `thermal-validate` and `thermal-solve` on the five accepted documents and `thermal-solve` on the five refused ones | `refmod.thermal.build` (fixture), `…validate.` and `…solve.` on `thermal_{slab,convection,lumped,surface_flux,transient_slab}`, `…refused.thermal_{singular,singular_balanced,unresolved_load,unresolved_temperature,unresolved_convection}` — **16 tests plus the fixture = 17** |
| The scripted two-binary workflow | `tests/cli/ThermalReferenceWorkflow.cmake` — **58 processes**, every exit code asserted, every payload parsed twice (CMake `string(JSON)` and the strict RFC 8259 reader), the fresh-binary ledger written | `refmod.thermal.workflow` |

## The workflow (58 processes)

1. **Fresh-binary proof**, before any process runs. For the CLI, the runner **and the test executable that does the numeric comparison** (added after the review: a stale tests binary would otherwise print "all passed"): the path is absolute (`$<TARGET_FILE:…>`) and inside the build tree; its size and SHA-256 are recorded; its runtime closure is resolved against its own directory (a shared preset must find every `libbettercad_*.dll` beside it, a static one none; the headless CLI may load no Qt). The ledger (`ledger.txt`) names the exact binaries that ran. *What this proves and what it does not:* path, location and identity are recorded; that the binary was built from THIS source is the qualification harness's clean build, no-op rebuild and the frozen source fingerprints (`FREEZE.md`) — the script cannot know it.
2. **Process 1 — the runner.** `--thermal --out docs`. Asserted: exit 0; the 11 documents the CLI is run on exist; **exactly 15** documents were re-judged stale after an edit (`mesh_stale_finding yes`) and **exactly 5** refused (`expected_outcome refusal actual refusal published_result no`); the stale-mesh model ran.
3. **Five accepted models × (validate, solve, solve again) = 15, plus 10 summaries = 25 processes:** `accepted: true`, `stoppedAt: null`, validation `PASS`, `heatBalance.balanced`; and **the second solve payload is byte-identical to the first** — a different process, nothing carried over, the same answer to the last digit. The document's SHA-256 is unchanged after the solves.
4. **Five expected failures × (5 summaries + validate) = 30 processes:** `thermal-solve/-temperature/-gradient/-heat-flux/-balance` each exit **1 with nothing on stdout** and the real cause on stderr (`<program> <command>: <code>: <message>`): `no_temperature_datum` (×2 models), `loads_invalid`, `boundary_conditions_invalid` (×2); `thermal-validate` exits 1 **with** the report on stdout (`accepted: false`, `stoppedAt` = `solve` / `solve` / `loads` / `boundary` / `boundary`). The three unresolved-target refusals each say *"Nothing nearby is substituted"*. The document is byte-identical after the refused commands. (The CLI's `no_temperature_datum` text reads "no part of the model is anchored to an absolute temperature…"; the word *datum* is in the code, the in-process failure message carries the longer explanation.)
5. **The stale-mesh model, edited between processes (2 processes).** A one-shot process cannot be handed a stale mesh — each loads, regenerates and meshes afresh — so the test that matters is that it does not *remember* one: the saved intent is edited (`stale_l` 30 → 36 mm) and a second process must report the **new** flux, not the old.
6. 1 + 25 + 30 + 2 = **58** processes: 1 runner, 25, 30, 2.
7. **The numbers (the part CMake cannot do).** The script runs `bettercad_tests "[refmod-cli]"` over the raw bytes: **six** hidden cases, strict-parsed, compared with the closed forms and with the same model run in process through the core:

| Case | CLI value | Closed form |
|---|---|---|
| slab, `thermal-solve` | min 300 K, max 400 K | `Tc`, `Th` |
| slab, `thermal-heat-flux` | max = min = **800 W/m²**, flux = (0, 0, 800) | `k ΔT/L = 8 × 100 / 1` |
| slab, `thermal-balance` | net reaction `0 W` | sums to zero |
| surface flux, solve / heat-flux / balance | min 300, max **310 K**; flux (0, 0, **−500**) W/m²; surface heat **+1.25 W** into the body, held-cap reaction **−1.25 W** | `T = 300 + 100 z`, `q'' A = 500 × 0.0025` |
| convection, `thermal-solve` | max 400 K, min **350 K** | `Th`; `Ts = T∞ + Q Rconv` |
| convection, `thermal-balance` | reaction **+200 W**, convection **−200 W**, area 0.2 m² | `(Th − T∞)/(Rcond + Rconv)`; sign: heat **into** the body |
| held-cap transient slab | state 10 at **500 s**, hottest node = the held 400 K, min/max equal the core's | (the independent integrator agrees with the core in process, `RM-THERM-08.md`) |
| lumped, `thermal-temperature` | state 46 at **4600 s**, min/max within 0.02 K of the backward-Euler value 300.868 K | `T_n = T∞ + (T0 − T∞)/(1 + dt/τ)ⁿ` |
| lumped, `thermal-balance` | 46 states, final step 100 s, stored-energy rate = convection | energy balance |
| stale model, before / after the edit | 16 666.67 → 13 888.89 W/m² | `k ΔT/L`, L = 30 / 36 mm |
| five refusals | stage and first error code per document | table above |

Plus `ThermalRefmodCli_EveryPayloadIsStrictJson` over **every** `.json` payload (**32** non-empty objects, **25** empty — the refusals — exactly).

8. **Exact process count (58)** and the exact payload counts are asserted: a step removed and a step added both fail the script. `ctest -N` counts per group are in `tests/cli/ZeroMatchGuard.cmake`. The hidden cases are run by the tag `[refmod-cli]`; the script requires the Catch summary to say *6 test cases*, so a tag that matched fewer (or none — Catch exits non-zero on a zero match) fails.

## What it deliberately does not do

It does not grep numbers (`string(JSON)` and the strict reader parse; the digits are compared as numbers). It does not accept "exit 0" as the whole answer for a model that must fail (exit 1 *and* empty stdout *and* the code on stderr *and* the report's stage). It never launches `bettercad-cli` by bare name: a stale binary on `PATH` cannot be tested by accident. It does not claim the CLI's transient comparison is tight: 0.02 K is a regression guard from the measured 0.0084 K spread (`TOLERANCES.md`).
