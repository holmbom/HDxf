#!/usr/bin/env python3
"""Makes facit originals with AutoCAD 2025's core for phase 9's families (step 9.0, plan 2026-10-27-fas-9-typer.md).

    python -I tools/fas9_facit_run.py doc/facit/fas9_<family>.scr [--input FILE] [--out DIR] [--timeout SECONDS]

A facit is a file AutoCAD itself saved; this runs AutoCAD, not HDxf, and checks nothing HDxf writes. The core
(`accoreconsole.exe /i <drawing> /s <script> /l en-US`) has no LISP: a family script is plain AutoCAD commands,
one input per line (comment lines start with ';'), and two comment markers this tool expands:

    ;SAVE <theme> [<change>]    the drawing as it is now, in R12, 2000, 2004, 2007, 2010, 2013 and 2018, ASCII
                                (16 decimals) and binary: acad_<ver>_dxf_<theme>[_<change>][_bin].dxf — the names
                                of scripts 01 … 26. One change per pair: save the state, change one property, list
                                or relation, then save <theme> <change>.
    ;EXPECT <NAME> [<NAME> ...] after the run the 2018 ASCII file of the first ;SAVE must have a record of each
                                name; a script that ends without the wanted record is not a new facit.

The sequence expanded for each file is the one the core takes (checked 2026-10-27):

    _.SAVEAS / DXF / _V / <version> / 16 (or _B for binary) / <path>

The drawing the script starts from is `--input` (an AutoCAD-saved original the family needs: a plugin's, a
dynamic block's) or, by default, AutoCAD's own acad.dwt, which the core opens as a new empty drawing. AutoCAD 2025
cannot save R13 or R14 DXF: no file of those versions is made; the historical AutoCAD originals
(test/data/manifest.cmake, build/test-data/local-r13r14) and the profile doc/reference/hdxf_r13_r14_format.md are
their facit. A command the core does not have (MTEXT, VIEWBASE, plugin dialogs) stops the script: the run reports
it, and the family step marks that branch "ej testat", naming the F9 id.

Writes DIR (default doc/facit): the files, fas9_facit_log.csv (theme, change, version, format, path, made, the
file's actual $ACADVER and $ACADMAINTVER, SHA-256, records, expected names found) and <script>.run.log (the core's
output). The facit register is regenerated with tools/fas9_facit_register.py.
"""

import argparse
import csv
import glob
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inventory_types as inv  # noqa: E402

CORE = "C:/Program Files/Autodesk/AutoCAD 2025/accoreconsole.exe"
VERSIONS = [("r12", "R12", "AC1009"), ("2000", "2000", "AC1015"), ("2004", "2004", "AC1018"), ("2007", "2007", "AC1021"),
            ("2010", "2010", "AC1024"), ("2013", "2013", "AC1027"), ("2018", "2018", "AC1032")]
# The system variables the older facit scripts set before drawing (proxy graphics on, so a drawn entity has its
# proxy graphics; the thumbnail on); the dialogs off
PROLOG = ["_.FILEDIA", "0", "_.CMDDIA", "0", "_.PROXYGRAPHICS", "1"]


def find_template():
    candidates = glob.glob(os.path.expanduser("~/AppData/Local/Autodesk/AutoCAD 2025/R25.0/enu/Template/acad.dwt"))
    candidates += glob.glob("C:/Program Files/Autodesk/AutoCAD 2025/UserDataCache/en-us/Template/acad.dwt")
    return candidates[0] if candidates else None


def save_file_name(tag, theme, change, binary):
    return "acad_%s_dxf_%s%s%s.dxf" % (tag, theme, "_" + change if change else "", "_bin" if binary else "")


def expand(script_lines, out_dir):
    """The script's lines with ;SAVE expanded, the files they will make, and the ;EXPECT names."""
    out = list(PROLOG)
    saves = []
    expect = []
    base = out_dir.replace("\\", "/").rstrip("/")
    for raw in script_lines:
        line = raw.rstrip("\r\n")
        m = re.match(r"^;SAVE\s+(\S+)(?:\s+(\S+))?\s*$", line)
        if m:
            theme, change = m.group(1), m.group(2) or ""
            for tag, version, acadver in VERSIONS:
                for binary in (False, True):
                    name = save_file_name(tag, theme, change, binary)
                    out += ["_.SAVEAS", "DXF", "_V", version, "_B" if binary else "16", base + "/" + name]
                    saves.append({"theme": theme, "change": change, "file": name, "path": base + "/" + name,
                                  "version": version, "acadver": acadver, "binary": binary})
            continue
        m = re.match(r"^;EXPECT\s+(.+)$", line)
        if m:
            expect += m.group(1).split()
            continue
        out.append(line)
    out += ["_.QUIT", "_Y"]
    return out, saves, expect


def close_crash_dialogs():
    """A core that crashes leaves Autodesk's error report window (cer_dialog.exe) open on the desktop: closed here."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", "cer_dialog.exe"], capture_output=True, stdin=subprocess.DEVNULL)


def run_core(core, drawing, script, timeout, log_path, cwd):
    started = time.time()
    command = [core, "/i", drawing, "/s", script, "/l", "en-US"]
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout, cwd=cwd, stdin=subprocess.DEVNULL)
        text = (done.stdout + done.stderr).decode("utf-8", "replace").replace("\0", "")
        code = done.returncode
    except subprocess.TimeoutExpired as e:
        text = ((e.stdout or b"") + (e.stderr or b"")).decode("utf-8", "replace").replace("\0", "") + "\n[TIMEOUT]\n"
        code = None
    close_crash_dialogs()
    with open(log_path, "w", encoding="utf-8") as f:
        f.write(text)
    return code, text, time.time() - started


def self_test():
    """The expansion of ;SAVE and ;EXPECT (no AutoCAD needed)."""
    lines = ["_.LINE", "0,0", "1,1", "", ";SAVE t", ";EXPECT LINE", "_.ERASE", "_L", "", ";SAVE t erased"]
    out, saves, expect = expand(lines, "C:/x")
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise SystemExit("self-test failed: " + message)

    check(len(saves) == 28, "two states x seven versions x two formats: %d" % len(saves))
    check(saves[0]["file"] == "acad_r12_dxf_t.dxf" and saves[1]["file"] == "acad_r12_dxf_t_bin.dxf", "ASCII first, then binary")
    check(saves[14]["file"] == "acad_r12_dxf_t_erased.dxf" and saves[27]["file"] == "acad_2018_dxf_t_erased_bin.dxf",
          "the change in the name")
    check(expect == ["LINE"], "the expected record")
    check(out[:len(PROLOG)] == PROLOG and out[-2:] == ["_.QUIT", "_Y"], "the prolog and the end")
    first = out.index("_.SAVEAS")
    check(out[first:first + 6] == ["_.SAVEAS", "DXF", "_V", "R12", "16", "C:/x/acad_r12_dxf_t.dxf"], "the ASCII sequence")
    check(out[first + 6:first + 12] == ["_.SAVEAS", "DXF", "_V", "R12", "_B", "C:/x/acad_r12_dxf_t_bin.dxf"], "the binary sequence")
    check(";SAVE t" not in out and ";EXPECT LINE" not in out, "the markers are gone")
    check(out.count("") == 2, "the empty lines (Enter) stay")
    print("fas9_facit_run self-test: %d checks passed" % checks)
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("script", help="doc/facit/fas9_<family>.scr")
    p.add_argument("--input", help="the drawing to start from (default: AutoCAD's acad.dwt, a new empty drawing)")
    p.add_argument("--out", default=os.path.join(inv.ROOT, "doc", "facit"), help="where the files go")
    p.add_argument("--core", default=CORE)
    p.add_argument("--timeout", type=int, default=900)
    p.add_argument("--expand-only", action="store_true", help="write the expanded script and stop (no AutoCAD)")
    args = p.parse_args(argv)

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    with open(args.script, encoding="utf-8") as f:
        lines = f.read().split("\n")
    # (the end of the last line is no empty line: an Enter at the command prompt would repeat the last command)
    if lines and lines[-1] == "":
        lines.pop()
    expanded, saves, expect = expand(lines, out_dir)
    stem = os.path.splitext(os.path.basename(args.script))[0]
    expanded_path = os.path.join(out_dir, stem + ".expanded.scr")
    with open(expanded_path, "w", encoding="ascii", errors="replace", newline="") as f:
        f.write("\r\n".join(expanded) + "\r\n")
    if args.expand_only:
        print("%d lines, %d files, expected %s: %s" % (len(expanded), len(saves), expect or "-", expanded_path))
        return 0
    drawing = args.input or find_template()
    if not drawing or not os.path.exists(drawing):
        print("no drawing to start from (--input) and no acad.dwt found")
        return 2
    if not os.path.exists(args.core):
        print("AutoCAD's core is not at " + args.core)
        return 2
    for s in saves:
        if os.path.exists(s["path"]):
            os.remove(s["path"])         # AutoCAD would ask before replacing it
    code, text, seconds = run_core(args.core, os.path.abspath(drawing), expanded_path, args.timeout,
                                   os.path.join(out_dir, stem + ".run.log"), out_dir)
    problems = []
    if code is None:
        problems.append("the core did not finish in %d seconds" % args.timeout)
    for m in re.finditer(r"^(Unknown command[^\n]*|[^\n]*Invalid[^\n]*)$", text, re.M):
        problems.append(m.group(0).strip())

    log_path = os.path.join(out_dir, "fas9_facit_log.csv")
    new_log = not os.path.exists(log_path)
    first_ascii_2018 = None
    with open(log_path, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if new_log:
            w.writerow(["theme", "change", "version", "format", "path", "made", "acadver", "maintver", "sha256", "records"])
        for s in saves:
            made = os.path.exists(s["path"])
            row = [s["theme"], s["change"], s["version"], "binary" if s["binary"] else "ascii", s["file"], "yes" if made else "no",
                   "", "", "", ""]
            if made:
                try:
                    info = inv.read_file(s["path"], s["file"])
                    row[6], row[7], row[8], row[9] = info["acadver"] or "", info["maintver"] or "", info["sha256"], len(info["records"])
                    if info["acadver"] != s["acadver"]:
                        problems.append("%s: $ACADVER %s, not %s" % (s["file"], info["acadver"], s["acadver"]))
                    if (info["format"] == "binary") != s["binary"]:
                        problems.append("%s: %s, not the format asked for" % (s["file"], info["format"]))
                    if first_ascii_2018 is None and s["version"] == "2018" and not s["binary"]:
                        first_ascii_2018 = info
                except (inv.DxfError, OSError) as e:
                    problems.append("%s: not readable: %s" % (s["file"], e))
            else:
                problems.append("%s: AutoCAD did not make it" % s["file"])
            w.writerow(row)
    if expect:
        have = {r["name"] for r in first_ascii_2018["records"]} if first_ascii_2018 else set()
        for name in expect:
            if name not in have:
                problems.append("expected record %s is not in the 2018 file: not a new facit" % name)
    print("%s: %d files asked for, %d made, %.0f s%s" % (stem, len(saves), sum(os.path.exists(s["path"]) for s in saves), seconds,
                                                          ", expected " + " ".join(expect) if expect else ""))
    for pr in problems:
        print("  PROBLEM: " + pr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
