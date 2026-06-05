"""Runs the benchmarks behind every number in the README and writes results/ plus figures.

1. generate the seeded 10M-message workload
2. latency harness (warm-up + 5 timed repeats) for the engine and the std::map reference
3. save one JSON stamped with git commit, hardware and build flags; plot from it
"""
import argparse
import json
import os
import platform
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def sh(*cmd, **kw) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout.strip()


def try_sh(*cmd) -> str:
    try:
        return sh(*cmd)
    except (OSError, subprocess.CalledProcessError):
        return ""


def read(path: str) -> str:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return ""


def hardware(build: Path) -> dict:
    cpu = try_sh("sysctl", "-n", "machdep.cpu.brand_string")
    if not cpu:
        m = re.search(r"model name\s*:\s*(.*)", read("/proc/cpuinfo"))
        cpu = m.group(1) if m else platform.processor()
    cache = read(str(build / "CMakeCache.txt"))

    def get(key: str) -> str:
        m = re.search(rf"^{key}:\w+=(.*)$", cache, re.M)
        return m.group(1) if m else ""

    no_turbo = read("/sys/devices/system/cpu/intel_pstate/no_turbo")
    boost = read("/sys/devices/system/cpu/cpufreq/boost")
    return {
        "cpu": cpu,
        "logical_cpus": os.cpu_count(),
        "os": platform.platform(),
        "compiler": try_sh(get("CMAKE_CXX_COMPILER") or "c++", "--version").splitlines()[0],
        "build_type": get("CMAKE_BUILD_TYPE"),
        "flags": "-O3 -march=native" if get("LOB_NATIVE") == "ON" else "-O3",
        "turbo": "off" if no_turbo == "1" or boost == "0" else ("on" if no_turbo == "0" or boost == "1" else "unknown"),
        "governor": read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor") or "unknown",
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--count", type=int, default=10_000_000)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--cpu", type=int, default=1, help="core to pin the matcher to (Linux only)")
    args = ap.parse_args()

    build = ROOT / args.build
    workload = build / f"workload_{args.seed}_{args.count}.bin"
    if not workload.exists():
        print(sh(str(build / "gen_workload"), "--seed", str(args.seed), "--count", str(args.count),
                 "--out", str(workload)))
    book = sh(str(build / "replay"), str(workload)).splitlines()[0]

    runs = {}
    for engine in ("lob", "reference"):
        cmd = [str(build / "latency_harness"), str(workload), "--repeats", str(args.repeats), "--cpu", str(args.cpu)]
        if engine == "reference":
            cmd.append("--reference")
        print(f"running {engine} ...", flush=True)
        runs[engine] = json.loads(sh(*cmd))

    commit = sh("git", "rev-parse", "--short=12", "HEAD", cwd=ROOT)
    if sh("git", "status", "--porcelain", "--untracked-files=no", cwd=ROOT):
        commit += "-dirty"
    result = {
        "git_commit": commit,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "hardware": hardware(build),
        "workload": {"seed": args.seed, "messages": args.count, "mix": "60% add, 35% cancel, 5% market",
                     "final_book": book},
        "runs": runs,
    }
    out = ROOT / "results" / f"bench_{commit}.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(result, indent=1))
    (ROOT / "results" / "latest.json").write_text(json.dumps(result, indent=1))
    print(f"wrote {out}")
    subprocess.run(["python3", str(ROOT / "scripts" / "plot.py"), str(out)], check=True)


if __name__ == "__main__":
    main()
