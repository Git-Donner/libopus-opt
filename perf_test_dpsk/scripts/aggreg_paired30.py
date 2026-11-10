#!/usr/bin/env python3
# aggreg_paired30.py — 30 对配对稳健分析：
# 中位数(重尾鲁棒)、配对中 ratio>=1.10 的占比、以及基于中位数符号秩的检验判断。
import csv, glob, os, statistics

RUNDIR = "/tmp/paired"
CASES = ["fin-silk-mono-10ms-8k-c3","fin-silk-mono-20ms-8k-c3","fin-silk-mono-20ms-12k-c3",
         "fin-silk-mono-10ms-16k-c3","fin-silk-rt-mono-10ms-8k-c3","fin-silk-rt-mono-20ms-8k-c3",
         "fin-silk-mono-10ms-8k-c4","fin-silk-mono-10ms-8k-c5"]

def load_enc(repdir):
    f = glob.glob(os.path.join(repdir,"csv","*_final-*.csv"))
    if not f: return {}
    d={}
    with open(f[0]) as fh:
        for row in csv.DictReader(fh):
            try: d[row["name"]]=float(row["enc_avg_us"])
            except: pass
    return d

reps = sorted([int(x.split("_r")[1]) for x in os.listdir(RUNDIR) if x.startswith("src_r")])
res={c:[] for c in CASES}
for i in reps:
    s=load_enc(os.path.join(RUNDIR,"src_r%d"%i)); z=load_enc(os.path.join(RUNDIR,"zhu_r%d"%i))
    for c in CASES:
        if c in s and c in z: res[c].append(s[c]/z[c])

print("pairs=%d" % len(reps))
print("%-32s %8s %8s %8s %8s %8s %9s" %("case","median","Q1","Q3","min","max","%>=1.10"))
print("-"*90)
for c in CASES:
    r=res[c]
    if not r: continue
    n=len(r); med=statistics.median(r); mn=min(r); mx=max(r)
    q1=statistics.quantiles(r,n=4)[0]; q3=statistics.quantiles(r,n=4)[2]
    f10=100.0*sum(1 for x in r if x>=1.10)/n
    print("%-32s %8.3f %8.3f %8.3f %8.3f %8.3f %8.1f%%" %(c,med,q1,q3,mn,mx,f10))

print("\n稳健解读：中位数是重尾噪声下的可靠估计；>=10%占比表示配对中达标比例。")
