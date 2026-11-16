#!/usr/bin/env python3
"""Isolated Autotools validation for default cutoff+finish spreading NEON v2."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

repo = Path('/workspace/libopus-opt')
work = Path('/workspace/spreading-v2-validation')
source = work / 'autotools-source'
build = work / 'autotools-build'
previous_source = Path('/workspace/spreading-neon-validation/autotools-source')
host_tools = Path('/workspace/pvq-sve256-validation/host-tools')
toolchain = Path('/workspace/pvq-neon-validation/toolchain')
cc = toolchain / 'usr/bin/aarch64-linux-gnu-gcc'

# The v1 source copy already contains the frozen reference and existing tests.
# Overlay only the six production/test/build files changed for v2.
overlay = [
    'celt/arm/bands_neon_intr.c', 'celt/arm/spreading_cutoffs.h',
    'celt_headers.mk', 'CMakeLists.txt', 'Makefile.am',
    'celt/tests/test_spreading_cutoffs.c',
]
if source.exists() or build.exists():
    raise SystemExit('Refusing to replace existing isolated validation directories')
shutil.copytree(previous_source, source, symlinks=True)
for name in overlay:
    shutil.copy2(repo / name, source / name)
(work / 'autotools-overlay-manifest.json').write_text(json.dumps({
    'base_source': str(previous_source),
    'overlay': {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
                for name in overlay},
}, indent=2) + '\n')
build.mkdir()

env = dict(os.environ)
env['PATH'] = str(host_tools / 'usr/bin') + ':' + env.get('PATH', '')
env['autom4te_perllibdir'] = str(host_tools / 'usr/share/autoconf')
for var, name in [('AUTOCONF', 'autoconf'), ('AUTOHEADER', 'autoheader'),
                  ('AUTOM4TE', 'autom4te'), ('ACLOCAL', 'aclocal'),
                  ('AUTOMAKE', 'automake'), ('LIBTOOLIZE', 'libtoolize'),
                  ('M4', 'm4')]:
    env[var] = str(host_tools / 'usr/bin' / name)
env['LD_LIBRARY_PATH'] = str(toolchain / 'usr/lib/x86_64-linux-gnu')
env['CC'] = str(cc) + ' --sysroot=' + str(toolchain)
env['AR'] = str(toolchain / 'usr/bin/aarch64-linux-gnu-ar')
env['RANLIB'] = str(toolchain / 'usr/bin/aarch64-linux-gnu-ranlib')
env['CFLAGS'] = '-O3'
commands = []
statuses = {}

def run(name, argv, cwd):
    commands.append({'name': name, 'argv': list(map(str, argv)), 'cwd': str(cwd)})
    (work / 'autotools-commands.json').write_text(json.dumps({
        'environment': {key: env[key] for key in [
            'PATH', 'autom4te_perllibdir', 'AUTOCONF', 'AUTOHEADER',
            'AUTOM4TE', 'ACLOCAL', 'AUTOMAKE', 'LIBTOOLIZE', 'M4',
            'LD_LIBRARY_PATH', 'CC', 'AR', 'RANLIB', 'CFLAGS']},
        'commands': commands,
    }, indent=2) + '\n')
    print('START:', name, flush=True)
    with (work / (name + '.log')).open('w') as log:
        result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
    statuses[name] = result.returncode
    (work / 'autotools-statuses.json').write_text(json.dumps(statuses, indent=2) + '\n')
    print('EXIT:', name, result.returncode, flush=True)
    if result.returncode:
        print((work / (name + '.log')).read_text()[-6000:], flush=True)
        sys.exit(result.returncode)

run('autotools-compiler-version', [cc, '--version'], build)
run('autotools-autoreconf', [host_tools / 'usr/bin/autoreconf', '-isf'], source)
run('autotools-configure', [source / 'configure', '--host=aarch64-linux-gnu',
    '--disable-sve', '--disable-shared', '--enable-static', '--disable-fixed-point'], build)
run('autotools-build', ['make', '-j8', 'libopus.la'], build)

archive = build / '.libs/libopus.a'
(work / 'autotools-archive-sha256.txt').write_text(
    hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + str(archive) + '\n')

test_flags = ['--sysroot=' + str(toolchain), '-O3', '-std=gnu99', '-Wall',
    '-Wextra', '-DHAVE_CONFIG_H', '-I' + str(build),
    '-I' + str(source / 'include'), '-I' + str(source / 'celt')]
for name in ['test_unit_spreading_neon', 'test_encoder_spreading_neon',
             'test_spreading_cutoffs']:
    flags = list(test_flags)
    if name == 'test_spreading_cutoffs':
        flags += ['-frounding-math', '-ffp-contract=off']
    argv = [cc, *flags, source / 'celt/tests' / (name + '.c'),
        source / 'celt/tests/test_spreading_reference.c', archive]
    if name == 'test_encoder_spreading_neon':
        argv.append('-Wl,--wrap=spreading_statistics_neon')
    argv.extend(['-lm', '-o', build / name])
    run('autotools-compile-' + name, argv, build)
    run('autotools-' + name, [toolchain / 'usr/bin/qemu-aarch64', '-cpu',
        'max,sve=off', '-L', toolchain / 'usr/aarch64-linux-gnu', build / name], build)

print('PASS: isolated Autotools static library and three standalone tests', flush=True)
