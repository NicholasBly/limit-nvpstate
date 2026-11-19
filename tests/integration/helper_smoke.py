"""Smoke test of limit-nvpstate-updater.exe's recovery, outside the app:
an update interrupted mid-swap (written to disk by hand) must be undone."""
import os, shutil, subprocess, sys
H = '/home/claude/harness'
helper = sys.argv[1]
d = '/home/claude/runs/helper_smoke'
shutil.rmtree(d, ignore_errors=True)
os.makedirs(d + '/limit-nvpstate-update/backup')
os.makedirs(d + '/limit-nvpstate-update/download/unpacked/limit-nvpstate')
def u16(path, text): open(path, 'wb').write(('﻿' + text).encode('utf-16-le'))
def put(path, text): open(path, 'w').write(text)
shutil.copy(f'{H}/bin/exit3.exe', d + '/limit-nvpstate.exe')       # the "app" it restarts at the end
shutil.copy(helper, d + '/limit-nvpstate-updater.exe')
W = d + '/limit-nvpstate-update'
u16(W + '/owner.txt', 'limit-nvpstate updater\n')
u16(W + '/journal.txt', 'limit-nvpstate update journal 2\nP\tdownload\\unpacked\\limit-nvpstate\n'
    'R\taaa.txt\nA\tnewfile.txt\nR\tQt5Core.dll\nR\tzzz.txt\n')
u16(W + '/phase.txt', 'swapping\n')
put(d + '/aaa.txt', 'new aaa\n');      put(W + '/backup/aaa.txt', 'old aaa\n')        # swapped
put(d + '/newfile.txt', 'new file\n')                                               # added (source gone)
put(d + '/Qt5Core.dll', 'new core\n'); put(W + '/backup/Qt5Core.dll', 'old core\n')  # swapped
put(d + '/zzz.txt', 'old zzz\n');      put(W + '/download/unpacked/limit-nvpstate/zzz.txt', 'new zzz\n')  # not reached
env = dict(os.environ, WINEDEBUG='-all', WINEPREFIX='/home/claude/wineprefix', DISPLAY=':99')
env.pop('TMPDIR', None)
rc = subprocess.run(['wine', d + '/limit-nvpstate-updater.exe', 'recover'], env=env, timeout=60).returncode
def text(p):
    return open(p, 'rb').read().decode('utf-16' if p.endswith(('phase.txt', 'result.txt')) else 'utf-8') if os.path.exists(p) else None
checks = [('exit code', 0, rc),
          ('aaa.txt restored', 'old aaa\n', text(d + '/aaa.txt')),
          ('Qt5Core.dll restored', 'old core\n', text(d + '/Qt5Core.dll')),
          ('zzz.txt untouched', 'old zzz\n', text(d + '/zzz.txt')),
          ('newfile.txt removed', None, text(d + '/newfile.txt')),
          ('phase', 'rolled-back\n', text(W + '/phase.txt')),
          ('result says interrupted', True, 'interrupted' in (text(W + '/result.txt') or ''))]
bad = 0
for label, want, got in checks:
    ok = want == got
    bad += not ok
    print(('ok  ' if ok else 'XX  ') + f'{label}: expected {want!r}, got {got!r}')
sys.exit(1 if bad else 0)
