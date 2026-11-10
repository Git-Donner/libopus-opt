#!/usr/bin/env python3
# aggreg_metric.py — 配对多指标分析（min / p50 / p95）
# 对每对 (src,var)，每用例每指标 ratio = src_metric / var_metric；
# 跨 N 对取中位数(鲁棒)，分别报告 enc / dec / rt 三个阶段的 min/p50/p95。
import csv, glob, os, statistics, sys

VTAG = sys.argv[1] if len(sys.argv)>1 else "v1"
RUNDIR = "/tmp/pe/" + VTAG

# 按 CSV 表头列名(而非写死索引)解析，免疫加列(如 chunk_frames)
STAGE_COLS = {
  "enc": {"min":"enc_min_us","p50":"enc_p50_us","p95":"enc_p95_us"},
  "dec": {"min":"dec_min_us","p50":"dec_p50_us","p95":"dec_p95_us"},
  "rt":  {"min":"rt_min_us","p50":"rt_p50_us","p95":"rt_p95_us"},
}
METRIC = ("min","p50","p95")

def load(repdir):
    f = glob.glob(os.path.join(repdir,"csv","*.csv"))
    if not f: return {}
    d={}
    with open(f[0]) as fh:
        rd=csv.DictReader(fh)          # 按表头列名
        for row in rd:
            nm=row.get("name","")
            if not nm: continue
            entry={}
            ok=True
            for stage,cols in STAGE_COLS.items():
                me={}
                for m,cn in cols.items():
                    try: me[m]=float(row[cn])
                    except (ValueError,TypeError,KeyError): ok=False; me[m]=None
                entry[stage]=me
            if ok:
                d[nm]=entry
    return d

reps=sorted([int(x.split("_r")[1]) for x in os.listdir(RUNDIR) if x.startswith("src_r")])
# collect per case per metric lists of ratios
from collections import defaultdict
ratios=defaultdict(lambda: defaultdict(list))  # case -> ('enc','min') -> [ratios]

for i in reps:
    s=load(os.path.join(RUNDIR,"src_r%d"%i)); v=load(os.path.join(RUNDIR,"var_r%d"%i))
    for nm in s:
        if nm not in v: continue
        for stage in ("enc","dec","rt"):
            for m in ("min","p50","p95"):
                sv=s[nm][stage][m]; vv=v[nm][stage][m]
                if vv>0 and sv>0:
                    ratios[nm][(stage,m)].append(sv/vv)

def fmt(x):
    return "%.3f" % x if x else "  -  "

print("VTAG=%s pairs=%d" % (VTAG,len(reps)))
print("="*120)
hdr="%-30s" % "case"
for stage in ("enc","dec","rt"):
    for m in ("min","p50","p95"):
        hdr += " | %s.%s" % (stage.ljust(3),m)
print(hdr)
print("-"*120)
for nm in ratios:
    line="%-30s" % nm
    for stage in ("enc","dec","rt"):
        for m in ("min","p50","p95"):
            r=ratios[nm].get((stage,m),[])
            line += " | %6s" % fmt(statistics.median(r) if r else 0.0)
    print(line)
print("说明: 值=src/var 中位数比率; >1.00 表示 var(zhujing变体)比 Source 快; >=1.10 达标 10%")
