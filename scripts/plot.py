"""Regenerates the README figures and table from a results/bench_*.json file."""
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
COLORS = {"lob": "#2a6fdb", "reference": "#b0b7c3"}
LABELS = {"lob": "this engine", "reference": "std::map baseline"}


def tput(run: dict) -> float:
    return run["throughput_msgs_per_s"]["median"] / 1e6


def lat(run: dict, kind: str, q: str) -> float:
    return run["latency_ns_median_run"][kind][q]


def table(r: dict) -> str:
    lob, ref = r["runs"]["lob"], r["runs"]["reference"]
    rows = [
        "| | this engine | std::map baseline |",
        "|---|---:|---:|",
        f"| throughput, timed replay (M msgs/s, median of {lob['repeats']}) | {tput(lob):.1f} | {tput(ref):.1f} |",
        f"| throughput, untimed replay (M msgs/s) | {lob['untimed_throughput_msgs_per_s']['median'] / 1e6:.1f} "
        f"| {ref['untimed_throughput_msgs_per_s']['median'] / 1e6:.1f} |",
    ]
    for t in ("add", "cancel", "market"):
        rows.append(f"| {t} latency p50 / p99 / p99.9 (ns) | {lat(lob, t, 'p50'):.0f} / {lat(lob, t, 'p99'):.0f} / "
                    f"{lat(lob, t, 'p99.9'):.0f} | {lat(ref, t, 'p50'):.0f} / {lat(ref, t, 'p99'):.0f} / "
                    f"{lat(ref, t, 'p99.9'):.0f} |")
    return "\n".join(rows) + "\n"


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "latest.json"
    r = json.loads(path.read_text())
    out = ROOT / "results"

    fig, axes = plt.subplots(1, 3, figsize=(11, 3.4), sharey=False)
    for ax, t in zip(axes, ("add", "cancel", "market"), strict=True):
        qs = ["p50", "p99", "p99.9"]
        for i, eng in enumerate(("lob", "reference")):
            vals = [r["runs"][eng]["latency_ns_median_run"][t][q] for q in qs]
            ax.bar([x + 0.38 * i for x in range(3)], vals, 0.38, color=COLORS[eng], label=LABELS[eng])
        ax.set_xticks([x + 0.19 for x in range(3)], qs)
        ax.set_title(f"{t} latency")
        ax.set_ylabel("ns")
        ax.spines[["top", "right"]].set_visible(False)
    axes[0].legend(frameon=False)
    fig.suptitle(f"{r['workload']['messages'] / 1e6:.0f}M-message replay, {r['hardware']['cpu']}", fontsize=9)
    fig.tight_layout()
    fig.savefig(out / "latency.png", dpi=150)

    (out / "table.md").write_text(table(r))
    print(table(r))


if __name__ == "__main__":
    main()
