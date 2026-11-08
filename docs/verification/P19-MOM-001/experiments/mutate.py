"""P19-MOM-001 mutation probes, RUN rather than reasoned about.

An independent review produced a table of 21 single-line mutations and judged
nine of them survivors -- analytically, because it could not build the project.
This script applies each mutation to the real tree, builds, runs the momentum
suite, records whether the suite KILLED it, and reverts. The verdicts below are
measured.

Every probe is reverted before the next is applied, and the file's sha256 is
compared against the original at the end: a probe that failed to revert would
otherwise poison the qualification.
"""

import hashlib
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(r"C:\Users\Hasib\Desktop\CAD")
SRC = ROOT / "src" / "cfd" / "CfdMomentum.cpp"
HDR = ROOT / "include" / "bettercad" / "cfd" / "CfdMomentum.hpp"
TESTS = r"C:\bettercad\bc-build\debug-ext\bin\bettercad_tests.exe"
ENV = {"BETTERCAD_BUILD_ROOT": "C:/bettercad/bc-build"}

# (name, file, old, new, what the review predicted)
PROBES = [
    ("M1  conv + diff -> conv - diff", SRC,
     "values[i] = convectionValues[i] + diffusionValues[i];",
     "values[i] = convectionValues[i] - diffusionValues[i];",
     "predicted KILLED"),
    ("M2  rho*V/dt -> rho*V*dt", SRC,
     "input.density * geometry.cellVolume(CellIndex::fromValue(c)) / *input.timeStep;",
     "input.density * geometry.cellVolume(CellIndex::fromValue(c)) * *input.timeStep;",
     "predicted KILLED -- and now a COMPILE error, which is stronger"),
    ("M3  drop faceGradient from the assembly's diffusion input", SRC,
     """    const CfdDiffusionInput diffusionInput{input.viscosity, input.component, input.currentVelocity,
                                           input.faceGradient};""",
     """    const CfdDiffusionInput diffusionInput{input.viscosity, input.component, input.currentVelocity,
                                           {}};""",
     "predicted SURVIVOR"),
    ("M4  delete phase 1 (geometry describes topology)", SRC,
     "    if (!geometry.describes(topology)) {",
     "    if (false && !geometry.describes(topology)) {",
     "predicted SURVIVOR"),
    ("M5  delete phase 2's face-count arm", SRC,
     "    if (boundary.boundaryFaces() != topology.boundaryFaces()) {",
     "    if (false && boundary.boundaryFaces() != topology.boundaryFaces()) {",
     "predicted SURVIVOR"),
    ("M6  largestTransient = *least (both fields report the minimum)", SRC,
     "            largestTransient = *most;",
     "            largestTransient = *least;",
     "predicted SURVIVOR"),
    ("M7  smallestTransient = *most (both report the maximum)", SRC,
     "            smallestTransient = *least;",
     "            smallestTransient = *most;",
     "predicted SURVIVOR"),
    ("M9  never increment notDominant", SRC,
     "            ++notDominant;",
     "            notDominant += 0;",
     "predicted CONDITIONAL"),
    ("M11 previousVelocity[comp*cells + c] -> [c]", SRC,
     "Velocity::fromSi(input.previousVelocity[componentIndex * cells + c]);",
     "Velocity::fromSi(input.previousVelocity[c]);",
     "predicted KILLED"),
    ("M12 pressure term sign", SRC,
     "                -(volume * input.pressureGradient[c].component(input.component));",
     "                (volume * input.pressureGradient[c].component(input.component));",
     "predicted KILLED"),
    ("M13 drop the volume from the body force", SRC,
     "                input.density * volume * input.gravity->acceleration.component(input.component);",
     "                input.density * volume * input.gravity->acceleration.component(input.component) / volume * Volume::fromSi(1.0);",
     "predicted KILLED"),
    ("M14 every cell uses cell 0's volume", SRC,
     "        const Volume volume = geometry.cellVolume(CellIndex::fromValue(c));",
     "        const Volume volume = geometry.cellVolume(CellIndex::fromValue(0));",
     "predicted PARTLY"),
    ("M15 isfinite -> isnan in the scalar array gate", SRC,
     "            if (!std::isfinite(scalars[a].values[i])) {",
     "            if (std::isnan(scalars[a].values[i])) {",
     "predicted SURVIVOR"),
    ("M17 hard-code Upwind in the assembly's convection input", SRC,
     "    const CfdConvectionInput convectionInput{input.faceFlux, input.scheme, input.component};",
     "    const CfdConvectionInput convectionInput{input.faceFlux, CfdConvectionScheme::Upwind, input.component};",
     "predicted CONDITIONAL"),
    ("M18 transient diagonal += -> =", SRC,
     "                transient[c];",
     "                0.0 * transient[c] + transient[c];",
     "no-op control: the suite must still PASS"),
    ("M20 delete the source finiteness loop", SRC,
     "    for (std::size_t c = 0; c < cells; ++c) {\n        if (!std::isfinite(source[c])) {",
     "    for (std::size_t c = 0; c < cells && false; ++c) {\n        if (!std::isfinite(source[c])) {",
     "predicted SURVIVOR"),
    ("M22 drop the interpolation weight's effect: scheme ignored in the gate", HDR,
     "inline constexpr double kCfdMomentumRowSumTolerance = 1.0e-13;",
     "inline constexpr double kCfdMomentumRowSumTolerance = 1.0e-30;",
     "tolerance tightened past the measurement: the suite must FAIL, which shows "
     "the row-sum assertions are live"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build():
    r = subprocess.run(
        ["cmake", "--build", "--preset", "debug-ext", "--target", "bettercad_tests", "-j", "8"],
        cwd=ROOT, capture_output=True, text=True,
        env={**dict(__import__("os").environ), **ENV})
    return r.returncode == 0, r.stdout + r.stderr


def run_suite():
    r = subprocess.run([TESTS, "CfdMomentum_*"], cwd=ROOT, capture_output=True, text=True)
    return r.returncode == 0, r.stdout + r.stderr


originals = {SRC: SRC.read_text(encoding="utf-8"), HDR: HDR.read_text(encoding="utf-8")}
baseline = {p: sha(p) for p in originals}

print("P19-MOM-001 -- MUTATION PROBES, MEASURED")
print("=" * 78)
ok, log = build()
if not ok:
    print("BASELINE BUILD FAILED; aborting")
    print(log[-3000:])
    sys.exit(1)
passed, out = run_suite()
print(f"baseline: suite {'PASSES' if passed else 'FAILS'} -- {out.strip().splitlines()[-1]}")
print("=" * 78)

results = []
for name, path, old, new, predicted in PROBES:
    text = originals[path]
    count = text.count(old)
    if count != 1:
        results.append((name, "NOT APPLIED", f"anchor found {count} times", predicted))
        print(f"{name}\n    NOT APPLIED: anchor found {count} times")
        continue
    path.write_text(text.replace(old, new), encoding="utf-8")
    built, blog = build()
    if not built:
        verdict = "KILLED BY THE COMPILER"
        detail = "did not compile"
    else:
        survived, slog = run_suite()
        if survived:
            verdict = "SURVIVED"
            detail = slog.strip().splitlines()[-1]
        else:
            verdict = "KILLED"
            failing = [l for l in slog.splitlines() if l.strip().startswith("CfdMomentum_")]
            detail = f"{len(failing)} case(s) failed, first: {failing[0].strip() if failing else '?'}"
    path.write_text(text, encoding="utf-8")
    results.append((name, verdict, detail, predicted))
    print(f"{name}\n    {verdict}: {detail}\n    ({predicted})")

# revert and verify
for p, text in originals.items():
    p.write_text(text, encoding="utf-8")
print("=" * 78)
for p in originals:
    same = sha(p) == baseline[p]
    print(f"{p.name}: reverted exactly = {same}")
    if not same:
        print("  *** REVERT FAILED -- the tree is NOT the tree that was measured")
        sys.exit(1)

print()
print("SUMMARY")
killed = sum(1 for _, v, _, _ in results if v.startswith("KILLED"))
survived = sum(1 for _, v, _, _ in results if v == "SURVIVED")
print(f"  applied: {len(results)}   killed: {killed}   survived: {survived}")
for name, verdict, detail, predicted in results:
    print(f"  {verdict:<24} {name}")
built, _ = build()
print(f"\nfinal rebuild of the reverted tree: {'OK' if built else 'FAILED'}")
