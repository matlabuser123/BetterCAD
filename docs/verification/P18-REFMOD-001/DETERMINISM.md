# P18-REFMOD-001 — Determinism

**Item 22.** Nothing in the chain may depend on a clock, an address, a hash order or a thread. "The same answer" here means **the same bits**, not "within a tolerance": a result that differs in the 15th digit between two runs of the same document is a result that depends on something it should not.

## In process (`Determinism_*`, 2 cases)

A **fingerprint** hashes the bit pattern (`std::bit_cast<uint64_t>`, an FNV-style 64-bit mix over whole words) of every nodal temperature of every stored state, the stored times, the final per-element gradients and heat fluxes, every balance term, step size and per-node reaction of every step, and the worst balance residual. Not covered: the intermediate states' gradients and fluxes, and the mesh's coordinates and ids (the mesh is covered indirectly — a different mesh changes every temperature).

* `RepeatedRunsAreBitIdentical`, for six documents (slab, convection, lumped, surface flux, generation box, held-cap transient slab): **five runs from five independently built documents** (build → regenerate → mesh → solve; independent *objects* in one process — process-level independence is the CLI byte comparison below), **plus five more solves of one document with the mesh held** — ten runs, **one fingerprint**, and the two groups agree.
* `OrderOfRunsDoesNotMatter`: the models run forward and then in the opposite order in the same process; the fingerprints match model by model. No case depends on another having run (this shows order independence; it does not by itself prove that nothing is cached).

Persistence adds a third axis (`PERSISTENCE.md`): a document **saved, destroyed, loaded and solved** from nothing has the same fingerprint as the original.

## Across processes

The workflow runs `thermal-solve` twice on each accepted document in **different processes**; the two stdout payloads are **byte-identical** (`CLI_VALIDATION.md`, step 3) — the strongest form of the claim, since the payload carries every printed digit.

## Across presets (debug-ext / release-ext / debug-shared-ext)

Three build configurations, two compilation modes (static, shared) and -O0/-O3 — floating-point results could in principle differ in the last bit between them. The qualification compares, per preset: the workflow's 22 JSON payloads and the 18 documents the runner wrote (SHA-256), and the printed numbers. The result is recorded in `qualification/CROSS_PRESET.md` (written by the post-run script from the three workflow ledgers).
