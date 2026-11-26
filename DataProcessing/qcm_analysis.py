
#!/usr/bin/env python3
"""
QCM log analysis (step 1): plot frequency + deposition rate, highlight labels,
print/save steady rates.
 
Usage:
    python qcm_analysis.py [config.ini]
"""
import sys
import configparser
from pathlib import Path
 
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
 
 
# ----------------------------------------------------------------- config
def parse_int_list(text):
    text = (text or "").strip()
    if not text:
        return None
    return [int(x) for x in text.replace(";", ",").split(",") if x.strip()]
 
 
def load_config(path):
    cfg = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    if not cfg.read(path, encoding="utf-8"):
        sys.exit(f"Config file not found: {path}")
    return cfg
 
 
# ------------------------------------------------------------------- data
def load_log(path):
    df = pd.read_csv(path)
    df.columns = [c.strip() for c in df.columns]
    if "Label" not in df.columns:
        df["Label"] = ""
    df["Label"] = df["Label"].fillna("").astype(str).str.strip()
    df = df.sort_values("Time_s", kind="stable").reset_index(drop=True)
    return df
 
 
def label_segments(df):
    """Contiguous runs of the same label (over the whole log) -> list of dicts."""
    seg_id = (df["Label"] != df["Label"].shift()).cumsum()
    segs = []
    for _, g in df.groupby(seg_id, sort=True):
        label = g["Label"].iloc[0]
        if label == "":
            continue
        segs.append({"label": label,
                     "t0": g["Time_s"].min(),
                     "t1": g["Time_s"].max()})
    return segs
 
 
# ----------------------------------------------------------- steady rates
def resolve_windows(sec, segs):
    """Return list of (name, t0, t1) for one [steady_*] section."""
    mode = sec.get("mode", "label").strip().lower()
    if mode == "interval":
        return [(f"t={sec.getfloat('t_start'):g}..{sec.getfloat('t_end'):g} s",
                 sec.getfloat("t_start"), sec.getfloat("t_end"))]
 
    if mode != "label":
        sys.exit(f"Unknown mode '{mode}' (use label or interval)")
 
    label = sec.get("label", "").strip()
    trim0 = sec.getfloat("trim_start_s", 0.0)
    trim1 = sec.getfloat("trim_end_s", 0.0)
    occ = sec.get("occurrence", "all").strip().lower()
 
    matches = [s for s in segs if s["label"].lower() == label.lower()]
    if not matches:
        print(f"  WARNING: label '{label}' not found in log")
        return []
    if occ != "all":
        k = int(occ)
        if k < 1 or k > len(matches):
            print(f"  WARNING: label '{label}' has only {len(matches)} occurrence(s)")
            return []
        matches = [matches[k - 1]]
 
    wins = []
    for i, s in enumerate(matches, 1):
        t0, t1 = s["t0"] + trim0, s["t1"] - trim1
        if t1 <= t0:
            print(f"  WARNING: '{label}' occurrence {i} too short after trimming")
            continue
        wins.append((f"{label}" + (f" #{i}" if len(matches) > 1 else ""), t0, t1))
    return wins
 
 
def evaluate(df, channels, rate_col, sign, windows):
    """Stats per channel, pooling all windows of the section."""
    rows = []
    for ch in channels:
        d = df[df["Channel"] == ch]
        mask = np.zeros(len(d), dtype=bool)
        for _, t0, t1 in windows:
            mask |= ((d["Time_s"] >= t0) & (d["Time_s"] <= t1)).to_numpy()
        w = d[mask].dropna(subset=[rate_col])
        if len(w) < 2:
            rows.append({"ch": ch, "n": len(w)})
            continue
        r = sign * w[rate_col].to_numpy()
        # Slope of a linear fit of frequency vs time (independent of the filter)
        slope = sign * np.polyfit(w["Time_s"], w["Frequency_Hz"], 1)[0]
        rows.append({
            "ch": ch, "n": len(w),
            "dev": int(w["DeviceId"].iloc[0]) if "DeviceId" in w else None,
            "mean": r.mean(), "std": r.std(ddof=1), "median": np.median(r),
            "slope": slope,
            "t0": w["Time_s"].min(), "t1": w["Time_s"].max(),
        })
    return rows
 
 
def format_results(name, windows, rows, rate_col, invert):
    unit = "Hz/s" + (" (sign inverted)" if invert else "")
    lines = [f"[{name}]  rate column: {rate_col}, unit: {unit}"]
    for wn, t0, t1 in windows:
        lines.append(f"  window: {wn}  {t0:.1f} .. {t1:.1f} s")
    lines.append(f"  {'Ch':>2} {'Dev':>4} {'N':>6} {'mean':>10} {'std':>8} "
                 f"{'median':>10} {'fit slope':>10}")
    for r in rows:
        if "mean" not in r:
            lines.append(f"  {r['ch']:>2}  -- not enough data (N={r['n']})")
            continue
        lines.append(f"  {r['ch']:>2} {str(r['dev']):>4} {r['n']:>6} "
                     f"{r['mean']:>10.4f} {r['std']:>8.4f} "
                     f"{r['median']:>10.4f} {r['slope']:>10.4f}")
    return "\n".join(lines)
 
 
# ------------------------------------------------------------------- plot
def make_plot(df, channels, segs, steady_windows, cfg, sign):
    pcfg = cfg["plot"]
    rate_col = pcfg.get("rate_column", "Rate60pt_Hz_s")
    relative = pcfg.get("frequency_mode", "relative").strip().lower() == "relative"
 
    fig, (ax_f, ax_r) = plt.subplots(2, 1, sharex=True, figsize=(12, 7),
                                     gridspec_kw={"height_ratios": [1, 1]})
    colors = plt.rcParams["axes.prop_cycle"].by_key()["color"]
 
    for i, ch in enumerate(channels):
        d = df[df["Channel"] == ch]
        if d.empty:
            continue
        dev = d["DeviceId"].iloc[0] if "DeviceId" in d else "?"
        name = f"Ch {ch} (dev {dev})"
        f = d["Frequency_Hz"] - (d["Frequency_Hz"].iloc[0] if relative else 0)
        ax_f.plot(d["Time_s"], f, color=colors[i % len(colors)], lw=1, label=name)
        ax_r.plot(d["Time_s"], sign * d[rate_col], color=colors[i % len(colors)],
                  lw=1, label=name)
 
    ax_f.set_ylabel("Δf from start (Hz)" if relative else "Frequency (Hz)")
    ax_r.set_ylabel(f"{rate_col.replace('_Hz_s', '')} (Hz/s)"
                    + (" ×(-1)" if sign < 0 else ""))
    ax_r.set_xlabel("Time (s)")
    ax_f.legend(loc="best", fontsize=8)
    ax_f.grid(alpha=0.3)
    ax_r.grid(alpha=0.3)
 
    # Label regions
    if pcfg.getboolean("show_labels", True):
        label_colors = {}
        pal = plt.get_cmap("tab20").colors
        for s in segs:
            c = label_colors.setdefault(s["label"], pal[len(label_colors) % len(pal)])
            for ax in (ax_f, ax_r):
                ax.axvspan(s["t0"], s["t1"], color=c, alpha=0.18, lw=0)
            ax_f.text((s["t0"] + s["t1"]) / 2, 1.01, s["label"],
                      transform=ax_f.get_xaxis_transform(), ha="center",
                      va="bottom", fontsize=8, rotation=0 if len(segs) < 8 else 45)
 
    # Steady windows
    if pcfg.getboolean("show_steady_windows", True):
        for name, t0, t1 in steady_windows:
            ax_r.axvspan(t0, t1, facecolor="none", edgecolor="k", ls="--", lw=1)
            ax_r.axvline(t0, color="k", ls="--", lw=0.8)
            ax_r.axvline(t1, color="k", ls="--", lw=0.8)
 
    fig.tight_layout()
    return fig
 
 
# ------------------------------------------------------------------- main
def main():
    cfg_path = sys.argv[1] if len(sys.argv) > 1 else "qcm_config.ini"
    cfg = load_config(cfg_path)
    base = Path(cfg_path).resolve().parent
 
    def rel(p):  # paths in config are relative to the config file
        p = Path(p)
        return p if p.is_absolute() else base / p
 
    df = load_log(rel(cfg["input"]["log_file"]))
    rate_col = cfg["plot"].get("rate_column", "Rate60pt_Hz_s")
    if rate_col not in df.columns:
        sys.exit(f"Column '{rate_col}' not in log. Available: {list(df.columns)}")
    invert = cfg["plot"].getboolean("invert_rate", False)
    sign = -1.0 if invert else 1.0
 
    all_ch = sorted(int(c) for c in df["Channel"].unique())
    channels = parse_int_list(cfg["plot"].get("channels", "")) or all_ch
 
    segs = label_segments(df)
    print(f"Log: {cfg['input']['log_file']}  |  channels: {all_ch}  |  "
          f"duration: {df['Time_s'].min():.1f}..{df['Time_s'].max():.1f} s")
    print("Labels found: " + (", ".join(f"{s['label']} [{s['t0']:.0f}-{s['t1']:.0f} s]"
                                        for s in segs) or "none"))
    print()
 
    # steady rates
    out_blocks, steady_windows = [], []
    for sname in cfg.sections():
        if not sname.lower().startswith("steady"):
            continue
        sec = cfg[sname]
        windows = resolve_windows(sec, segs)
        if not windows:
            continue
        ch_sel = parse_int_list(sec.get("channels", "")) or channels
        rows = evaluate(df, ch_sel, rate_col, sign, windows)
        out_blocks.append(format_results(sname, windows, rows, rate_col, invert))
        steady_windows += windows
 
    report = "\n\n".join(out_blocks) if out_blocks else "No steady rate sections evaluated."
    print(report)
 
    out_file = rel(cfg["output"].get("results_file", "steady_rates.txt"))
    header = f"Log: {cfg['input']['log_file']}\nConfig: {cfg_path}\n\n"
    out_file.write_text(header + report + "\n", encoding="utf-8")
    print(f"\nSaved: {out_file}")
 
    # plot
    fig = make_plot(df, channels, segs, steady_windows, cfg, sign)
    if cfg["output"].getboolean("save_plot", True):
        pf = rel(cfg["output"].get("plot_file", "qcm_plot.png"))
        fig.savefig(pf, dpi=150)
        print(f"Saved: {pf}")
    if cfg["output"].getboolean("show_plot", True):
        plt.show()
 
 
if __name__ == "__main__":
    main()
 