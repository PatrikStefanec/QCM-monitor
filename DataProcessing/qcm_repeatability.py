#!/usr/bin/env python3
"""
QCM repeatability analysis: compares steady rates of the same step across
several runs (logs) and reports mean, SD, RSD, spread, 95 % CI and trend.

Usage:
    python qcm_repeatability.py [qcm_repeatability.ini]

Requires qcm_analysis.py in the same folder (uses its log loader).
"""
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from qcm_analysis import load_config, load_log, label_segments, parse_int_list

# two-sided 95 % Student-t critical values by degrees of freedom
T975 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365,
        8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179, 13: 2.160,
        14: 2.145, 15: 2.131, 16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093,
        20: 2.086}


def t_crit(dof):
    return T975.get(dof, 2.0 if dof > 20 else np.nan)


# ----------------------------------------------------------- window logic
def pick_segments(segs, occ):
    occ = occ.strip().lower()
    if not segs:
        return []
    if occ == "all":
        return list(segs)
    if occ == "longest":
        return [max(segs, key=lambda s: s["t1"] - s["t0"])]
    if occ == "first":
        return [segs[0]]
    if occ == "last":
        return [segs[-1]]
    k = int(occ)
    return [segs[k - 1]] if 1 <= k <= len(segs) else []


def per_run_value(text, run_idx, n_runs, what):
    """One value for all runs, or a list with one value per run."""
    parts = [p.strip() for p in text]
    if len(parts) == 1:
        return parts[0]
    if len(parts) == n_runs:
        return parts[run_idx]
    sys.exit(f"{what}: give 1 value or {n_runs} values (one per run), got {len(parts)}")


def get_windows(sec, segs, run_idx, n_runs):
    mode = sec.get("mode", "label").strip().lower()
    if mode == "interval":
        items = [x for x in sec.get("intervals", "").split(";") if x.strip()]
        item = per_run_value(items, run_idx, n_runs, "intervals")
        m = re.match(r"^\s*([-+0-9.eE]+?)\s*-\s*([0-9.eE+]+)\s*$", item)
        if not m:
            sys.exit(f"Cannot parse interval '{item}' (expected t0-t1)")
        return [(float(m.group(1)), float(m.group(2)))]

    if mode != "label":
        sys.exit(f"Unknown mode '{mode}' (use label or interval)")

    label = sec.get("label", "").strip().lower()
    matches = [s for s in segs if s["label"].lower() == label]
    occ = per_run_value(sec.get("occurrence", "longest").split(","),
                        run_idx, n_runs, "occurrence")
    trim0 = sec.getfloat("trim_start_s", 0.0)
    trim1 = sec.getfloat("trim_end_s", 0.0)
    wins = []
    for s in pick_segments(matches, occ):
        t0, t1 = s["t0"] + trim0, s["t1"] - trim1
        if t1 > t0:
            wins.append((t0, t1))
    return wins


# ------------------------------------------------------------- statistics
def window_stats(df, ch, windows, rate_col, sign, min_samples):
    d = df[df["Channel"] == ch]
    mask = np.zeros(len(d), dtype=bool)
    for t0, t1 in windows:
        mask |= ((d["Time_s"] >= t0) & (d["Time_s"] <= t1)).to_numpy()
    w = d[mask].dropna(subset=[rate_col])
    if len(w) < min_samples:
        return None
    t = w["Time_s"].to_numpy()
    r = sign * w[rate_col].to_numpy()
    mean = r.mean()
    span = t.max() - t.min()
    drift = np.polyfit(t, r, 1)[0] * span / abs(mean) * 100 if mean != 0 else np.nan
    return {
        "n": len(w),
        "dev": int(w["DeviceId"].iloc[0]) if "DeviceId" in w else None,
        "t0": t.min(), "t1": t.max(), "duration": span,
        "mean": mean, "std": r.std(ddof=1), "median": float(np.median(r)),
        "slope": sign * np.polyfit(t, w["Frequency_Hz"].to_numpy(), 1)[0],
        "drift_pct": drift,   # change of the rate across the window, % of mean
    }


def summarize(values):
    v = np.array([x for x in values if np.isfinite(x)], dtype=float)
    n = len(v)
    if n == 0:
        return None
    mean = v.mean()
    sd = v.std(ddof=1) if n > 1 else np.nan
    out = {"n": n, "mean": mean, "sd": sd,
           "rsd": sd / abs(mean) * 100 if n > 1 and mean != 0 else np.nan,
           "min": v.min(), "max": v.max(),
           "spread": (v.max() - v.min()) / abs(mean) * 100 if mean != 0 else np.nan,
           "ci": t_crit(n - 1) * sd / np.sqrt(n) if n > 1 else np.nan,
           "trend": np.nan}
    if n >= 3:  # linear trend vs run number, % of mean per run
        idx = np.array([i for i, x in enumerate(values) if np.isfinite(x)])
        out["trend"] = np.polyfit(idx, v, 1)[0] / abs(mean) * 100
    return out


# ------------------------------------------------------------------- plot
def make_plot(steps, keys, values, run_names, summaries, metric):
    nr, nc = len(steps), len(keys)
    fig, axes = plt.subplots(nr, nc, figsize=(4.2 * nc, 3.0 * nr), squeeze=False)
    x = np.arange(1, len(run_names) + 1)
    for i, step in enumerate(steps):
        for j, key in enumerate(keys):
            ax = axes[i][j]
            y = np.array(values[step][key], dtype=float)
            s = summaries[step][key]
            ax.plot(x, y, "o", ms=7)
            if s:
                ax.axhline(s["mean"], color="k", lw=1)
                if np.isfinite(s["sd"]):
                    ax.axhspan(s["mean"] - s["sd"], s["mean"] + s["sd"],
                               color="gray", alpha=0.25, lw=0)
            ttl = f"{step} | {key if isinstance(key, str) else f'Ch {key}'}"
            if s and np.isfinite(s["rsd"]):
                ttl += f"   RSD {s['rsd']:.1f} %"
            ax.set_title(ttl, fontsize=9)
            ax.set_xticks(x)
            ax.set_xticklabels(run_names, fontsize=8)
            ax.set_xlim(0.5, len(x) + 0.5)
            ax.grid(alpha=0.3)
            if j == 0:
                ax.set_ylabel(f"rate ({metric}) Hz/s")
    fig.suptitle("Run-to-run repeatability  (line = mean, band = ±1 SD)", fontsize=10)
    fig.tight_layout()
    return fig


# ------------------------------------------------------------------- main
def main():
    cfg_path = sys.argv[1] if len(sys.argv) > 1 else "qcm_repeatability.ini"
    cfg = load_config(cfg_path)
    base = Path(cfg_path).resolve().parent

    def rel(p):
        p = Path(p)
        return p if p.is_absolute() else base / p

    a = cfg["analysis"]
    rate_col = a.get("rate_column", "Rate60pt_Hz_s")
    sign = -1.0 if a.getboolean("invert_rate", False) else 1.0
    metric = a.get("metric", "mean").strip().lower()
    if metric not in ("mean", "median", "slope"):
        sys.exit("metric must be mean, median or slope")
    min_samples = a.getint("min_samples", 50)
    ratio = parse_int_list(a.get("ratio_channels", ""))
    if ratio and len(ratio) != 2:
        sys.exit("ratio_channels needs exactly two channels, e.g. 1,2")

    files = [l.strip() for l in cfg["input"]["log_files"].splitlines() if l.strip()]
    if len(files) < 2:
        sys.exit("Need at least 2 log files in [input] log_files")
    run_names = [Path(f).stem.split("_")[-1] for f in files]
    if len(set(run_names)) < len(run_names):
        run_names = [Path(f).stem for f in files]

    logs = [load_log(rel(f)) for f in files]
    all_ch = sorted({int(c) for df in logs for c in df["Channel"].unique()})
    channels = parse_int_list(a.get("channels", "")) or all_ch
    segs_per_run = [label_segments(df) for df in logs]
    n_runs = len(files)

    step_names = [s for s in cfg.sections() if s.lower().startswith("step")]
    if not step_names:
        sys.exit("No [step_*] sections in config")
    keys = list(channels) + ([f"Ch{ratio[0]}/Ch{ratio[1]}"] if ratio else [])

    values = {s: {k: [np.nan] * n_runs for k in keys} for s in step_names}
    detail = {s: {k: [None] * n_runs for k in channels} for s in step_names}
    rows = []
    notes = []
    devs = {}

    for s in step_names:
        sec = cfg[s]
        for r, (df, segs) in enumerate(zip(logs, segs_per_run)):
            wins = get_windows(sec, segs, r, n_runs)
            if not wins:
                notes.append(f"{s}: no usable window in {run_names[r]}")
                continue
            for ch in channels:
                st = window_stats(df, ch, wins, rate_col, sign, min_samples)
                if st is None:
                    notes.append(f"{s}: Ch{ch} in {run_names[r]}: too few samples")
                    continue
                values[s][ch][r] = st[metric]
                detail[s][ch][r] = st
                devs.setdefault(ch, st["dev"])
                rows.append({"step": s, "run": run_names[r], "channel": ch,
                             "device": st["dev"], "t_start": st["t0"],
                             "t_end": st["t1"], "n": st["n"],
                             "mean": st["mean"], "std": st["std"],
                             "median": st["median"], "fit_slope": st["slope"],
                             "drift_pct": st["drift_pct"], "value_used": st[metric]})
            if ratio:
                a_, b_ = values[s][ratio[0]][r], values[s][ratio[1]][r]
                if np.isfinite(a_) and np.isfinite(b_) and b_ != 0:
                    values[s][keys[-1]][r] = a_ / b_

    summaries = {s: {k: summarize(values[s][k]) for k in keys} for s in step_names}

    # ------------------------------------------------------------- report
    L = []
    L.append(f"Repeatability over {n_runs} runs: {', '.join(run_names)}")
    L.append(f"Rate column: {rate_col}   metric per run: {metric}   "
             f"unit: Hz/s{' (sign inverted)' if sign < 0 else ''}")
    L.append("SD = sample SD (n-1) across runs, RSD = SD/mean, "
             "spread = (max-min)/mean, CI95 = Student-t half-width of the mean, "
             "trend = linear slope vs run number in % of mean per run (n>=3).")
    L.append("")
    for s in step_names:
        L.append(f"[{s}]  {dict(cfg[s])}")
        # per-run values
        for k in keys:
            is_ratio = isinstance(k, str)
            name = k if is_ratio else f"Ch{k} (dev {devs.get(k, '?')})"
            fmt = "{:.4f}" if is_ratio else "{:.3f}"
            vals = "  ".join(f"{run_names[i]}={fmt.format(v)}" if np.isfinite(v)
                             else f"{run_names[i]}=n/a"
                             for i, v in enumerate(values[s][k]))
            L.append(f"  {name}: {vals}")
            sm = summaries[s][k]
            if not sm:
                L.append("     no data")
                continue
            f2 = "{:.4f}" if is_ratio else "{:.3f}"
            L.append(f"     n={sm['n']}  mean={f2.format(sm['mean'])}  "
                     f"SD={f2.format(sm['sd'])}  RSD={sm['rsd']:.2f} %  "
                     f"spread={sm['spread']:.2f} %  "
                     f"CI95=±{f2.format(sm['ci'])}  trend={sm['trend']:+.2f} %/run")
            if not is_ratio:
                dr = [d["drift_pct"] for d in detail[s][k] if d]
                if dr:
                    L.append("     in-window rate drift (% over window): "
                             + ", ".join(f"{x:+.1f}" for x in dr))
        L.append("")
    if notes:
        L.append("Notes:")
        L += [f"  - {n}" for n in notes]
        L.append("")
    L.append("Caveat: with few runs the SD itself is uncertain "
             "(n=3: 95 % interval roughly 0.5x to 6x). Use 5-10 runs for a "
             "number you want to quote.")
    report = "\n".join(L)
    print(report)

    rf = rel(cfg["output"].get("results_file", "repeatability.txt"))
    rf.write_text(report + "\n", encoding="utf-8")
    print(f"\nSaved: {rf}")
    cf = rel(cfg["output"].get("csv_file", "repeatability_runs.csv"))
    pd.DataFrame(rows).to_csv(cf, index=False)
    print(f"Saved: {cf}")

    fig = make_plot(step_names, keys, values, run_names, summaries, metric)
    if cfg["output"].getboolean("save_plot", True):
        pf = rel(cfg["output"].get("plot_file", "repeatability.png"))
        fig.savefig(pf, dpi=150)
        print(f"Saved: {pf}")
    if cfg["output"].getboolean("show_plot", True):
        plt.show()


if __name__ == "__main__":
    main()
