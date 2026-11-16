#!/usr/bin/env python3
"""Build exact-rounding test objects and run the requested cutoff/finish ablation."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--cutoffs', type=int, choices=(0, 1), default=1)
parser.add_argument('--vector-finish', type=int, choices=(0, 1), default=1)
parser.add_argument('--build', type=Path, default=Path('/workspace/spreading-neon-validation/arm-build'))
parser.add_argument('--source', type=Path, default=Path('/workspace/libopus-opt'))
parser.add_argument('--output', type=Path, default=Path('/workspace/spreading-v2-validation'))
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
tag = f'cutoffs-{args.cutoffs}-finish-{args.vector_finish}' + ('-ubsan' if args.sanitize else '')
commands = json.loads((args.build / 'compile_commands.json').read_text())
source_command = next(c['command'] for c in commands if c['file'] == str(args.source / 'celt/arm/bands_neon_intr.c'))
common = shlex.split(source_command)
# Keep source build definitions/includes and flags, replace its original -o/-c.
common = common[:common.index('-o')]
common += ['-ffp-contract=off', '-frounding-math',
           f'-DOPUS_SPREAD_NEON_CUTOFFS={args.cutoffs}',
           f'-DOPUS_SPREAD_NEON_VECTOR_FINISH={args.vector_finish}']
if args.sanitize:
    common += ['-fsanitize=undefined', '-fno-sanitize-recover=all']
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = '/workspace/pvq-neon-validation/toolchain/usr/lib/x86_64-linux-gnu' + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
command_log = args.output / f'{tag}-commands.txt'
log = args.output / f'{tag}.log'
all_commands = []

def run(command, output):
    all_commands.append(shlex.join(command))
    command_log.write_text('\n'.join(all_commands) + '\n')
    subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT, check=True)

with log.open('w') as output:
    objects = []
    for relative in ('celt/bands.c', 'celt/arm/bands_neon_intr.c', 'celt/tests/test_spreading_reference.c', 'celt/tests/test_spreading_cutoffs.c'):
        obj = args.output / f'{tag}-{Path(relative).stem}.o'
        run(common + ['-c', str(args.source / relative), '-o', str(obj)], output)
        objects.append(str(obj))
    executable = args.output / f'test_spreading_{tag}'
    run(common + objects + [str(args.build / 'libopus.a'), '-lm', '-o', str(executable)], output)
    qemu = '/workspace/pvq-neon-validation/toolchain/usr/bin/qemu-aarch64'
    command = [qemu, '-L', '/workspace/pvq-neon-validation/toolchain/usr/aarch64-linux-gnu', str(executable)]
    started = time.monotonic()
    run(command, output)
    elapsed = time.monotonic() - started
    output.write(f'QEMU correctness-test elapsed seconds: {elapsed:.3f} (not a native performance measurement)\n')
print(log.read_text(), end='')
