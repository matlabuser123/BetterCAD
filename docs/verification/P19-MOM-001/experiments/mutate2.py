"""Round 2: the probes round 1 missed, and the one it mislabelled.

ROUND 1'S M18 WAS A NO-OP AND CARRIED THE REAL MUTATION'S NAME. The replacement
was `0.0 * transient[c] + transient[c]`, which is arithmetically the identity
for every finite value, so its survival was the CONTROL behaving correctly --
it shows the other sixteen kills are attributable to their mutations and not to
build flakiness. But the label described the review's actual M18
(`values[slot] += transient[c]` -> `= transient[c]`), which round 1 therefore
never ran. It is run here under its own name, and the no-op is renamed to what
it is.
"""

import hashlib
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(r"C:\Users\Hasib\Desktop\CAD")
SRC = ROOT / "src" / "cfd" / "CfdMomentum.cpp"
TESTS = r"C:\bettercad\bc-build\debug-ext\bin\bettercad_tests.exe"
ENV = {"BETTERCAD_BUILD_ROOT": "C:/bettercad/bc-build"}

PROBES = [
    ("M18 transient diagonal += -> = (the REAL one)",
     """            values[static_cast<std::size_t>(std::distance(convectionInner.begin(), found))] +=
                transient[c];""",
     """            values[static_cast<std::size_t>(std::distance(convectionInner.begin(), found))] =
                transient[c];""",
     "predicted KILLED"),
    ("M19 delete the MATRIX finiteness loop",
     "    for (std::size_t i = 0; i < values.size(); ++i) {\n        if (!std::isfinite(values[i])) {",
     "    for (std::size_t i = 0; i < values.size() && false; ++i) {\n        if (!std::isfinite(values[i])) {",
     "predicted SURVIVOR -- the source loop fires with the same tag"),
    ("M16 reorder the three cell-wise source terms (gravity before pressure)",
     """        if (hasPressure) {
            const Force pressureForce =
                -(volume * input.pressureGradient[c].component(input.component));
            total += pressureForce.si();
        }
        if (hasGravity) {
            const Force bodyForce =
                input.density * volume * input.gravity->acceleration.component(input.component);
            total += bodyForce.si();
        }""",
     """        if (hasGravity) {
            const Force bodyForce =
                input.density * volume * input.gravity->acceleration.component(input.component);
            total += bodyForce.si();
        }
        if (hasPressure) {
            const Force pressureForce =
                -(volume * input.pressureGradient[c].component(input.component));
            total += pressureForce.si();
        }""",
     "predicted PROBABLY, not by construction -- 57.45% per sample"),
    ("M10 dominance `<` -> `<=`",
     "        if (diagonal < offDiagonal) {",
     "        if (diagonal <= offDiagonal) {",
     "predicted SURVIVOR -- needs an exact tie"),
    ("M23 drop the pattern verification (inner arrays)",
     "        if (convectionInner[i] != diffusionInner[i]) {",
     "        if (false && convectionInner[i] != diffusionInner[i]) {",
     "ADR-073 Decision 2's gate: unreachable, so a SURVIVOR is the honest answer"),
    ("M24 scheme phase accepts anything (the silent substitution restored)",
     "    if (std::ranges::find(kCfdConvectionSchemes, input.scheme) == kCfdConvectionSchemes.end()) {",
     "    if (false && std::ranges::find(kCfdConvectionSchemes, input.scheme) == kCfdConvectionSchemes.end()) {",
     "the new phase: must be KILLED or it is decoration"),
    ("M25 convection sub-tag not mapped (the wrong tag restored)",
     "        if (sub == CfdConvectionProblem::ResultIsNotFinite) {",
     "        if (false && sub == CfdConvectionProblem::ResultIsNotFinite) {",
     "the review's finding 1 fix: must be KILLED"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build():
    r = subprocess.run(
        ["cmake", "--build", "--preset", "debug-ext", "--target", "bettercad_tests", "-j", "8"],
        cwd=ROOT, capture_output=True, text=True, env={**dict(os.environ), **ENV})
    return r.returncode == 0


def run_suite():
    r = subprocess.run([TESTS, "CfdMomentum_*"], cwd=ROOT, capture_output=True, text=True)
    return r.returncode == 0, r.stdout + r.stderr


original = SRC.read_text(encoding="utf-8")
baseline = sha(SRC)

print("P19-MOM-001 -- MUTATION PROBES, ROUND 2")
print("=" * 78)
if not build():
    print("BASELINE BUILD FAILED")
    sys.exit(1)
passed, out = run_suite()
print(f"baseline: suite {'PASSES' if passed else 'FAILS'} -- {out.strip().splitlines()[-1]}")
print("=" * 78)

results = []
for name, old, new, predicted in PROBES:
    count = original.count(old)
    if count != 1:
        print(f"{name}\n    NOT APPLIED: anchor found {count} times")
        results.append((name, "NOT APPLIED", predicted))
        continue
    SRC.write_text(original.replace(old, new), encoding="utf-8")
    if not build():
        verdict, detail = "KILLED BY THE COMPILER", "did not compile"
    else:
        survived, slog = run_suite()
        if survived:
            verdict, detail = "SURVIVED", slog.strip().splitlines()[-1]
        else:
            failing = [l for l in slog.splitlines() if l.strip().startswith("CfdMomentum_")]
            verdict = "KILLED"
            detail = f"{len(failing)} case(s) failed, first: {failing[0].strip() if failing else '?'}"
    SRC.write_text(original, encoding="utf-8")
    results.append((name, verdict, predicted))
    print(f"{name}\n    {verdict}: {detail}\n    ({predicted})")

SRC.write_text(original, encoding="utf-8")
print("=" * 78)
same = sha(SRC) == baseline
print(f"CfdMomentum.cpp: reverted exactly = {same}")
if not same:
    sys.exit(1)
print()
print("ROUND 2 SUMMARY")
for name, verdict, predicted in results:
    print(f"  {verdict:<24} {name}")
print(f"\nfinal rebuild: {'OK' if build() else 'FAILED'}")
