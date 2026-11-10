#!/usr/bin/env python3
# compare_aggreg.py <outroot> <N> — 逐测试用例、逐指标，计算 N 次独立测试的提升比例平均值
#
# 指标: enc.min / enc.p50 / enc.p95 / dec.min / dec.p50 / dec.p95 (弃 avg/max/p99)
# 按 CSV 表头列名解析(不受加列影响)。
import csv, glob, os, statistics, sys

outroot = sys.argv[1] if len(sys.argv)>1 else "."
N = int(sys.argv[2]) if len(sys.argv)>2 else 1

# 指标 -> CSV 列名
METRIC_COL = {"enc.min":"enc_min_us","enc.p50":"enc_p50_us","enc.p95":"enc_p95_us",
              "dec.min":"dec_min_us","dec.p50":"dec_p50_us","dec.p95":"dec_p95_us"}
METRIC_ORDER = ["enc.min","enc.p50","enc.p95","dec.min","dec.p50","dec.p95"]

def load_run(run_dir):
    files = glob.glob(os.path.join(run_dir,"csv","*.csv"))
    if not files: return {}
    d = {}
    with open(files[0]) as fh:
        rd = csv.DictReader(fh)          # 按表头列名读取，天然免疫加列
        fields = rd.fieldnames or []
        for row in rd:
            nm = row.get("name","")
            if not nm: continue
            vals = {}
            for k,col in METRIC_COL.items():
                try: vals[k] = float(row[col])
                except (ValueError,TypeError,KeyError): vals[k]=None
            d[nm] = vals
    return d

pairs = {}
for i in range(1, N+1):
    sr = load_run(os.path.join(outroot,"src","r%d"%i))
    zr = load_run(os.path.join(outroot,"zhu","r%d"%i))
    for nm in sr:
        if nm not in zr: continue
        for m in METRIC_ORDER:
            sv= sr[nm][m]; zv= zr[nm][m]
            if sv and zv:
                pairs.setdefault(nm,{}).setdefault(m,[]).append((sv,zv))

print("对比: Source vs zhujing | N=%d 次独立测试 | 指标 min/p50/p95 (按表头解析)" % N)
print("-"*104)
print("%-38s %-9s %10s %10s %15s %12s %3s" % ("case","metric","src_avg","zhu_avg","avg_ratio","avg_improv%","N"))
print("-"*104)
rows_out=[]
for nm in sorted(pairs):
    for m in METRIC_ORDER:
        pr = pairs[nm].get(m)
        if not pr: continue
        ratios  = [s/z for s,z in pr]
        improv  = [(s-z)/s*100.0 for s,z in pr]
        src_avg = statistics.mean([s for s,z in pr])
        zhu_avg = statistics.mean([z for s,z in pr])
        avg_ratio = statistics.mean(ratios)
        avg_improv = statistics.mean(improv)
        print("%-38s %-9s %10.2f %10.2f %15.3f %11.1f%% %3d" %
              (nm, m, src_avg, zhu_avg, avg_ratio, avg_improv, len(pr)))
        rows_out.append([nm,m,round(src_avg,2),round(zhu_avg,2),round(avg_ratio,3),round(avg_improv,2),len(pr)])

os.makedirs(outroot, exist_ok=True)
with open(os.path.join(outroot,"summary.csv"),"w",newline="") as f:
    w=csv.writer(f)
    w.writerow(["case","metric","src_avg_us","zhu_avg_us","avg_speedup_ratio_src_over_zhu","avg_improve_pct","n_runs"])
    w.writerows(rows_out)
print("\n汇总已写: %s/summary.csv" % outroot)
print("注: 平均提速比=mean(src/zhu) 平均提升%=mean((src-zhu)/src*100)；>=1.10 / >=10% 即达标")
