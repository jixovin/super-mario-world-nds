#!/usr/bin/env python3
"""Super Mario World DS: make the game data from your own ROM, and build.

Works the same on Windows, macOS and Linux (Python 3.8+, nothing else to
install for the data step):

  python build.py data   Make smw_assets.dat from your ROM, for the ready-made
                         smw-nds.nds from the Releases page. Copy it to
                         _nds/smw-nds/ on your microSD card.
  python build.py        Build nds/smw-nds.nds yourself (needs devkitPro with
                         NDS Development). The game data is built in.
  python build.py clean  Delete build output.

On Windows use "py build.py ..." instead, or double-click build.py (= build)
or drag your ROM onto it (= build with that ROM).

The ROM is your own Super Mario World (USA), .sfc or .smc, put in the roms
folder (any file name) or given as the last argument. Never share the .nds you
build or smw_assets.dat: they contain data from your ROM.
"""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
NDS_DIR = os.path.join(ROOT, 'nds')
DATA_BIN = os.path.join(NDS_DIR, 'data', 'smw_assets.bin')
DATA_VER = os.path.join(NDS_DIR, 'data', 'assets_version.txt')
# Bump when the asset tool changes, so old game data is made again.
WANT_VER = '2'


class Failure(Exception):
    pass


def find_rom(args):
    for a in args:
        if os.path.isfile(a):
            return os.path.abspath(a)
    roms = os.path.join(ROOT, 'roms')
    os.makedirs(roms, exist_ok=True)
    found = sorted(glob.glob(os.path.join(roms, '*.sfc')) + glob.glob(os.path.join(roms, '*.smc')))
    if not found:
        raise Failure('No ROM found. Copy your Super Mario World (USA) ROM (.sfc or .smc)\n'
                      'into this folder, then run this again:\n  ' + roms)
    return found[0]


def make_data(rom, out_dir):
    """Runs snesrev's asset tool; returns the path of smw_assets.dat."""
    print('Using ROM: ' + rom)
    print('Making the game data...')
    out = os.path.join(out_dir, 'smw_assets.dat')
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'assets', 'restool.py'),
                        '-r', rom, '--no-include-rom'], cwd=out_dir,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        tail = r.stdout.strip().splitlines()
        msg = next((l for l in reversed(tail) if 'not supported' in l or 'Exception' in l), '')
        if 'not supported' in r.stdout:
            raise Failure('That ROM is not Super Mario World (USA). Other versions and ROM\n'
                          'hacks are not supported.')
        raise Failure('The asset tool failed:\n' + '\n'.join(tail[-8:]) + ('\n' + msg if msg else ''))
    return out


def find_devkitpro():
    candidates = [os.environ.get('DEVKITPRO', '')]
    if os.name == 'nt':
        candidates += ['C:\\devkitPro', 'C:/devkitPro']
    candidates += ['/opt/devkitpro', os.path.expanduser('~/devkitpro')]
    for c in candidates:
        # On Windows DEVKITPRO may be set as an MSYS path like /opt/devkitpro.
        if c and os.path.isdir(os.path.join(c, 'devkitARM')):
            return os.path.abspath(c)
    raise Failure('devkitPro was not found. Install it with the "NDS Development" option\n'
                  '(Windows: https://github.com/devkitPro/installer/releases, to C:\\devkitPro;\n'
                  'macOS/Linux: https://devkitpro.org/wiki/Getting_Started, then\n'
                  '"sudo dkp-pacman -S nds-dev"). Or use "build.py data" with the\n'
                  'ready-made .nds instead.')


def run_make(dkp, target=None):
    args = ['make'] + ([target] if target else [])
    if os.name == 'nt':
        bash = os.path.join(dkp, 'msys2', 'usr', 'bin', 'bash.exe')
        if not os.path.exists(bash):
            raise Failure('devkitPro is missing its MSYS2 shell: ' + bash)
        nds = NDS_DIR.replace('\\', '/')
        cmd = 'cd "$(cygpath -u \'%s\')" && %s' % (nds, ' '.join(args))
        r = subprocess.run([bash, '-lc', cmd])
    else:
        env = dict(os.environ, DEVKITPRO=dkp, DEVKITARM=os.path.join(dkp, 'devkitARM'))
        env['PATH'] = os.pathsep.join([os.path.join(dkp, 'tools', 'bin'),
                                       os.path.join(dkp, 'devkitARM', 'bin'), env.get('PATH', '')])
        r = subprocess.run(args, cwd=NDS_DIR, env=env)
    if r.returncode != 0:
        raise Failure('The build failed (see the messages above).')


def cmd_data(args):
    out = make_data(find_rom(args), ROOT)
    print('\nDONE: ' + out)
    print('Copy it to the folder _nds/smw-nds/ on your microSD card.')
    print('Keep it to yourself: it is made from your ROM.')


def cmd_build(args):
    dkp = find_devkitpro()
    have = ''
    if os.path.exists(DATA_BIN) and os.path.exists(DATA_VER):
        have = open(DATA_VER).read().strip()
    if have != WANT_VER:
        tmp = os.path.join(NDS_DIR, 'data')
        made = make_data(find_rom(args), tmp)
        shutil.move(made, DATA_BIN)
        with open(DATA_VER, 'w') as f:
            f.write(WANT_VER + '\n')
    print('Building...')
    run_make(dkp)
    print('\nBUILD OK: ' + os.path.join(NDS_DIR, 'smw-nds.nds'))
    print('Copy it to your microSD card. Don\'t share it: it contains your game data.')


def main():
    args = sys.argv[1:]
    cmd = args[0] if args and args[0] in ('data', 'build', 'clean') else 'build'
    if args and args[0] == cmd:
        args = args[1:]
    if sys.version_info < (3, 8):
        print('Python 3.8 or newer is needed.')
        return 1
    try:
        if cmd == 'data':
            cmd_data(args)
        elif cmd == 'clean':
            run_make(find_devkitpro(), 'clean')
        else:
            cmd_build(args)
        return 0
    except Failure as e:
        print('\nFAILED: ' + str(e))
        return 1


if __name__ == '__main__':
    code = main()
    # Double-clicked on Windows: keep the window open to read the result.
    if os.name == 'nt' and not os.environ.get('PROMPT') and sys.stdin.isatty():
        input('\nPress Enter to close...')
    sys.exit(code)
