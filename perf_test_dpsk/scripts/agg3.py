#!/usr/bin/env python3
# agg3.py <outroot> <N> <lib1> <lib2> [lib3] — 任意 N 个库，逐 case/metric 求平均，输出各库均值和两两比值
import csv, glob, os, statistics, sys

outroot = sys.argv[1]; N=int(sys.argv[2]); libs=sys.argv[3:]
METRIC_COL = {"enc.min":"enc_min_us","enc.p50":"enc_p50_us","enc.p95":"enc_p95_us",
              "dec.min":"dec_min_us","dec.p50":"dec_p50_us","dec.p95":"dec_p95_us"}

def load(run_dir):
    fs=glob.glob(os.path.join(run_dir,"csv","*.csv"))
    if not fs: return {}
    d={}
    with open(fs[0]) as fh:
        rd=csv.DictReader(fh)
        for row in rd:
            nm=row.get("name","")
            if not nm: continue
            d[nm]={k: (float(row[c]) if row.get(c) not in (None,"") else None) for k,c in METRIC_COL.items()}
    return d

# per-lib: case -> metric -> list of vals across runs
data={lb:{} for lb in libs}
for i in range(1,N+1):
    for lb in libs:
        rd=load(os.path.join(outroot,lb,"r%d"%i))
        for nm,mm in rd.items():
            for m,v in mm.items():
                if v is None: continue
                data[lb].setdefault(nm,{}).setdefault(m,[]).append(v)

def med(a): return statistics.mean(a) if a else None
METRIC_ORDER=["enc.min","enc.p50","enc.p95"]

cases=sorted(set().union(*(data[lb].keys() for lb in libs)))
print("N=%d 库: %s | 指标=各case各指标N次均值 (us)"%(N,", ".join(libs)))
hdr="%-26s %-9s"%( "case","metric" )
for lb in libs: hdr+=" %12s"%lb
hdr+=" %10s"%("%s/%s"%(libs[1],libs[0]))  # ratio lib2/lib1
print(hdr); print("-"*len(hdr))
for nm in cases:
    for m in METRIC_ORDER:
        vals=[med(data[lb][nm].get(m)) for lb in libs]
        if any(v is None for v in vals): continue
        line="%-26s %-9s"%(nm,m)
        for v in vals: line+=" %12.2f"%v
        ratio=vals[1]/vals[0]
        line+=" %10.4f"%ratio
        print(line)
