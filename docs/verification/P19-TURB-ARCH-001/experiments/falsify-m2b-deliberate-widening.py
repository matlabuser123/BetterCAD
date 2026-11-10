"""M2b -- what the count pin is ACTUALLY worth, after M2 disproved the first claim.

M2 widened both frozen tables and the build failed WITHOUT reaching the new count
pin: `-Werror=switch` caught the fourth enumerator at src/cfd/CfdFields.cpp:64 and
src/cfd/CfdInitialField.cpp:297. So the claim "the frozen table was frozen by a
comment" was WRONG, and the milestone must not make it.

This mutation asks the narrower question that is left. An author who DELIBERATELY
adds a fourth variable will satisfy every exhaustive switch -- that is the first
thing the compiler tells them to do. Once they have, is anything left that makes
the widening state itself?

  M2b  widen both tables, add the enumerator to BOTH exhaustive switches, so
       -Werror=switch is satisfied
       EXPECTED: the new count pin is the ONLY thing that fails.

If it is, the pin's value is precise and modest: it does not stop a fourth
variable appearing, it stops one appearing SILENTLY once the switches are fixed,
which is what ADR-058 means by "a visible change to a frozen table". If something
else fires instead, there is nothing left for the pin to do and the evidence says
so.
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

TARGETS = {
    'CfdFields.hpp': ROOT + '/include/bettercad/cfd/CfdFields.hpp',
    'CfdFields.cpp': ROOT + '/src/cfd/CfdFields.cpp',
    'CfdInitialField.cpp': ROOT + '/src/cfd/CfdInitialField.cpp',
}

EDITS = {
    'CfdFields.hpp': [
        ('enum class CfdVariable : std::uint8_t { Pressure, Velocity, MassFlux };',
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
         '     {CfdVariable::Extra, CfdFieldLocation::Cell, 1, dimensions::pressure, "x"}}};'),
    ],
    'CfdFields.cpp': [
        ('    case CfdVariable::MassFlux:\n        return "mass flux";\n    }',
         '    case CfdVariable::MassFlux:\n        return "mass flux";\n'
         '    case CfdVariable::Extra:\n        return "extra";\n    }'),
    ],
    'CfdInitialField.cpp': [
        ('    switch (variable) {\n    case CfdVariable::Pressure:',
         '    switch (variable) {\n    case CfdVariable::Extra:\n        return Result<void>{};\n'
         '    case CfdVariable::Pressure:'),
    ],
}


def sha(p):
    return hashlib.sha256(io.open(p, 'rb').read()).hexdigest()


def rd(p):
    return io.open(p, encoding='utf-8', newline='').read()


def wr(p, t):
    io.open(p, 'w', encoding='utf-8', newline='').write(t)


before = {}
for name, path in TARGETS.items():
    before[name] = sha(path)
    pristine = EXP + '/' + name + '.pristine'
    if not os.path.exists(pristine):
        shutil.copy2(path, pristine)

print('applying M2b to three files')
try:
    for name, path in TARGETS.items():
        text = rd(path)
        for old, new in EDITS[name]:
            assert text.count(old) == 1, '%s: anchor found %d times' % (name, text.count(old))
            text = text.replace(old, new)
        wr(path, text)
        print('  %-22s %s -> %s' % (name, before[name][:12], sha(path)[:12]))

    env = dict(os.environ)
    env['PATH'] = WINLIBS + os.pathsep + env.get('PATH', '')
    env['BETTERCAD_BUILD_ROOT'] = 'C:/bettercad/bc-build'
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = '4'
    r = subprocess.run([CMAKE, '--build', '--preset', 'debug-ext', '--target', 'bettercad_tests'],
                       cwd=ROOT, env=env, capture_output=True, text=True)
    out = (r.stdout or '') + (r.stderr or '')
    wr(EXP + '/falsify-2b-deliberate-widening.log', out)

    errors = [ln for ln in out.splitlines() if 'error:' in ln]
    print()
    print('  build exit %d, %d error line(s)' % (r.returncode, len(errors)))
    for ln in errors[:10]:
        print('     ' + ln.strip()[:150])

    switch_errors = [ln for ln in errors if 'Werror=switch' in ln]
    pin_fired = 'ADR-058 refuses a fourth CFD variable' in out
    in_my_test = 'CfdTurbulenceArchitectureTests.cpp' in out
    print()
    print('  -Werror=switch errors        %d  (0 means the switches were satisfied)' % len(switch_errors))
    print('  the new count pin fired      %s' % pin_fired)
    print('  the failure is in my test    %s' % in_my_test)
finally:
    ok = True
    for name, path in TARGETS.items():
        shutil.copy2(EXP + '/' + name + '.pristine', path)
        os.utime(path, None)  # see the note at the top: copy2 keeps the mtime
        if sha(path) != before[name]:
            ok = False
            print('  *** %s NOT RESTORED ***' % name)
    print()
    print('  restored: %s' % ('all three byte-identical' if ok else 'MISMATCH -- STOP'))
    if not ok:
        sys.exit(2)

print()
if len(switch_errors) == 0 and pin_fired and in_my_test:
    print('M2b RESULT: the count pin is the ONLY thing that fails once the switches are')
    print('satisfied. Its value is narrow and real: a deliberate widening must come here')
    print('and state itself.')
    sys.exit(0)
print('M2b RESULT: the count pin is NOT what catches a deliberate widening.')
sys.exit(1)
