from pathlib import Path
import subprocess,json,shlex
r=Path('/workspace/libopus-opt');w=Path('/workspace/spreading-v2-validation');b=Path('/workspace/spreading-neon-validation/arm-build');t=Path('/workspace/pvq-neon-validation/toolchain')
c=shlex.split(next(x['command'] for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].endswith('/celt/arm/bands_neon_intr.c')));c=c[:c.index('-o')]
records=[]
for cuts,finish in [(0,0),(1,0),(0,1),(1,1)]:
 label=f'{cuts}{finish}';obj=w/f'kernel-{label}.o';exe=w/f'encoder-{label}'
 flags=[f'-DOPUS_SPREAD_NEON_CUTOFFS={cuts}',f'-DOPUS_SPREAD_NEON_VECTOR_FINISH={finish}']
 cc=c+flags+['-c',str(r/'celt/arm/bands_neon_intr.c'),'-o',str(obj)];subprocess.run(cc,cwd=b,check=True)
 link=c+flags+[str(r/'celt/tests/test_encoder_spreading_neon.c'),str(r/'celt/tests/test_spreading_reference.c'),str(obj),str(b/'libopus.a'),'-Wl,--wrap=spreading_statistics_neon','-lm','-o',str(exe)];subprocess.run(link,cwd=b,check=True)
 run=[str(t/'usr/bin/qemu-aarch64'),'-cpu','max,sve=off','-L',str(t/'usr/aarch64-linux-gnu'),str(exe)]
 with (w/f'encoder-{label}.log').open('w') as f:res=subprocess.run(run,stdout=f,stderr=subprocess.STDOUT)
 records.append({'variant':label,'compile':cc,'link':link,'run':run,'exit_code':res.returncode})
 print('encoder',label,'exit',res.returncode,flush=True)
 if res.returncode:print((w/f'encoder-{label}.log').read_text());break
(w/'encoder-matrix.json').write_text(json.dumps(records,indent=2)+'\n')
assert len(records)==4 and all(x['exit_code']==0 for x in records)
