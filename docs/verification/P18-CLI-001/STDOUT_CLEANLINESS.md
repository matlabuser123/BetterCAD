# P18-CLI-001 — raw structured stdout is clean

## The contract

With `--json`, a command that can answer writes **exactly one JSON object followed by one line ending to stdout, and nothing else**: no banner, no progress line, no warning, no second object, no ANSI escape, no backend chatter. A command that cannot answer writes **nothing** to stdout and one `<program> <command>: <code>: <message>` line to stderr (the stream discipline P13–P17 fixed). The two reports that *are* the answer when a result is rejected (`thermal-validate`, `thermal-balance` for an out-of-tolerance balance) print their payload **and** exit 1.

## Where it is tested — five layers, each independent of the CLI's writer

1. **In process** — `ThermalCli_StructuredStdoutIsClean`: all seven reports, steady and transient, parsed by the strict reader (`JsonReader`, RFC 8259: rejects trailing text, repeated keys, trailing commas, leading zeros, `nan`, `inf`); first byte `{`, last byte `\n`, no `0x1b`, stderr empty. `ThermalCli_StructuredFailureOutputIsClean`: every refusal prints an empty stdout and exactly one stderr line; the validation report is parseable *and* exits 1; `ThermalCli_ARejectedBalanceIsStillPrintedAndTheExitStatusSaysNo`.
2. **Third-party output** — `ThermalCli_ThirdPartyOutputDoesNotPolluteStdout`: a document with a local size restriction makes Netgen write a warning to `std::cout`; the test captures the stream's `rdbuf` while the CLI runs and requires the payload to still be strict JSON. The workflow does the same in a real process (`local-size.bcad`, which also asserts the node count changed so the restriction is live, and that `RestrictLocalH` is absent). The suppression is in the meshing adapter and nothing in the thermal CLI filters output.
3. **Non-finite numbers** — `JsonValue::number(double)` would write `nan`/`inf`, which is not JSON. All thermal numbers go through `FiniteJson` (`ThermalOutput.hpp`), which writes `null` and records that it did. `ThermalCli_NonFiniteNumbersAreNeverWrittenAsNumbers`, `…ANonFiniteMeasurement…`, `…ValidationJsonWritesANonFiniteMeasurementAsNullAndSaysSo` (hand-built report with a NaN measurement → `value: null`, `valueFinite: false`). The gate forbids a double reaching `JsonValue::number` directly.
4. **Real process, raw bytes** — `ThermalWorkflow.cmake` captures every process's stdout with `execute_process(OUTPUT_FILE)` (binary, CRLF preserved), records its byte count and SHA-256 in `ledger.txt`, and parses it twice: once with CMake's `string(JSON)` (catches a banner or an unparseable value) and, because `string(JSON)` is lenient (measured by the independent review: it accepts trailing text, a repeated key, a trailing comma and a leading zero), once more at the end by the strict reader over the **raw files** (hidden test `ThermalCliWorkflow_EveryPayloadIsStrictJson`, which also requires at least 25 payloads, so a workflow that wrote nothing cannot pass).
5. **Structural** — the gate forbids `std::cout`, `std::cerr`, `printf`, `puts`, `std::print(ln)` and stream `.write(` in any thermal CLI file; all output goes through the one `out` stream the driver passes in.

## Prose mode

Without `--json` the output is human text; the workflow asserts its first line has the documented form and that it contains no `{`.

## Not claimed

Console code page and Windows line endings: on a real console the `\n` is `\r\n`; the workflow records raw bytes (CRLF) and the strict reader accepts both. A consumer that reads the payload in text mode sees `\n`.
