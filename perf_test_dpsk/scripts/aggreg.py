#!/usr/bin/env python3
# aggreg.py - 聚合 final-bench 多重复测量，取每库每用例的最优(min)/中位(median)，计算提速
import glob, csv, statistics, os

SRCDIR = "/home/zl_00986855/workspace/libopus/perf_test_dpsk/output/csv"

def load_lib(prefix):
    files = sorted(glob.glob(os.path.join(SRCDIR, prefix + "*final-bench*.csv")))
    data = {}  # name -> list of enc_avg
    rt = {}    # name -> list of rt_avg
    for f in files:
        with open(f) as fh:
            for row in csv.DictReader(fh):
                nm = row["name"]
                try:
                    e = float(row["enc_avg_us"])
                    r = float(row["rt_avg_us"])
                except ValueError:
                    continue
                data.setdefault(nm, []).append(e)
                rt.setdefault(nm, []).append(r)
    return data, rt, len(files)

src, src_rt, nsrc = load_lib("opus-1.6.1-Source")
zhu, zhu_rt, nzhu = load_lib("zhujing-sve2-opus")

print("reps: source=%d zhujing=%d" % (nsrc, nzhu))
print("%-32s %8s %8s %8s %8s %8s %8s" % ("case","src_min","zhu_min","min_spd","src_med","zhu_med","med_spd"))
print("-"*96)
for nm in src:
    if nm not in zhu: continue
    s_min = min(src[nm]); z_min = min(zhu[nm])
    s_med = statistics.median(src[nm]); z_med = statistics.median(zhu[nm])
    min_spd = s_min/z_min if z_min>0 else 0
    med_spd = s_med/z_med if z_med>0 else 0
    print("%-32s %8.2f %8.2f %7.1f%% %8.2f %8.2f %7.1f%%" % (nm, s_min, z_min, min_spd*100, s_med, z_med, med_spd*100))
