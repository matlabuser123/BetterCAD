"""P19-TURB-ARCH-001 -- do the new pins actually fail when what they pin moves?

A gate that cannot fail is not a gate. Each mutation below is applied to a COPY-
PROTECTED production file, the test target is built, the diagnostics are recorded,
and the file is restored and verified byte-identical by sha256 before the next one
runs. Nothing is left mutated: the restore is checked, not assumed.

Four mutations, and the first two together are what make the G1 claim exact:

  M1  widen kCfdVariables to four and leave kCfdFields at three
      EXPECTED: caught ALREADY, by CfdFields.hpp's own parallel-order assert,
      which indexes kCfdFields[i] over kCfdVariables.size(). So this half of the
      gap was never open, and the milestone must not claim it.
  M2  widen BOTH tables to four, consistently
      EXPECTED: every assertion in CfdFields.hpp passes -- the parallel-order loop
      holds for four rows, and the dimension and component asserts index 0, 1 and 2
      explicitly -- and ONLY the new count pin fails. This is the gap G1 closes.
  M3  add a fourth member to CfdBoundaryTreatment
      EXPECTED: the new size mirror or its structured binding fails. Nothing in the
      tree caught this before.
  M4  make canonicalText emit a turbulence line
      EXPECTED: the new absence test fails AT RUNTIME, which is the check that
      decisions 1 and 7 are asserted over real output and not over a constant.
"""

# WHY EVERY RESTORE IS FOLLOWED BY os.utime. shutil.copy2 PRESERVES THE MTIME, so a
# byte-identical restore left the file looking OLDER than the object files built from
# the mutated one, and ninja then considered those objects up to date. The next build
# ran 3 edges, linked a binary holding a four-variable isRecognised and a canonicalText
# that still emitted a turbulence line, and TWO OF THIS MILESTONE'S OWN TESTS FAILED --
# which read exactly like a product defect and was not one. Restoring the content is
# not restoring the tree; the mtime is part of the build state.


import hashlib
import io
import os
import shutil
import subprocess
import sys

ROOT = r'C:/Users/Hasib/Desktop/CAD'
EXP = ROOT + r'/docs/verification/P19-TURB-ARCH-001/experiments'
WINLIBS = (r'C:/Users/Hasib/AppData/Local/Microsoft/WinGet/Packages/'
           r'BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin')
CMAKE = WINLIBS + '/cmake.exe'

FIELDS = ROOT + '/include/bettercad/cfd/CfdFields.hpp'
BOUNDARY = ROOT + '/include/bettercad/cfd/CfdBoundary.hpp'
CANONICAL = ROOT + '/src/cfd/CfdAnalysisObject.cpp'


def sha(path):
    return hashlib.sha256(io.open(path, 'rb').read()).hexdigest()


def rd(path):
    return io.open(path, encoding='utf-8', newline='').read()


def wr(path, text):
    io.open(path, 'w', encoding='utf-8', newline='').write(text)


def build(tag):
    env = dict(os.environ)
    env['PATH'] = WINLIBS + os.pathsep + env.get('PATH', '')
    env['BETTERCAD_BUILD_ROOT'] = 'C:/bettercad/bc-build'
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = '4'
    r = subprocess.run([CMAKE, '--build', '--preset', 'debug-ext', '--target', 'bettercad_tests'],
                       cwd=ROOT, env=env, capture_output=True, text=True)
    out = (r.stdout or '') + (r.stderr or '')
    wr(EXP + '/falsify-%s.log' % tag, out)
    return r.returncode, out


def run_tests(tag):
    exe = 'C:/bettercad/bc-build/debug-ext/bin/bettercad_tests.exe'
    r = subprocess.run([exe, '[cfd][turbarch]'], capture_output=True, text=True,
                       cwd='C:/bettercad/bc-build/debug-ext/bin')
    out = (r.stdout or '') + (r.stderr or '')
    wr(EXP + '/falsify-%s.log' % tag, out)
    return r.returncode, out


results = []


def mutation(tag, path, edits, expect_build_failure, expect_markers, run_after=False):
    """Apply `edits` (list of (old, new)) to `path`, build, record, restore."""
    print()
    print('=' * 72)
    print('M%s  %s' % (tag[0], tag[2:]))
    print('=' * 72)
    before = sha(path)
    pristine = EXP + '/' + os.path.basename(path) + '.pristine'
    if not os.path.exists(pristine):
        shutil.copy2(path, pristine)
    text = rd(path)
    for old, new in edits:
        assert text.count(old) == 1, 'edit anchor found %d times: %r' % (text.count(old), old[:60])
        text = text.replace(old, new)
    wr(path, text)
    print('  applied, file sha %s -> %s' % (before[:12], sha(path)[:12]))

    try:
        if run_after:
            rc, out = build(tag)
            if rc != 0:
                print('  UNEXPECTED: the mutated tree did not compile')
                ok = False
                found = []
            else:
                rc, out = run_tests(tag)
                found = [m for m in expect_markers if m in out]
                ok = (rc != 0) and bool(found)
                print('  test run exit %d' % rc)
        else:
            rc, out = build(tag)
            found = [m for m in expect_markers if m in out]
            ok = (rc != 0) == expect_build_failure and bool(found)
            print('  build exit %d (expected failure: %s)' % (rc, expect_build_failure))
        for m in expect_markers:
            print('     %-5s %s' % ('FOUND' if m in out else 'absent', m[:66]))
    finally:
        shutil.copy2(pristine, path)
        os.utime(path, None)  # see the note at the top: copy2 keeps the mtime
        after = sha(path)
        restored = (after == before)
        print('  restored: %s (sha %s)' % ('byte-identical' if restored else 'MISMATCH', after[:12]))
        if not restored:
            print('  *** THE TREE IS NOT AS IT WAS. STOP. ***')
            sys.exit(2)

    results.append((tag, ok))
    print('  %s' % ('OK -- the gate bit' if ok else 'DID NOT BITE AS EXPECTED'))


# ----------------------------------------------------------------- M1
mutation(
    '1-widen-variables-only', FIELDS,
    [('enum class CfdVariable : std::uint8_t { Pressure, Velocity, MassFlux };',
      'enum class CfdVariable : std::uint8_t { Pressure, Velocity, MassFlux, Extra };'),
     ('inline constexpr std::array<CfdVariable, 3> kCfdVariables{CfdVariable::Pressure, CfdVariable::Velocity,\n'
      '                                                          CfdVariable::MassFlux};',
      'inline constexpr std::array<CfdVariable, 4> kCfdVariables{CfdVariable::Pressure, CfdVariable::Velocity,\n'
      '                                                          CfdVariable::MassFlux, CfdVariable::Extra};')],
    expect_build_failure=True,
    expect_markers=['CfdFields.hpp', 'static assertion failed'])

# ----------------------------------------------------------------- M2
mutation(
    '2-widen-both-tables', FIELDS,
    [('enum class CfdVariable : std::uint8_t { Pressure, Velocity, MassFlux };',
      'enum class CfdVariable : std::uint8_t { Pressure, Velocity, MassFlux, Extra };'),
     ('inline constexpr std::array<CfdVariable, 3> kCfdVariables{CfdVariable::Pressure, CfdVariable::Velocity,\n'
      '                                                          CfdVariable::MassFlux};',
      'inline constexpr std::array<CfdVariable, 4> kCfdVariables{CfdVariable::Pressure, CfdVariable::Velocity,\n'
      '                                                          CfdVariable::MassFlux, CfdVariable::Extra};'),
     ('inline constexpr std::array<CfdFieldDescriptor, 3> kCfdFields{\n'
      '    {{CfdVariable::Pressure, CfdFieldLocation::Cell, 1, dimensions::pressure, "p"},\n'
      '     {CfdVariable::Velocity, CfdFieldLocation::Cell, kVectorComponents.size(), dimensions::velocity, "U"},\n'
      '     {CfdVariable::MassFlux, CfdFieldLocation::Face, 1, dimensions::mass / dimensions::time, "phi"}}};',
      'inline constexpr std::array<CfdFieldDescriptor, 4> kCfdFields{\n'
      '    {{CfdVariable::Pressure, CfdFieldLocation::Cell, 1, dimensions::pressure, "p"},\n'
      '     {CfdVariable::Velocity, CfdFieldLocation::Cell, kVectorComponents.size(), dimensions::velocity, "U"},\n'
      '     {CfdVariable::MassFlux, CfdFieldLocation::Face, 1, dimensions::mass / dimensions::time, "phi"},\n'
      '     {CfdVariable::Extra, CfdFieldLocation::Cell, 1, dimensions::pressure, "x"}}};')],
    expect_build_failure=True,
    expect_markers=['CfdTurbulenceArchitectureTests.cpp', 'ADR-058 refuses a fourth CFD variable'])

# ----------------------------------------------------------------- M3
mutation(
    '3-fourth-treatment-member', BOUNDARY,
    [('struct CfdBoundaryTreatment {\n'
      '    CfdBoundaryPolicy pressure = CfdBoundaryPolicy::ZeroGradient;\n'
      '    CfdBoundaryPolicy velocity = CfdBoundaryPolicy::FixedValue;\n'
      '    CfdBoundaryPolicy massFlux = CfdBoundaryPolicy::FixedValue;',
      'struct CfdBoundaryTreatment {\n'
      '    CfdBoundaryPolicy pressure = CfdBoundaryPolicy::ZeroGradient;\n'
      '    CfdBoundaryPolicy velocity = CfdBoundaryPolicy::FixedValue;\n'
      '    CfdBoundaryPolicy massFlux = CfdBoundaryPolicy::FixedValue;\n'
      '    CfdBoundaryPolicy extra = CfdBoundaryPolicy::ZeroGradient;')],
    expect_build_failure=True,
    expect_markers=['CfdTurbulenceArchitectureTests.cpp'])

# ----------------------------------------------------------------- M4
mutation(
    '4-canonical-text-names-turbulence', CANONICAL,
    [('    line(out, "output.residual_history", toString(definition.output.residualHistory));',
      '    line(out, "output.residual_history", toString(definition.output.residualHistory));\n'
      '    line(out, "turbulence", "none");')],
    expect_build_failure=False,
    expect_markers=['CfdTurbulenceArchitecture_TheCanonicalTextNamesNoTurbulenceQuantity'],
    run_after=True)

# ----------------------------------------------------------------- verdict
print()
print('=' * 72)
for tag, ok in results:
    print('  %-8s %s' % ('CAUGHT' if ok else 'MISSED', tag))
print()
bad = [t for t, ok in results if not ok]
if bad:
    print('FALSIFICATION INCOMPLETE: %d mutation(s) did not behave as expected' % len(bad))
    sys.exit(1)
print('FALSIFICATION PASSED: every mutation was caught by the gate named for it')
sys.exit(0)
