#!/usr/bin/env python3
"""Runs hdxf_tests as N shards in parallel and sums their results.

    python -I tools/run_tests.py [-j N] [--exe PATH] [filter ...]

The selected tests (`hdxf_tests --list [filter ...]`) are shared out by their times of the last run
(build/test-times.tsv; the longest first, each to the shard with the least so far; a test without a
time counts as one second), and shard K runs `hdxf_tests --shard=K/N --names=FILE` with a temporary directory of its own (TMP/TEMP), as tests write files by fixed names
there. The output of shard K is build/test-shards/K.log. Prints every FAIL line, the shards' times
and the sum; the exit code is 1 when a test failed or a shard did not end normally.
HDXF_TEST_TIMES=1 lists the twenty slowest tests of all shards.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMES = os.path.join(REPO, "build", "test-times.tsv")
UNKNOWN_MS = 1000


def default_exe():
    for name in ("hdxf_tests.exe", "hdxf_tests"):
        path = os.path.join(REPO, "build", "test", name)
        if os.path.exists(path):
            return path
    return os.path.join(REPO, "build", "test", "hdxf_tests")


def read_times():
    times = {}
    if os.path.exists(TIMES):
        for line in open(TIMES, encoding="utf-8"):
            ms, _, name = line.rstrip("\r\n").partition("\t")
            if name and ms.isdigit():
                times[name] = int(ms)
    return times


def share_out(names, count, times):
    """The names in `count` lists, the longest first, each to the list with the least time so far."""
    shards = [[] for _ in range(count)]
    load = [0] * count
    for name in sorted(names, key=lambda n: times.get(n, UNKNOWN_MS), reverse=True):
        k = load.index(min(load))
        shards[k].append(name)
        load[k] += times.get(name, UNKNOWN_MS)
    return shards


def run_shard(exe, index, count, filters, names, work):
    tmp = os.path.join(work, "tmp%d" % index)
    os.makedirs(tmp, exist_ok=True)
    names_file = os.path.join(work, "%d.names" % index)
    with open(names_file, "w", encoding="utf-8") as out:
        out.write("".join(name + "\n" for name in names))
    env = dict(os.environ)
    env["TMP"] = tmp
    env["TEMP"] = tmp
    env["TMPDIR"] = tmp
    env["HDXF_TEST_TIMES_FILE"] = os.path.join(work, "%d.times" % index)
    log = os.path.join(work, "%d.log" % index)
    start = time.monotonic()
    with open(log, "wb") as out:
        rc = subprocess.call([exe, "--shard=%d/%d" % (index, count), "--names=" + names_file] + filters,
                             stdout=out, stderr=subprocess.STDOUT, env=env)
    return index, rc, time.monotonic() - start, log


def main():
    parser = argparse.ArgumentParser(description="Run hdxf_tests in parallel shards")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, min(16, (os.cpu_count() or 4) - 4)))
    parser.add_argument("--exe", default=default_exe())
    parser.add_argument("filters", nargs="*")
    args = parser.parse_args()

    listed = subprocess.run([args.exe, "--list"] + args.filters, capture_output=True)
    if listed.returncode != 0:
        print("%s --list failed (exit %d)" % (args.exe, listed.returncode))
        return 1
    names = [n for n in listed.stdout.decode("utf-8", "replace").splitlines() if n.strip()]
    count = max(1, min(args.jobs, len(names)))
    times = read_times()
    shared = share_out(names, count, times)

    work = os.path.join(REPO, "build", "test-shards")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    start = time.monotonic()
    with ThreadPoolExecutor(max_workers=count) as pool:
        shards = list(pool.map(lambda k: run_shard(args.exe, k, count, args.filters, shared[k], work), range(count)))
    wall = time.monotonic() - start

    passed = failed = skipped = 0
    fails = []
    slowest = []
    broken = []
    for index, rc, seconds, log in sorted(shards):
        text = open(log, "rb").read().decode("utf-8", "replace").replace("\r\n", "\n")
        summary = re.search(r"^(\d+) passed, (\d+) failed, (\d+) skipped, \d+ total$", text, re.M)
        if not summary:
            broken.append("shard %d ended without its summary (exit %d): %s" % (index, rc, log))
        else:
            passed += int(summary.group(1))
            failed += int(summary.group(2))
            skipped += int(summary.group(3))
        fails += [line for line in text.splitlines() if line.startswith("  FAIL")]
        slowest += [(int(m.group(1)), m.group(2)) for m in re.finditer(r"^  (\d+) ms  (\S+)$", text, re.M)]
        times_file = os.path.join(work, "%d.times" % index)
        if os.path.exists(times_file):
            for line in open(times_file, encoding="utf-8"):
                ms, _, name = line.rstrip("\r\n").partition("\t")
                if name and ms.isdigit():
                    times[name] = int(ms)
        print("shard %2d: %6.1f s, %3d tests, exit %d" % (index, seconds, len(shared[index]), rc))

    with open(TIMES, "w", encoding="utf-8") as out:
        out.write("".join("%d\t%s\n" % (ms, name) for name, ms in sorted(times.items())))

    for line in fails:
        print(line)
    for line in broken:
        print(line)
    if slowest:
        print("\n=== Slowest ===")
        for ms, name in sorted(slowest, reverse=True)[:20]:
            print("  %d ms  %s" % (ms, name))
    print("\n%d passed, %d failed, %d skipped, %d total in %d shards, %.1f s"
          % (passed, failed, skipped, passed + failed + skipped, count, wall))
    ok = failed == 0 and not broken and all(rc == 0 for _, rc, _, _ in shards)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
