#!/usr/bin/env python3
# aggreg_paired.py — 配对数据分析：
# 对每对 (src,zhu)，每用例算 ratio = src_enc / zhu_enc；
# 跨 N 对报告 median / min / max / 95%CI（t 分布），判断最差情况是否仍 >= 10%。
import csv, glob, os, math, statistics

RUNDIR = "/tmp/paired"
CASES = ["fin-silk-mono-10ms-8k-c3","fin-silk-mono-20ms-8k-c3","fin-silk-mono-20ms-12k-c3",
         "fin-silk-mono-10ms-16k-c3","fin-silk-rt-mono-10ms-8k-c3","fin-silk-rt-mono-20ms-8k-c3",
         "fin-silk-mono-10ms-8k-c4","fin-silk-mono-10ms-8k-c5"]

def load_enc(repdir):
    f = glob.glob(os.path.join(repdir,"csv","*_final-*.csv"))
    if not f: return {}
    d = {}
    with open(f[0]) as fh:
        for row in csv.DictReader(fh):
            try: d[row["name"]] = float(row["enc_avg_us"])
            except (ValueError,KeyError): pass
    return d

# 收集每对的 pairs 序号
reps = sorted([int(x.split("_r")[1]) for x in os.listdir(RUNDIR) if x.startswith("src_r")])

results = {}  # case -> list of ratios
for i in reps:
    s = load_enc(os.path.join(RUNDIR,"src_r%d" % i))
    z = load_enc(os.path.join(RUNDIR,"zhu_r%d" % i))
    if not s or not z: 
        print("pair %d missing data" % i); continue
    for c in CASES:
        if c in s and c in z:
            results.setdefault(c,[]).append(s[c]/z[c])

# t 值 for 95% two-sided, df=n-1
def t95(n):
    tbl={1:12.706,2:4.303,3:3.182,4:2.776,5:2.571,6:2.447,7:2.365,8:2.306,9:2.262,10:2.228}
    return tbl.get(n,1.96)

print("pairs used=%d" % len(reps))
print("%-32s %7s %7s %7s %7s %7s" % ("case","median","min","max","CI_low","CI_high"))
print("-"*78)
for c in CASES:
    r = results.get(c)
    if not r: print(c,"no data"); continue
    n=len(r); med=statistics.median(r); mn=min(r); mx=max(r)
    avg=statistics.mean(r); sd=statistics.stdev(r) if n>1 else 0
    half = t95(n)*sd/math.sqrt(n)
    print("%-32s %7.3f %7.3f %7.3f %7.3f %7.3f  (n=%d)" % (c,med,mn,mx,avg-half,avg+half,n))

print("\n判断: 若某用例的 min(最差配对) >= 1.10，则即使最差情况也达标(>10%提速)。")
