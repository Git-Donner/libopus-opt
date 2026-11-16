#!/usr/bin/env python3
"""Run remaining independent ablations and UBSan, retaining complete exit evidence."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

root = Path('/workspace/spreading-v2-validation')
source = Path('/workspace/libopus-opt')
helper = root / 'run-cutoff-test.py'
report = {
    'purpose': 'AArch64 QEMU correctness validation; no native performance claim',
    'source_sha256': {
        str(source / p): hashlib.sha256((source / p).read_bytes()).hexdigest()
        for p in ('celt/arm/bands_neon_intr.c', 'celt/arm/spreading_cutoffs.h',
                  'celt/bands.c', 'celt/tests/test_spreading_cutoffs.c',
                  'celt/tests/test_spreading_reference.c')
    },
    'results': [{
        'cutoffs': 1, 'vector_finish': 1, 'sanitizer': None, 'exit_code': 0,
        'log': str(root / 'cutoffs-1-finish-1.log'),
        'commands': str(root / 'cutoffs-1-finish-1-commands.txt'),
        'evidence': 'Completed before matrix: PASS, 5,162,040 predicates; 643,200 complete cases; 6,432 protected-page sequences. Subsequent FPCR variable type change preserves the same AArch64 64-bit value and generated instructions.'
    }]
}
assert 'PASS: 5162040 exact cutoff predicates' in (root / 'cutoffs-1-finish-1.log').read_text()
assert '643200 scan/decision/history cases' in (root / 'cutoffs-1-finish-1.log').read_text()

def validate(configuration):
    cutoffs, finish, sanitize = configuration
    tag = f'cutoffs-{cutoffs}-finish-{finish}' + ('-ubsan' if sanitize else '')
    command = ['python3', str(helper), '--cutoffs', str(cutoffs), '--vector-finish', str(finish)]
    if sanitize:
        command += ['--sanitize']
    started = time.monotonic()
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    (root / f'{tag}-driver.log').write_text(completed.stdout)
    return {
        'cutoffs': cutoffs, 'vector_finish': finish,
        'sanitizer': 'undefined' if sanitize else None,
        'exit_code': completed.returncode,
        'driver_command': command,
        'driver_elapsed_seconds': round(time.monotonic() - started, 3),
        'log': str(root / f'{tag}.log'),
        'commands': str(root / f'{tag}-commands.txt'),
        'driver_log': str(root / f'{tag}-driver.log')
    }

with ThreadPoolExecutor(max_workers=4) as executor:
    report['results'] += list(executor.map(validate, ((0, 0, False), (1, 0, False), (0, 1, False), (1, 1, True))))
report['all_status_zero'] = all(result['exit_code'] == 0 for result in report['results'])
(root / 'strict-matrix.json').write_text(json.dumps(report, indent=2) + '\n')
for result in report['results']:
    print(f"cutoffs={result['cutoffs']} vector_finish={result['vector_finish']} sanitizer={result['sanitizer']} exit={result['exit_code']} log={result['log']}")
raise SystemExit(0 if report['all_status_zero'] else 1)
