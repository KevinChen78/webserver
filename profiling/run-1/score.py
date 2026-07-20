#!/usr/bin/env python3
"""
从 folded.txt + top-self-merged.txt 计算 K1-K10 "调优成熟度" KPI。

用法：
    score.py <scenario_dir> [--label SCENARIO] [--kind {server,lib}]
                            [--readme-qps QPS] [--measured-qps QPS]
                            [--readme-ns NS]  [--measured-ns NS]

在 <scenario_dir> 下输出 scores.json。
"""
import argparse, json, os, re, sys
from statistics import quantiles

# ---------- KPI pattern matchers ----------
KPI_PATTERNS = {
    "K2_alloc": [
        r"\bmalloc\b", r"\bcfree\b", r"\bfree\b",
        r"\boperator new\b", r"\boperator delete\b",
        r"__gnu_cxx::", r"__libc_malloc", r"__libc_free",
        r"_int_malloc", r"_int_free", r"_mid_memalign",
    ],
    "K3_lock": [
        r"pthread_mutex_", r"pthread_rwlock_", r"pthread_spin_",
        r"std::mutex", r"std::shared_mutex", r"std::lock_guard",
        r"std::unique_lock", r"std::shared_lock", r"futex",
        r"__lll_", r"elision_lock",
    ],
    "K4_atomic": [
        r"__atomic_", r"__sync_", r"cmpxchg", r"compare_exchange",
        r"__cxx_atomic", r"__aarch64_", r"lock_xadd",
    ],
    "K5_syscall_io": [
        r"__read", r"__write", r"__recv", r"__send",
        r"epoll_", r"\bpoll\b", r"\bselect\b", r"sendfile",
        r"recvfrom", r"sendto", r"accept4?", r"__socket",
        r"__close", r"__open", r"__stat",
    ],
    "K6_idle_wait": [
        r"pthread_cond_", r"sched_yield", r"nanosleep",
        r"__clock_nanosleep", r"futex_wait", r"__pause",
    ],
    "K7_compiler_fingerprint": [
        r"memcpy", r"memmove", r"memset", r"memcmp", r"__memcpy",
        r"__memmove", r"__memset_chk",
    ],
}

# (good, bad) thresholds — score is linear between them, clipped to [0,100]
# (lower-is-better for all of these since they're "overhead fractions")
THRESHOLDS = {
    "K1_concentration_top5": (60.0, 85.0),
    "K2_alloc":              (3.0,  10.0),
    "K3_lock":               (5.0,  15.0),
    "K4_atomic":             (2.0,  8.0),
    "K5_syscall_io_server":  (15.0, 40.0),
    "K5_syscall_io_lib":     (5.0,  20.0),
    "K6_idle_wait":          (5.0,  20.0),
    "K7_compiler_fingerprint": (1.0, 5.0),
    "K9_stack_depth_p90":    (15.0, 30.0),
    "K10_reproduction":      (10.0, 30.0),  # |% deviation|
}

WEIGHTS = {
    "K1": 15, "K2": 15, "K3_K4": 15, "K5": 10,
    "K6": 10, "K7": 10, "K8": 5,  "K9": 10, "K10": 10,
}

def score_lower_better(value, good, bad):
    """Lower is better. Returns 0-100."""
    if value <= good: return 100.0
    if value >= bad:  return 0.0
    return 100.0 * (bad - value) / (bad - good)

def parse_top_self(path):
    """Parse top-self-merged.txt → list of (func, pct, samples)."""
    entries = []
    if not os.path.exists(path): return entries, 0
    total = 0
    with open(path) as f:
        for line in f:
            line = line.rstrip()
            if not line or line.startswith("#") or line.startswith("---"):
                continue
            if line.startswith("Overhead") or line.startswith("Samples") or line.startswith("Function"):
                continue
            m = re.match(r"^\s*([\d.]+)%\s+(\d+)\s+(.+)$", line)
            if m:
                pct, samples, func = m.groups()
                entries.append((func.strip(), float(pct), int(samples)))
    return entries, sum(e[2] for e in entries)

def parse_folded(path):
    """Parse folded.txt → list of (depth, count)."""
    depths = []
    total_count = 0
    if not os.path.exists(path): return depths, total_count
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line: continue
            idx = line.rfind(' ')
            if idx < 0: continue
            stack = line[:idx]
            try:
                count = int(line[idx+1:])
            except ValueError:
                continue
            depth = stack.count(';') + 1
            depths.extend([depth] * min(count, 1000))  # cap to avoid blowing mem
            total_count += count
    return depths, total_count

def match_pct(entries, patterns):
    pct_sum = 0.0
    matched = []
    for func, pct, _ in entries:
        for p in patterns:
            if re.search(p, func):
                pct_sum += pct
                matched.append((func, pct))
                break
    return pct_sum, matched[:10]

def compute(args):
    scenario = args.scenario_dir
    label = args.label or os.path.basename(scenario.rstrip('/'))
    kind = args.kind  # "server" or "lib"

    top_path = os.path.join(scenario, "top-self-merged.txt")
    folded_path = os.path.join(scenario, "folded.txt")

    entries, _ = parse_top_self(top_path)
    if not entries:
        return {"error": f"no top-self-merged.txt at {top_path}"}

    depths, total_count = parse_folded(folded_path)

    # K1: top-5 self-% sum
    top5 = sum(e[1] for e in entries[:5])
    # K8: leaf diversity — count distinct leaves in top 90% of mass
    cum = 0.0; leaf_count = 0
    for func, pct, _ in entries:
        cum += pct
        leaf_count += 1
        if cum >= 90.0: break
    # Effective leaf diversity (more = better inlining/visibility)
    if leaf_count >= 25: k8 = 100.0
    elif leaf_count >= 15: k8 = 70.0
    elif leaf_count >= 8: k8 = 40.0
    else: k8 = 15.0

    # K9: stack depth p90 (weighted)
    if depths:
        sorted_d = sorted(depths)
        n = len(sorted_d)
        p90 = sorted_d[int(0.9 * n)]
        p50 = sorted_d[int(0.5 * n)]
    else:
        p90 = p50 = 0

    # Pattern-matched KPIs
    kpi_results = {}
    for kpi, patterns in KPI_PATTERNS.items():
        val, matched = match_pct(entries, patterns)
        kpi_results[kpi] = {"pct": val, "top_matches": matched}

    # K10: reproduction
    if args.readme_qps and args.measured_qps:
        deviation = abs(args.measured_qps - args.readme_qps) / args.readme_qps * 100.0
    elif args.readme_ns and args.measured_ns:
        deviation = abs(args.measured_ns - args.readme_ns) / args.readme_ns * 100.0
    else:
        deviation = None

    # Compute scores
    scores = {}
    scores["K1"] = score_lower_better(top5, *THRESHOLDS["K1_concentration_top5"])
    scores["K2"] = score_lower_better(kpi_results["K2_alloc"]["pct"], *THRESHOLDS["K2_alloc"])
    scores["K3"] = score_lower_better(kpi_results["K3_lock"]["pct"], *THRESHOLDS["K3_lock"])
    scores["K4"] = score_lower_better(kpi_results["K4_atomic"]["pct"], *THRESHOLDS["K4_atomic"])
    k5_thresh = ("K5_syscall_io_server" if kind == "server" else "K5_syscall_io_lib")
    scores["K5"] = score_lower_better(kpi_results["K5_syscall_io"]["pct"], *THRESHOLDS[k5_thresh])
    scores["K6"] = score_lower_better(kpi_results["K6_idle_wait"]["pct"], *THRESHOLDS["K6_idle_wait"])
    scores["K7"] = score_lower_better(kpi_results["K7_compiler_fingerprint"]["pct"], *THRESHOLDS["K7_compiler_fingerprint"])
    scores["K8"] = k8
    scores["K9"] = score_lower_better(p90, *THRESHOLDS["K9_stack_depth_p90"])
    if deviation is not None:
        scores["K10"] = score_lower_better(deviation, *THRESHOLDS["K10_reproduction"])
    else:
        scores["K10"] = None  # not measured

    # Composite
    k3_k4_avg = (scores["K3"] + scores["K4"]) / 2.0
    composite_components = {
        "K1": scores["K1"], "K2": scores["K2"], "K3_K4": k3_k4_avg,
        "K5": scores["K5"], "K6": scores["K6"], "K7": scores["K7"],
        "K8": scores["K8"], "K9": scores["K9"],
    }
    total_weight = sum(WEIGHTS.values())
    if scores["K10"] is not None:
        composite_components["K10"] = scores["K10"]
    else:
        total_weight -= WEIGHTS["K10"]

    composite = sum(composite_components[k] * WEIGHTS[k] for k in composite_components) / total_weight

    def grade(s):
        if s >= 85: return "A（调优良好）"
        if s >= 70: return "B（扎实但有提升空间）"
        if s >= 55: return "C（中等）"
        if s >= 40: return "D（明显短板）"
        return "F（未调优 / 病态）"

    out = {
        "scenario": label,
        "kind": kind,
        "composite_score": round(composite, 1),
        "grade": grade(composite),
        "weights": WEIGHTS,
        "kpi_scores": {k: (round(v, 1) if v is not None else None) for k, v in scores.items()},
        "kpi_raw": {
            "K1_top5_sum_pct": round(top5, 2),
            "K1_top5_funcs": [{"func": f, "pct": round(p, 2)} for f, p, _ in entries[:5]],
            "K2_alloc_pct": round(kpi_results["K2_alloc"]["pct"], 2),
            "K3_lock_pct": round(kpi_results["K3_lock"]["pct"], 2),
            "K4_atomic_pct": round(kpi_results["K4_atomic"]["pct"], 2),
            "K5_syscall_io_pct": round(kpi_results["K5_syscall_io"]["pct"], 2),
            "K6_idle_wait_pct": round(kpi_results["K6_idle_wait"]["pct"], 2),
            "K7_compiler_pct": round(kpi_results["K7_compiler_fingerprint"]["pct"], 2),
            "K8_leaves_in_top90pct": leaf_count,
            "K9_stack_depth_p50": p50,
            "K9_stack_depth_p90": p90,
            "K10_deviation_pct": round(deviation, 2) if deviation is not None else None,
        },
        "kpi_matches": {k: v["top_matches"] for k, v in kpi_results.items()},
        "total_folded_samples": total_count,
        "thresholds": THRESHOLDS,
    }

    out_path = os.path.join(scenario, "scores.json")
    with open(out_path, "w") as f:
        json.dump(out, f, indent=2)
    print(f"[{label}] 综合分={out['composite_score']:.1f}（{out['grade']}）")
    for k in ["K1","K2","K3","K4","K5","K6","K7","K8","K9","K10"]:
        v = out["kpi_scores"][k]
        print(f"  {k}: {v}")
    print(f"  → 已写入 {out_path}")
    return out

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario_dir")
    ap.add_argument("--label")
    ap.add_argument("--kind", choices=["server","lib"], default="server")
    ap.add_argument("--readme-qps", type=float)
    ap.add_argument("--measured-qps", type=float)
    ap.add_argument("--readme-ns", type=float)
    ap.add_argument("--measured-ns", type=float)
    args = ap.parse_args()
    compute(args)
