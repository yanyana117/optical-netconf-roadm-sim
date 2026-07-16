#!/usr/bin/env python3
"""Plot the transponder BER-vs-OSNR sweep produced by onsim-ber-sweep.

Usage: onsim-ber-sweep | python3 tools/plot_ber_sweep.py docs/experiments/ber_vs_osnr.png
"""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

FEC_LIMIT = 2e-2  # SD-FEC ceiling used by the device model

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "ber_vs_osnr.png"
    series = {}  # (modulation, rate) -> ([osnr], [ber])
    for row in csv.DictReader(sys.stdin):
        key = (row["modulation"], int(row["rate_gbps"]))
        xs, ys = series.setdefault(key, ([], []))
        xs.append(float(row["osnr_db"]))
        ys.append(float(row["pre_fec_ber"]))

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.2), sharey=True)
    for ax, rate in zip(axes, (100, 200, 400)):
        for mod in ("QPSK", "8QAM", "16QAM"):
            xs, ys = series[(mod, rate)]
            ax.semilogy(xs, ys, label=mod, linewidth=2)
        ax.axhline(FEC_LIMIT, color="crimson", linestyle="--", linewidth=1.2)
        ax.text(8.4, FEC_LIMIT * 1.6, "SD-FEC limit (2e-2)", color="crimson",
                fontsize=8)
        ax.set_title(f"{rate}G line rate")
        ax.set_xlabel("Received OSNR (dB)")
        ax.grid(True, which="both", alpha=0.3)
        ax.set_ylim(1e-18, 1)
    axes[0].set_ylabel("Simulated pre-FEC BER")
    axes[0].legend(title="Modulation", loc="lower left")
    fig.suptitle(
        "onsim transponder model: pre-FEC BER vs received OSNR "
        "(denser modulation and faster rates need more OSNR margin)")
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    print(f"wrote {out}")

if __name__ == "__main__":
    main()
