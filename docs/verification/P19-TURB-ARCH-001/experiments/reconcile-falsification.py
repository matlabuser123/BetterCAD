"""Does reconcile.py actually bite? Fifteen sabotages, fifteen runs.

A reconciliation that passes everything may be passing because it checks
nothing. `P19-STEADY-001` recorded a check that reported PASS off one parsed
path instead of eight, and this milestone's own first reconcile.py reported a
1306-byte file as 1320 bytes -- a measurement defect in the instrument. So the
instrument is tested the only way an instrument can be: by breaking one thing
at a time and requiring it to notice.

Nothing in the real evidence directory is modified. Everything happens under a
scratch copy, and reconcile.py is re-pointed at it by text substitution. Each
sabotage is applied to the copy, the script is run, and the file is restored
from the real evidence before the next one.

Run:  python -I reconcile-falsification.py
Exit: 0 only if the control passes AND every sabotage is caught.
"""

import io
import os
import re
import shutil
import subprocess
import sys

REAL = r'C:/Users/Hasib/Desktop/CAD/docs/verification/P19-TURB-ARCH-001'
PREV = r'C:/Users/Hasib/Desktop/CAD/docs/verification/P19-TRANSIENT-001'
REPO = r'C:/Users/Hasib/Desktop/CAD'
SCRATCH = (r'C:/Users/Hasib/AppData/Local/Temp/claude/8b17a8f29dfea518/'
           r'527dbd1e-d40d-42f7-8715-a186b5519185/scratchpad/falsify-turb')
EVID = SCRATCH + '/P19-TURB-ARCH-001'
QUAL = EVID + '/qualification'
PY = sys.executable

PRESETS = ['debug-ext', 'release-ext', 'debug-shared-ext']
QFILES = (['run-qualification.out', 'qualification-times.txt',
           'run-qualification.cmd', 'launch.cmd', 'preflight-space.cmd',
           'preflight-space.ps1', 'qualify.cmd', 'verify-harness.cmd']
          + ['build-%s.log' % p for p in PRESETS]
          + ['rebuild-%s.log' % p for p in PRESETS]
          + ['clean-%s.log' % p for p in PRESETS]
          + ['configure-%s.log' % p for p in PRESETS]
          + ['ctest-%s.log' % p for p in PRESETS]
          + ['ctest-repeat-%s.log' % p for p in ['release-ext', 'debug-ext']])


def rd(p):
    with io.open(p, encoding='utf-8', errors='replace', newline='') as f:
        return f.read()


def wr(p, s):
    with io.open(p, 'w', encoding='utf-8', errors='replace', newline='') as f:
        f.write(s)


def build_scratch():
    if os.path.isdir(SCRATCH):
        shutil.rmtree(SCRATCH)
    os.makedirs(QUAL)
    os.makedirs(EVID + '/experiments')
    os.makedirs(SCRATCH + '/P19-TRANSIENT-001/qualification')
    shutil.copy2(REAL + '/FREEZE.md', EVID + '/FREEZE.md')
    # reconcile.py reads the qualification commit out of README.md
    shutil.copy2(REAL + '/README.md', EVID + '/README.md')
    for f in QFILES:
        shutil.copy2(REAL + '/qualification/' + f, QUAL + '/' + f)
    # the carried-forward launcher the identity check compares against
    shutil.copy2(PREV + '/qualification/run-qualification.cmd',
                 SCRATCH + '/P19-TRANSIENT-001/qualification/'
                           'run-qualification.cmd')
    src = rd(REAL + '/experiments/reconcile.py')
    old = ("HERE = os.path.dirname(os.path.abspath(__file__))\n"
           "EVID = os.path.dirname(HERE)\n"
           "QUAL = os.path.join(EVID, 'qualification')\n"
           "REPO = os.path.abspath(os.path.join(EVID, '..', '..', '..'))")
    assert src.count(old) == 1, 'reconcile.py path block not found verbatim'
    new = ("HERE = r'%s/experiments'\nEVID = r'%s'\nQUAL = r'%s'\n"
           "REPO = r'%s'" % (EVID, EVID, QUAL, REPO))
    wr(EVID + '/experiments/reconcile.py', src.replace(old, new))


TOP = ('FREEZE.md', 'README.md')


def restore(rel):
    """Put one file back, byte for byte, from the real evidence."""
    if rel in TOP:
        shutil.copy2(REAL + '/' + rel, EVID + '/' + rel)
    else:
        shutil.copy2(REAL + '/qualification/' + rel, QUAL + '/' + rel)


def run():
    r = subprocess.run([PY, '-I', EVID + '/experiments/reconcile.py'],
                       capture_output=True, text=True)
    fails = [ln.strip()[6:].split('   ')[0]
             for ln in r.stdout.splitlines() if ln.strip().startswith('FAIL')]
    crashed = r.returncode not in (0, 1)
    return r.returncode, fails, crashed, r.stdout + r.stderr


# ------------------------------------------------------------------ sabotages
def sab_count(path):
    def go(s):
        return s.replace('100% tests passed out of 6995',
                         '100% tests passed out of 6994', 1)
    return path, go


def sab_delete_passed_line(s):
    """Remove one Passed report: the suite then ran 6994 tests, not 6995."""
    m = re.search(r'^\s*\d+/6995 Test\s+#\d+: unit\.[^\n]*Passed[^\n]*\n', s,
                  re.M)
    assert m, 'no Passed line to delete'
    return s[:m.start()] + s[m.end():]


def sab_stage_count(s):
    return s.replace('0 stage(s) failed', '1 stage(s) failed', 1)


def sab_postrun_identity(s):
    """The tree moved during the run and the harness read it afterwards."""
    i = s.index('after the last test run:')
    head, tail = s[:i], s[i:]
    return head + tail.replace('c33a7b6fdbe7cb1b4deb66727152525e85dd4a48',
                               'c33a7b6fdbe7cb1b4deb66727152525e85dd4a4f', 1)


def sab_warning(s):
    return s.replace('\n', '\nsrc/cfd/CfdTurbulence.cpp:1:1: '
                           'warning: unused variable [-Wunused-variable]\n', 1)


def sab_group_count(s):
    return s.replace('architecture.turbulence-ownership.valid',
                     'architecture.someoneelses-ownership.valid', 1)


def sab_stage_exit(s):
    return s.replace('release-ext ctest exit 0', 'release-ext ctest exit 1', 1)


def sab_verdict(s):
    return s.replace('Qualification passed: every stage exited 0.',
                     'Qualification FAILED: 1 stage(s) failed.', 1)


def sab_freeze_figure(s):
    return s.replace('full suite, per preset   6995',
                     'full suite, per preset   6996', 1)


def sab_repeat_execution(s):
    m = re.search(r'^\s*Test\s+#\d+: [^\n]*Passed[^\n]*\n', s, re.M)
    assert m, 'no repeat execution to delete'
    return s[:m.start()] + s[m.end():]


def sab_harness_byte(s):
    return s + '\nrem one appended line\n'


def sab_launcher_executable(s):
    i = s.index('@echo off')
    return s[:i] + s[i:].replace('exit /b', 'exit /b ', 1)


def sab_freeze_identity(s):
    """The freeze claims a `tests` tree the disk does not hold."""
    return s.replace('bf65a34a2027d1c3ceff8a3e8b3216c538a9165d',
                     'bf65a34a2027d1c3ceff8a3e8b3216c538a9165e')


def sab_wrong_commit(s):
    """The evidence names a commit whose tree is not the frozen tree."""
    return re.sub(r'(qualified and committed as\s+)[0-9a-f]{40}',
                  r'\g<1>' + '9d89f4d5002e0724323cc7340ea30c3cb7dc8419', s,
                  count=1)


def sab_rebuild_not_noop(s):
    return s.rstrip('\n') + ('\n[2/8] Building CXX object '
                             'CMakeFiles/bettercad_cfd.dir/CfdSteady.cpp.obj\n')


SABOTAGES = [
    ('a full-suite log claims one test fewer than the freeze',
     'ctest-debug-ext.log', sab_count('x')[1]),
    ('one Passed report is deleted from a full-suite log',
     'ctest-release-ext.log', sab_delete_passed_line),
    ('the harness\'s failed-stage count is 1, not 0',
     'qualification-times.txt', sab_stage_count),
    ('the post-run identity shows src moved DURING the run',
     'qualification-times.txt', sab_postrun_identity),
    ('a build log carries one compiler warning under -Werror',
     'build-release-ext.log', sab_warning),
    ('one ownership test is renamed, so the group ran 11 not 12',
     'ctest-debug-shared-ext.log', sab_group_count),
    ('one stage exited 1 while the failed-stage count still said 0',
     'qualification-times.txt', sab_stage_exit),
    ('run-qualification.out states a FAILURE',
     'run-qualification.out', sab_verdict),
    ('FREEZE.md\'s full-suite figure no longer adds up',
     'FREEZE.md', sab_freeze_figure),
    ('one of the 17715 repeat executions never ran',
     'ctest-repeat-release-ext.log', sab_repeat_execution),
    ('a carried-forward harness file gained a line',
     'qualify.cmd', sab_harness_byte),
    ('the launcher\'s EXECUTABLE half changed, not its header',
     'run-qualification.cmd', sab_launcher_executable),
    ('FREEZE.md claims a tests tree the disk does not hold',
     'FREEZE.md', sab_freeze_identity),
    ('the no-op rebuild actually compiled something',
     'rebuild-debug-ext.log', sab_rebuild_not_noop),
    ('the evidence names the WRONG commit as the qualified one',
     'README.md', sab_wrong_commit),
]

# --------------------------------------------------------------------- control
print('== control: the untouched copy must PASS ==')
build_scratch()
rc, fails, crashed, out = run()
print('   exit %d, %d FAIL line(s), crashed=%s' % (rc, len(fails), crashed))
control_ok = (rc == 0 and not fails and not crashed)
if control_ok:
    print('   OK -- the scratch copy reproduces the real run\'s PASS, so a '
          'failure below is the sabotage and not the copy')
else:
    print('   BROKEN -- the copy does not even pass, so nothing below means '
          'anything')
    print(out[-3000:])

# -------------------------------------------------------------------- sabotage
results = []
print('\n== fifteen sabotages, each one restored before the next ==')
for i, (label, rel, fn) in enumerate(SABOTAGES, 1):
    target = (EVID + '/' + rel) if rel in TOP else (QUAL + '/' + rel)
    before = rd(target)
    after = fn(before)
    assert after != before, '%s: sabotage %d changed nothing' % (rel, i)
    wr(target, after)
    rc, fails, crashed, out = run()
    restore(rel)
    assert rd(target) == before, '%s: restore did not restore' % rel
    caught = (rc == 1 and len(fails) >= 1 and not crashed)
    results.append((caught, label, rc, fails, crashed))
    print('  %-6s %2d. %s' % ('CAUGHT' if caught else 'MISSED', i, label))
    if caught:
        print('           -> %s' % fails[0])
        if len(fails) > 1:
            print('           -> and %d more' % (len(fails) - 1))
    else:
        print('           exit %d, %d FAIL line(s), crashed=%s'
              % (rc, len(fails), crashed))
        print('           %s' % out[-700:].replace('\n', ' | '))

# ---------------------------------------------------------------------- verdict
missed = [r for r in results if not r[0]]
print('\n' + '=' * 70)
print('control: %s' % ('PASS' if control_ok else 'BROKEN'))
print('sabotages: %d of %d caught' % (len(results) - len(missed), len(results)))
if missed:
    print('\nMISSED -- reconcile.py cannot see these:')
    for _, label, rc, fails, crashed in missed:
        print('   - %s (exit %d, %d FAIL, crashed=%s)'
              % (label, rc, len(fails), crashed))
shutil.rmtree(SCRATCH, ignore_errors=True)
if not control_ok or missed:
    sys.exit(1)
print('\nreconcile.py BITES. It passes the real evidence and fails every one '
      'of the fifteen sabotages.')
sys.exit(0)
