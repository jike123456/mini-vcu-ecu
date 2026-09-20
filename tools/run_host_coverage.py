#!/usr/bin/env python3
"""Compile production C modules on Windows and report GCC line/branch coverage.

No board or Python packages are required. Build in a new temporary directory on
every run to prevent stale .gcda counters and avoid MinGW Unicode path issues.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    "firmware/KaoYa_Project/Bsp/Src/vehicle_can.c",
    "firmware/KaoYa_Project/App/Src/vehicle_state.c",
    "firmware/KaoYa_Project/App/Src/dtc_manager.c",
    "firmware/KaoYa_Project/App/Src/uds_server.c",
    "firmware/KaoYa_Project/Bsp/Src/pid.c",
]


def run(args, cwd, env):
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args}\n{output}")
    return output


def parse_gcov(text):
    lines, missed, branches, missed_branches = 0, [], 0, []
    current_line = 0
    for row in text.splitlines():
        match = re.match(r"\s*([^:]+):\s*(\d+):", row)
        if match:
            count, current_line = match.group(1).strip(), int(match.group(2))
            if current_line and count != "-":
                lines += 1
                if count in ("#####", "=====") or count.rstrip("*") == "0":
                    missed.append(current_line)
        branch = re.match(r"branch\s+(\d+)\s+(.*)", row)
        if branch:
            branches += 1
            outcome = branch.group(2)
            if "never executed" in outcome or re.match(r"taken 0(?:%|\s|$)", outcome):
                missed_branches.append({"line": current_line, "branch": int(branch.group(1))})
    if not lines or not branches:
        raise RuntimeError("Missing executable lines or branch counters in gcov output")
    return {"lines": lines, "lines_hit": lines-len(missed),
            "branches": branches, "branches_hit": branches-len(missed_branches),
            "uncovered_lines": missed, "uncovered_branches": missed_branches}


def percent(hit, total):
    return 100.0 * hit / total if total else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", default=shutil.which("gcc") or r"D:\App\mingw64\bin\gcc.exe")
    parser.add_argument("--gcov", help="Matching gcov executable; defaults to GCC sibling")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/coverage")
    parser.add_argument("--min-lines", type=float, default=90.0)
    parser.add_argument("--min-branches", type=float, default=80.0)
    args = parser.parse_args()
    gcc = Path(args.gcc).resolve()
    gcov = Path(args.gcov).resolve() if args.gcov else gcc.with_name("gcov.exe")
    env = os.environ.copy()
    env["PATH"] = str(gcc.parent) + os.pathsep + env.get("PATH", "")
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # A failed run must not leave the previous report looking current.
    (output / "latest-status.json").write_text(json.dumps({"status": "running"}), encoding="utf-8")
    (output / "latest.json").write_text(json.dumps({"status": "running"}), encoding="utf-8")
    (output / "latest.md").write_text("# Coverage run started\n\nNo completed result for this run yet.\n", encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="ecu-coverage-") as tmp:
        stage = Path(tmp)
        for folder, name in [("firmware/KaoYa_Project/App/Inc", "app"),
                             ("firmware/KaoYa_Project/Bsp/Inc", "bsp"),
                             ("tests/host/stubs", "stubs")]:
            shutil.copytree(ROOT / folder, stage / name)
        shutil.copy2(ROOT / "firmware/Shared/boot_layout.h", stage / "stubs/production_boot_layout.h")
        flags = ["-std=c11", "-O0", "-g", "--coverage", "-Wall", "-Wextra", "-Werror",
                 "-Istubs", "-Iapp", "-Ibsp"]
        objects = []
        source_hashes = {}
        for relative in SOURCES + ["tests/host/test_core.c"]:
            src = ROOT / relative
            shutil.copy2(src, stage / src.name)
            source_hashes[relative] = hashlib.sha256((stage/src.name).read_bytes()).hexdigest()
            obj = src.stem + ".o"
            run([gcc, *flags, "-c", src.name, "-o", obj], stage, env)
            objects.append(obj)
        run([gcc, "--coverage", *objects, "-lm", "-o", "core_tests.exe"], stage, env)
        test_output = run([stage / "core_tests.exe"], stage, env)
        print(test_output, end="")
        modules = []
        for relative in SOURCES:
            name = Path(relative).name
            run([gcov, "-b", "-c", name], stage, env)
            raw = (stage / (name + ".gcov")).read_text(encoding="utf-8", errors="replace")
            metrics = parse_gcov(raw)
            metrics.update(path=relative, sha256=source_hashes[relative])
            modules.append(metrics)
            shutil.copy2(stage / (name + ".gcov"), output / (name + ".gcov"))
        totals = {key: sum(m[key] for m in modules)
                  for key in ("lines", "lines_hit", "branches", "branches_hit")}
        line_pct = percent(totals["lines_hit"], totals["lines"])
        branch_pct = percent(totals["branches_hit"], totals["branches"])
        passed = line_pct >= args.min_lines and branch_pct >= args.min_branches
        report = {"generated_utc": datetime.now(timezone.utc).isoformat(),
                  "scope": "Five production C modules on host; hardware and RTOS dependencies stubbed",
                  "compiler": run([gcc, "--version"], stage, env).splitlines()[0],
                  "gcov": run([gcov, "--version"], stage, env).splitlines()[0],
                  "flags": flags, "test_output": test_output, "modules": modules,
                  "test_sha256": source_hashes["tests/host/test_core.c"],
                  "totals": totals, "thresholds": {"lines": args.min_lines, "branches": args.min_branches},
                  "status": "pass" if passed else "fail"}
        rows = ["# Host C coverage (generated)", "", report["generated_utc"], "",
                "Production sources are copied byte-for-byte for Windows path compatibility.",
                "Only the five listed modules are in the denominator; test/stub code is excluded.",
                "Host execution does not measure ARM hardware, ISR concurrency or MC/DC.", "",
                "| Module | Lines hit/total | Line % | Branch outcomes hit/total | Branch % |",
                "|---|---:|---:|---:|---:|"]
        for m in modules:
            rows.append(f"| {Path(m['path']).name} | {m['lines_hit']}/{m['lines']} | "
                        f"{percent(m['lines_hit'],m['lines']):.2f} | {m['branches_hit']}/{m['branches']} | "
                        f"{percent(m['branches_hit'],m['branches']):.2f} |")
        rows.extend([f"| TOTAL | {totals['lines_hit']}/{totals['lines']} | {line_pct:.2f} | "
                     f"{totals['branches_hit']}/{totals['branches']} | {branch_pct:.2f} |", "",
                     "```text", test_output.strip(), "```", "",
                     f"Gate: lines >= {args.min_lines}% and branches >= {args.min_branches}%: {report['status'].upper()}"])
        (output/"latest.json").write_text(json.dumps(report, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
        (output/"latest.md").write_text("\n".join(rows)+"\n", encoding="utf-8")
        (output/"latest-status.json").write_text(json.dumps({"status": report["status"], "generated_utc": report["generated_utc"]})+"\n", encoding="utf-8")
        print(f"COVERAGE {report['status'].upper()}: lines={line_pct:.2f}% branches={branch_pct:.2f}% modules={len(modules)}")
        return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
