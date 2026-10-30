"""The verification batch of step 9.S (plan phase 9, "Slutsteg 9.S", point 2): AutoCAD 2025's core and ODA File Converter
read every AutoCAD original and everything HDxf wrote of it, directly, and one summary says what each tool made of each file.

  python -I tools/fas9_verify_batch.py --work <directory outside the repository> [options]

For every original (doc/acad_*_dxf.dxf, doc/facit/**/*.dxf including dyntypes/, build/test-data; the producer is decided as
tools/inventory_types.py does, PROVENANCE: another producer's file is a robustness input and is marked so everywhere):

  1. The original is copied to <work>/job/<key>/ (with the fonts, images and references it names that are found in the roots)
     and tools/hdxf_roundtrip (test/CMakeLists.txt, target hdxf_roundtrip) writes there, in the file's own version:
     roundtrip__<key>__ascii|binary.dxf (read and written unchanged) and anew__<key>__ascii|binary.dxf (every typed record
     written from its typed values alone). A file that cannot be read or written is a FINDING with HDxf's last_error().
  2. AutoCAD 2025's core (C:/Program Files/Autodesk/AutoCAD 2025/accoreconsole.exe, never DWG TrueView's) opens the original
     and every HDxf output: AUDIT with fixing (errors found and fixed are counted: exit code 0 hides them), REGEN, ZOOM E,
     the extents (EXTMIN/EXTMAX, 8 decimals), SELECT ALL (objects found), MASSPROP for a file with 3DSOLID/REGION/BODY, and in
     paper space (when the file has entities there) the same extents and count; --list adds LIST of every object. QUIT
     discards the changes. At most --core-jobs (4) cores run at a time, each with --timeout; cer_dialog.exe (the crash
     window) is closed after every run; only the processes this script started are ever stopped.
  3. ODA File Converter (C:/Program Files/ODA/ODAFileConverter 27.9.0) reads the same files directly, one run per target
     version and chunk: the target is the file's own version as DXF (audit on), its .err files and its output are counted.
     A target ODA does not write (its $ACADVER differs) is reported, never a passed version conversion.
  4. ezdxf (when installed) audits and reads each file as an independent reader.
  5. The original against each HDxf output: entity and object counts per type, the core's extents, SELECT ALL count, audit,
     MASSPROP numbers and LIST text, ODA's counts of its own output, ezdxf's entities/tables/objects (compare.tsv).

Output in --work (never in the repository): results/<key>.json (one per original: what makes --resume possible),
summary.txt, summary.tsv (a row per file), compare.tsv (a row per HDxf output), findings.txt (the FINDING lines), logs/<key>/
(the core's logs and scripts, ODA's .err, hdxf_roundtrip's output and errors), run.log. The HDxf outputs are deleted after
their job unless --keep-outputs, or the original has a finding (then they stay in job/<key>/ for analysis). The tools are
readers here, never facit: only a file AutoCAD itself saved is (CLAUDE.md, "No golden files"), and nothing this batch
writes becomes one. The originals are never touched (SHA256 before and after).

Not checked, and why (the summary repeats this): the core shows no picture, so a headless run is no visual check; surfaces
and meshes have no MASSPROP and AREA needs a pick; a core DWG/GUI view of proxy graphics is not read; the actual $ACADVER
is the file's header ($ACADVER, read and compared), the core does not tell the version it opened.

Python's standard library only (ezdxf optional, as in tools/older_dxf_batch.py, whose readers and comparisons this uses).

  --only TEXT    only originals whose path contains TEXT (repeatable)     --limit N     the first N after the filters
  --resume       skip originals that already have a result                --skip-twins  skip a binary original whose ASCII twin is in
  --acadver V    only files of that $ACADVER (AC1009 ... AC1032)                         the corpus
  --summarize-only   write the summary from the results already there
The whole corpus (build hdxf_roundtrip first: cmake --build build -j 3 --target hdxf_roundtrip; keep --work path short, the tools
refuse paths above 260 characters; about 3.5 s per original with 5 files each, so hours; --resume continues after an interruption):
  python -I tools/fas9_verify_batch.py --work <short dir outside the repository> --data-root <HDxf with doc/facit and build/test-data> --resume
A smoke run of one file:
  python -I tools/fas9_verify_batch.py --work C:/temp/fas9_verify --only acad_2018_dxf.dxf --data-root C:/path/to/HDxf
"""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inventory_types as inv  # noqa: E402
import older_dxf_batch as odb  # noqa: E402

REPO = inv.ROOT
CORE = "C:/Program Files/Autodesk/AutoCAD 2025/accoreconsole.exe"
ODA = "C:/Program Files/ODA/ODAFileConverter 27.9.0/ODAFileConverter.exe"
ROUNDTRIP = os.path.join(REPO, "build", "test", "hdxf_roundtrip.exe")
TARGET_OF = odb.ALL_TARGET_OF                      # $ACADVER -> ODA's target name
TAG_VERSION = {"r12": "AC1009", "2000": "AC1015", "2004": "AC1018", "2007": "AC1021", "2010": "AC1024", "2013": "AC1027",
               "2018": "AC1032"}
SOLID_NAMES = ("3DSOLID", "REGION", "BODY")
ROLES = ("original", "roundtrip_ascii", "roundtrip_binary", "anew_ascii", "anew_binary")
SOURCE_PREFIXES = ("roundtrip", "anew")
RESOURCE_KINDS = ("font", "image", "xref")
# the core's lines of a normal start and of the commands this script feeds it: not messages about the file
BENIGN = ("CoreHeartBeat", "Regenerating model.", "**** System Variable Changed ****", "AutoCAD menu utilities loaded.",
          "LogFilePath has been set", "Execution Path:", "Current Directory:", "Version Number:", "AutoCAD Core Engine",
          "AcCoreConsole:", "Redirect stdout", "Command:", "monitored system variables")
EXTENTS_TOLERANCE = 1e-6


def now():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def safe(text):
    return re.sub(r"[^A-Za-z0-9._-]", "_", text)


def job_id(relative):
    """The readable name of an original (its path without .dxf); it goes in the results, never in a file name."""
    stem = re.sub(r"\.dxf$", "", relative, flags=re.I).replace("\\", "/")
    return safe(stem.replace("/", "__"))


def job_key(relative):
    """The short name of an original used for its directories and files: the scratchpad's path is long already and
    the tools (the core, ODA) do not take a path above 260 characters."""
    return "j" + hashlib.sha1(relative.replace("\\", "/").encode()).hexdigest()[:9]


def sha256_of(path):
    return odb.sha256(path)


# ---------------------------------------------------------------------------
# The core's log
# ---------------------------------------------------------------------------

def core_script(has_solids, has_paper, with_list):
    c = ["_.FILEDIA", "0", "_.CMDDIA", "0", "_.SETVAR", "LUPREC", "8",
         "_.AUDIT", "_Y", "_.REGEN", "_.ZOOM", "_E",
         "_.SETVAR", "EXTMIN", "_.SETVAR", "EXTMAX", "_.SELECT", "_ALL", ""]
    if has_solids:
        c += ["_.MASSPROP", "_ALL", "", "_N"]
    if with_list:
        c += ["_.LIST", "_ALL", ""]
    if has_paper:
        c += ["_.TILEMODE", "0", "_.ZOOM", "_E", "_.SETVAR", "EXTMIN", "_.SETVAR", "EXTMAX", "_.SELECT", "_ALL", ""]
    c += ["_.QUIT", "_Y"]
    return c


def parse_core_log(text, has_solids, has_paper):
    """What the core's log says: the audit counts, the extents, the SELECT ALL counts, MASSPROP, the messages."""
    text = text.replace("\r", "").replace("\0", "")
    out = {"completed": False, "discarded": "drawing discarded" in text, "audit_errors": None, "audit_fixed": None,
           "audit_lines": [], "erased": 0, "extents": [], "found": [], "other_space": [], "mass": [], "messages": [],
           "list": None}
    m = re.search(r"Total errors found (\d+) fixed (\d+)", text)
    if m:
        out["audit_errors"], out["audit_fixed"] = int(m.group(1)), int(m.group(2))
    m = re.search(r"Erased (\d+) objects", text)
    if m:
        out["erased"] = int(m.group(1))
    a, b = text.find("Auditing Header"), text.find("Total errors found")
    if a >= 0 and b > a:
        for line in text[a:b].splitlines():
            # (what the audit repaired is printed after the progress text on the same line: "Pass 1 100 objects auditedAcDbX(24F) was repaired.")
            s = re.sub(r"^Pass \d+\s+\d+\s+objects audited", "", line.strip()).strip()
            if s and not re.match(r"(Auditing|\d+\s+(Blocks|objects) audited|Fix any errors|CoreHeartBeat)", s):
                out["audit_lines"].append(s)
    num = r"(-?[\d.]+(?:E[-+]?\d+)?)"
    mins = re.findall(r"^EXTMIN = %s,%s,%s" % (num, num, num), text, re.M)
    maxs = re.findall(r"^EXTMAX = %s,%s,%s" % (num, num, num), text, re.M)
    for lo, hi in zip(mins, maxs):
        out["extents"].append(",".join(lo) + ";" + ",".join(hi))
    sections = re.split(r"\nCommand: ", text)
    for sec in sections:
        head = sec.split("\n", 1)[0].strip()
        if head.startswith("_.SELECT"):
            f = re.search(r"(\d+) found", sec)
            o = re.search(r"(\d+) were not in current space", sec)
            out["found"].append(int(f.group(1)) if f else 0)
            out["other_space"].append(int(o.group(1)) if o else 0)
        elif head.startswith("_.MASSPROP"):
            start, end = sec.find(" ----------------"), sec.find("Write analysis")
            if start >= 0:
                body = sec[start:end] if end > start else sec[start:]
                out["mass"] = [re.sub(r"\s+", " ", l.strip()) for l in body.splitlines() if l.strip()]
        elif head.startswith("_.LIST"):
            out["list"] = list_blocks(sec)
    out["completed"] = bool(out["extents"]) and out["audit_errors"] is not None and "_.QUIT" in text
    # the file's own messages: before the first command, and any line of a problem later on
    pre = text.split("\nCommand:", 1)[0]
    for line in pre.splitlines():
        s = line.strip()
        if s and not any(s.startswith(b) or b in s for b in BENIGN):
            out["messages"].append(s)
    for line in text.splitlines():
        s = line.strip()
        if re.search(r"Invalid|invalid|error|Error|discarded|corrupt|Unknown command|cannot|Cannot|Unable|Failed|Missing|Bad handle|not found", s) \
                and s not in out["messages"] and "Total errors found" not in s and not s.startswith("Command:"):
            if re.search(r'Unknown command "_?[NY]?"', s) or "Fix any errors detected" in s:
                continue
            out["messages"].append(s)
    out["messages"] = out["messages"][:12]
    return out


def list_blocks(section):
    """LIST's blocks (one per object), normalised, without the handle line."""
    parts = re.split(r"\n(?=\s{8,}\S[^\n]*Layer: )", section)
    blocks = []
    for part in parts[1:]:
        lines = [l.strip() for l in part.splitlines() if l.strip() and not l.strip().startswith(("Handle =", "Command:"))]
        blocks.append("\n".join(lines))
    return blocks


def extents_text(entry):
    return entry.replace(";", " .. ")


def extents_equal(a, b):
    if a is None or b is None:
        return a == b
    pa = [float(x) for x in re.split(r"[;,]", a) if x]
    pb = [float(x) for x in re.split(r"[;,]", b) if x]
    return len(pa) == len(pb) and all(abs(x - y) <= EXTENTS_TOLERANCE * max(1.0, abs(x), abs(y)) for x, y in zip(pa, pb))


# ---------------------------------------------------------------------------
# Running the tools
# ---------------------------------------------------------------------------

def run(command, timeout, cwd=None):
    """(return code or None on a timeout, output text, seconds). The child is stopped on a timeout by its handle: no other process."""
    started = time.time()
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout, cwd=cwd, stdin=subprocess.DEVNULL, **odb.hidden())
        text = (done.stdout + done.stderr).decode("utf-8", "replace").replace("\0", "")
        code = done.returncode
    except subprocess.TimeoutExpired as e:
        text = ((e.stdout or b"") + (e.stderr or b"")).decode("utf-8", "replace").replace("\0", "") + "\n[TIMEOUT]\n"
        code = None
    except OSError as e:
        text = "[cannot start: %s]" % e
        code = -1
    return code, text, time.time() - started


def close_crash_dialogs():
    """A core that crashes leaves Autodesk's error report window open: only cer_dialog.exe is closed, never a core."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", "cer_dialog.exe"], capture_output=True, stdin=subprocess.DEVNULL)


def run_core(core, directory, name, has_solids, has_paper, with_list, timeout):
    path = os.path.join(directory, name)
    base = os.path.splitext(name)[0]
    script = os.path.join(directory, base + ".scr")
    with open(script, "w", encoding="ascii", newline="") as f:
        f.write("\r\n".join(core_script(has_solids, has_paper, with_list)) + "\r\n")
    code, text, seconds = run([core, "/i", os.path.abspath(path), "/s", os.path.abspath(script), "/l", "en-US"], timeout, cwd=directory)
    close_crash_dialogs()
    with open(os.path.join(directory, base + ".core.log"), "w", encoding="utf-8") as f:
        f.write(text)
    result = parse_core_log(text, has_solids, has_paper)
    result.update({"exit": code, "seconds": round(seconds, 1), "timeout": code is None, "script": " ".join(core_script(has_solids, has_paper, with_list))})
    if not result["completed"]:
        result["tail"] = " | ".join(l.strip() for l in text.splitlines() if l.strip())[-300:]
    return result


# ---------------------------------------------------------------------------
# The corpus
# ---------------------------------------------------------------------------

def corpus(data_root):
    roots = [(os.path.join(data_root, "build", "test-data"), ""),
             (os.path.join(data_root, "doc", "facit"), "doc/facit/"),
             (os.path.join(data_root, "doc"), "doc/")]
    return roots, inv.collect(roots)


def counts_of(info):
    """Entities (ENTITIES and BLOCKS, the structure records left out) and objects per type."""
    c = collections.Counter()
    for r in info["records"]:
        if r["name"] in inv.STRUCTURAL:
            continue
        c[("O:" if r["section"] == "OBJECTS" else "E:") + r["name"]] += 1
    return dict(c)


def read_info(path, relative):
    """inventory_types' reading of a file: ($ACADVER, format, counts, solids in ENTITIES, error)."""
    try:
        info = inv.read_file(path, relative)
    except (inv.DxfError, UnicodeDecodeError, OSError, IndexError, struct_error()) as e:
        return {"acadver": "", "format": "", "counts": {}, "solids": 0, "error": "%s: %s" % (type(e).__name__, e), "classes": 0}
    solids = sum(1 for r in info["records"] if r["section"] == "ENTITIES" and r["name"] in SOLID_NAMES)
    return {"acadver": info["acadver"] or "", "format": info["format"], "counts": counts_of(info), "solids": solids, "error": "",
            "classes": len(info["classes"])}


def struct_error():
    import struct
    return struct.error


def paper_entities(path):
    s = odb.summarize(path)
    return s["paper"], s["error"]


def expected_acadver(relative):
    m = re.match(r"(?:doc/facit/(?:.*/)?|doc/)?acad_(r12|\d{4})_dxf", relative)
    return TAG_VERSION.get(m.group(1), "") if m else ""


def resources_for(path, found, extra_dirs):
    """The files the drawing names (fonts, images, references) that are found in the roots or next to it: {name: source}."""
    refs = odb.references_of(path)
    out = {}
    for kind, names in refs.items():
        for name in names:
            base = os.path.basename(name.replace("\\", "/"))
            candidates = [base] if os.path.splitext(base)[1] else [base + ".shx", base + ".ttf", base + ".dwg"]
            if kind == "xref":
                candidates.append(base + ".dwg")
            for c in candidates:
                local = [os.path.join(d, c) for d in extra_dirs if os.path.exists(os.path.join(d, c))]
                source = local[0] if local else found.get(c.lower())
                if source:
                    out[c] = source
                    break
    return out


# ---------------------------------------------------------------------------
# One chunk of originals
# ---------------------------------------------------------------------------

class Job:
    def __init__(self, source, relative):
        self.source = source
        self.relative = relative
        self.id = job_id(relative)
        self.key = job_key(relative)
        self.producer, self.producer_reason = inv.provenance(relative)
        self.kind = "autocad" if inv.autocad(self.producer) else "robustness"
        self.files = {}          # role -> result dict
        self.findings = []
        self.compare = []
        self.hdxf = {"exit": None, "seconds": 0, "lines": []}

    def finding(self, text):
        self.findings.append("FINDING [%s %s] %s: %s" % (self.kind, self.producer, self.relative, text))

    def role_name(self, role):
        if role == "original":
            return self.key + ".dxf"
        kind, fmt = role.split("_")
        return "%s__%s__%s.dxf" % (kind, self.key, fmt)


def prepare(job, work, found_resources):
    job.dir = os.path.join(work, "job", job.key)
    shutil.rmtree(job.dir, ignore_errors=True)
    os.makedirs(job.dir)
    job.sha = sha256_of(job.source)
    shutil.copyfile(job.source, os.path.join(job.dir, job.role_name("original")))
    job.resources = resources_for(job.source, found_resources, [os.path.dirname(job.source)])
    for name, source in job.resources.items():
        shutil.copyfile(source, os.path.join(job.dir, name))


def roundtrip(job, exe, timeout):
    code, text, seconds = run([os.path.abspath(exe), os.path.join(job.dir, job.role_name("original")), job.dir], timeout)
    job.hdxf = {"exit": code, "seconds": round(seconds, 1), "lines": [l for l in text.splitlines() if l.startswith(("FAIL", "WARN"))][:40]}
    if code is None:
        job.finding("hdxf_roundtrip did not finish in %d s" % timeout)
    elif code not in (0, 1):
        job.finding("hdxf_roundtrip stopped with exit %s: %s" % (code, " | ".join(text.splitlines()[-3:])))
    for line in job.hdxf["lines"]:
        if line.startswith("FAIL"):
            job.finding("HDxf: " + re.sub(r"^j[0-9a-f]{9}\.dxf: ", "", line[5:]))


def file_entry(job, role, path_dir):
    path = os.path.join(path_dir, job.role_name(role))
    e = {"role": role, "name": job.role_name(role), "exists": os.path.exists(path)}
    if not e["exists"]:
        return e
    e["size"] = os.path.getsize(path)
    e["sha256"] = sha256_of(path)
    info = read_info(path, job.relative if role == "original" else job.role_name(role))
    e["acadver"], e["format"], e["counts"], e["solids"], e["parse_error"] = info["acadver"], info["format"], info["counts"], info["solids"], info["error"]
    e["paper"], _ = paper_entities(path)
    return e


def process_chunk(jobs, args, work, found_resources, log):
    clock = {"start": time.time()}
    for job in jobs:
        prepare(job, work, found_resources)
    # 1. HDxf
    if not args.skip_roundtrip:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(lambda j: roundtrip(j, args.roundtrip, args.roundtrip_timeout), jobs))
    for job in jobs:
        for role in ROLES:
            job.files[role] = file_entry(job, role, job.dir)
            if role != "original" and not job.files[role]["exists"] and not args.skip_roundtrip:
                said = role.replace("_", " ")
                if not any(l.startswith("FAIL") and said in l for l in job.hdxf["lines"]):
                    job.finding("HDxf wrote no %s file (exit %s)" % (role, job.hdxf["exit"]))
        orig = job.files["original"]
        if orig.get("parse_error"):
            job.finding("the original cannot be read by inventory_types: %s" % orig["parse_error"])
        want = expected_acadver(job.relative)
        if want and orig["acadver"] != want:
            job.finding("the original's $ACADVER is %s, its name says %s" % (orig["acadver"] or "none", want))
        for role in ROLES[1:]:
            e = job.files[role]
            if e["exists"] and e["acadver"] != orig["acadver"]:
                job.finding("%s is $ACADVER %s, the original's is %s" % (role, e["acadver"] or "none", orig["acadver"] or "none"))
            if e["exists"] and e["format"] != ("binary" if role.endswith("binary") else "ascii"):
                job.finding("%s is %s, not the format asked for" % (role, e["format"]))
    clock["hdxf"] = time.time()
    # 2. AutoCAD's core
    if not args.skip_core:
        tasks = [(j, r) for j in jobs for r in ROLES if j.files[r]["exists"]]

        def one(task):
            j, r = task
            e = j.files[r]
            return task, run_core(args.core, j.dir, e["name"], bool(j.files["original"]["solids"]), bool(j.files["original"]["paper"]),
                                  args.list, args.timeout)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.core_jobs) as pool:
            for (j, r), result in pool.map(one, tasks):
                j.files[r]["core"] = result
    clock["core"] = time.time()
    # 3. ODA File Converter, a run per target version
    if not args.skip_oda and os.path.exists(args.oda):
        by_target = collections.defaultdict(list)
        for j in jobs:
            for r in ROLES:
                e = j.files[r]
                if e["exists"] and e["acadver"] in TARGET_OF:
                    by_target[TARGET_OF[e["acadver"]]].append((j, r))
                elif e["exists"]:
                    j.finding("%s: no ODA target for $ACADVER %s" % (r, e["acadver"] or "none"))
        for target, members in sorted(by_target.items()):
            src = os.path.join(work, "oda_in", target)
            dst = os.path.join(work, "oda_out", target)
            for d in (src, dst):
                shutil.rmtree(d, ignore_errors=True)
                os.makedirs(d)
            for j, r in members:
                shutil.copyfile(os.path.join(j.dir, j.files[r]["name"]), os.path.join(src, j.files[r]["name"]))
                for name in j.resources:
                    if not os.path.exists(os.path.join(src, name)):
                        shutil.copyfile(os.path.join(j.dir, name), os.path.join(src, name))
            code, text, seconds = run([args.oda, src, dst, target, "DXF", "0", "1", "*.dxf"], 120 + 40 * len(members))
            made = [n for n in os.listdir(dst) if n.lower().endswith(".dxf")]
            log("ODA %s: %d in, %d out, exit %s, %.0f s" % (target, len(members), len(made), code, seconds))
            for j, r in members:
                e = j.files[r]
                stem = os.path.splitext(e["name"])[0]
                out_file = os.path.join(dst, e["name"])
                errs = [n for n in os.listdir(dst) if n.startswith(stem + ".") and n.lower().endswith(".err")]
                oda = {"target": target, "made": os.path.exists(out_file), "exit": code, "err": [], "seconds": round(seconds / max(1, len(members)), 2)}
                for n in errs:
                    with open(os.path.join(dst, n), encoding="utf-8", errors="replace") as f:
                        oda["err"].append(f.read(1500).strip().replace("\n", " | "))
                    shutil.copyfile(os.path.join(dst, n), os.path.join(j.dir, n))
                if oda["made"]:
                    info = read_info(out_file, "oda/" + e["name"])
                    oda["acadver"], oda["counts"], oda["parse_error"] = info["acadver"], info["counts"], info["error"]
                    oda["entities"] = sum(v for k, v in info["counts"].items() if k.startswith("E:"))
                    oda["objects"] = sum(v for k, v in info["counts"].items() if k.startswith("O:"))
                e["oda"] = oda
    clock["oda"] = time.time()
    # 4. ezdxf
    ez = {}
    if not args.skip_ezdxf:
        for j in jobs:
            for r in ROLES:
                e = j.files[r]
                if e["exists"]:
                    res = odb.ezdxf_read(os.path.join(j.dir, e["name"]))
                    ez[(j.key, r)] = res
                    e["ezdxf"] = {k: res.get(k) for k in ("available", "ok", "mode", "message", "loaded", "version", "entities", "audit_errors", "audit_fixes")}
                    e["ezdxf"]["audit_text"] = res.get("audit_text", [])[:6]
                    e["ezdxf"]["objects"] = dict(res.get("objects", {})) if res.get("objects") else {}
    clock["ezdxf"] = time.time()
    # 5. the findings and the comparison
    for j in jobs:
        judge(j, ez)
    for j in jobs:
        sha_after = sha256_of(j.source)
        if sha_after != j.sha:
            raise SystemExit("ORIGINAL CHANGED: %s" % j.source)
    log("phases (s): copy+HDxf %.0f, core %.0f, ODA %.0f, ezdxf %.0f, compare %.0f for %d originals" % (
        clock["hdxf"] - clock["start"], clock["core"] - clock["hdxf"], clock["oda"] - clock["core"], clock["ezdxf"] - clock["oda"],
        time.time() - clock["ezdxf"], len(jobs)))
    return jobs


def core_findings(job, role, e, baseline):
    """What the core made of a file. The original's audit is reported as AutoCAD's own; an output's only when it is not what the
    original's was (a finding of the output's own: the same errors and fixes as the original's are the original's)."""
    c = e.get("core")
    if not c:
        return
    who = "the original" if role == "original" else role
    if c["timeout"]:
        job.finding("core: %s timed out after %s s" % (who, c["seconds"]))
    elif not c["completed"]:
        job.finding("core: %s was not opened/completed%s (exit %s): %s" % (who, " (drawing discarded)" if c["discarded"] else "", c["exit"], c.get("tail", "")))
    if baseline is not None and baseline.get("completed") and (c["audit_errors"], c["audit_fixed"], c["erased"]) ==             (baseline["audit_errors"], baseline["audit_fixed"], baseline["erased"]):
        return
    if c["audit_errors"] or c["audit_fixed"] or c["erased"]:
        job.finding("core AUDIT of %s: %s errors found, %s fixed, %d erased%s" % (
            who, c["audit_errors"], c["audit_fixed"], c["erased"], (" [" + " | ".join(c["audit_lines"][:4]) + "]") if c["audit_lines"] else ""))


def zero_value(value):
    if isinstance(value, tuple):
        return all(zero_value(v) for v in value)
    return value in (0, 0.0, "0", "0.0")


def without_zero_groups(dump):
    """The entities with every attribute whose value is a zero left out (a group written at its default value)."""
    def clean(attribs):
        return tuple(a for a in attribs if not zero_value(a[1]))
    return [(e[0], e[1], clean(e[2]), e[3], tuple(clean(c) for c in e[4])) for e in dump]


def zero_group_names(original, output):
    """The names of the attributes the output has at a zero where the original does not have them at all, by entity type."""
    names = set()
    a = collections.Counter((e[0], e[1], e[2], e[4]) for e in original["dump"])
    b = collections.Counter((e[0], e[1], e[2], e[4]) for e in output["dump"])
    gone, new = sorted((a - b).elements(), key=lambda e: e[1]), sorted((b - a).elements(), key=lambda e: e[1])
    for x, y in zip(gone, new):
        da, db = dict(x[2]), dict(y[2])
        for k in set(da) | set(db):
            if da.get(k) != db.get(k) and zero_value(db.get(k, da.get(k))):
                names.add("%s.%s" % (y[1], k))
        for ca, cb in zip(x[3], y[3]):
            ca, cb = dict(ca), dict(cb)
            for k in set(ca) | set(cb):
                if ca.get(k) != cb.get(k) and zero_value(cb.get(k, ca.get(k))):
                    names.add("%s.child.%s" % (y[1], k))
    return sorted(names)


def ez_compare(zo, ze, kind):
    """odb.compare_ezdxf's differences; when only groups at a zero are the difference (AutoCAD omits a group at its default, HDxf
    writes it) that is one line naming them, not a difference of content."""
    diffs = odb.compare_ezdxf(zo, ze, kind)
    if not diffs:
        return []
    zo2, ze2 = dict(zo, dump=without_zero_groups(zo["dump"])), dict(ze, dump=without_zero_groups(ze["dump"]))
    diffs2 = odb.compare_ezdxf(zo2, ze2, kind)
    out = ["ezdxf " + d for d in diffs2]
    if len(diffs2) < len(diffs) or diffs2 != diffs:
        names = zero_group_names(zo, ze)
        out.append("ezdxf: groups written at their zero/default value where the original has none: %s" % (", ".join(names) or "(see ezdxf dump)"))
    return out


def judge(job, ez):
    orig = job.files["original"]
    for role in ROLES:
        e = job.files[role]
        if not e["exists"]:
            continue
        core_findings(job, role, e, None if role == "original" else orig.get("core"))
        oda = e.get("oda")
        who = "the original" if role == "original" else role
        if "oda" in e:
            if not oda["made"]:
                job.finding("ODA made no DXF of %s" % who)
            if oda["err"]:
                job.finding("ODA .err for %s: %s" % (who, " ## ".join(oda["err"])[:400]))
            if oda["made"] and oda.get("acadver") != oda["target"] and TARGET_OF.get(oda.get("acadver", "")) != oda["target"]:
                job.finding("ODA wrote $ACADVER %s of %s, the target was %s (not a passed version conversion)" % (oda.get("acadver"), who, oda["target"]))
        z = e.get("ezdxf")
        if z and z["available"]:
            if not z["ok"]:
                job.finding("ezdxf could not read %s: %s" % (who, z["message"]))
            elif z["audit_errors"] or z["audit_fixes"]:
                zo = orig.get("ezdxf")
                if role == "original" or not (zo and zo["ok"] and (zo["audit_errors"], zo["audit_fixes"]) == (z["audit_errors"], z["audit_fixes"])):
                    job.finding("ezdxf audit of %s: %d errors, %d fixes %s" % (who, z["audit_errors"], z["audit_fixes"], z["audit_text"][:2]))
    # original against each output
    for role in ROLES[1:]:
        e = job.files[role]
        if not e["exists"]:
            continue
        diffs = []
        if orig["counts"] != e["counts"]:
            keys = sorted(set(orig["counts"]) | set(e["counts"]))
            changed = ["%s %d>%d" % (k, orig["counts"].get(k, 0), e["counts"].get(k, 0)) for k in keys if orig["counts"].get(k, 0) != e["counts"].get(k, 0)]
            diffs.append("record counts: " + ", ".join(changed[:8]) + (" ..." if len(changed) > 8 else ""))
        co, ce = orig.get("core"), e.get("core")
        if co and ce and co["completed"] and ce["completed"]:
            for i, space in enumerate(("model", "paper")):
                a = co["extents"][i] if i < len(co["extents"]) else None
                b = ce["extents"][i] if i < len(ce["extents"]) else None
                if not extents_equal(a, b):
                    diffs.append("core %s extents: %s | %s" % (space, a and extents_text(a), b and extents_text(b)))
                fa = co["found"][i] if i < len(co["found"]) else None
                fb = ce["found"][i] if i < len(ce["found"]) else None
                if fa != fb:
                    diffs.append("core SELECT ALL %s: %s | %s" % (space, fa, fb))
            if co["audit_errors"] != ce["audit_errors"] or co["audit_fixed"] != ce["audit_fixed"]:
                diffs.append("core AUDIT: %s/%s | %s/%s" % (co["audit_errors"], co["audit_fixed"], ce["audit_errors"], ce["audit_fixed"]))
            if len(co["mass"]) != len(ce["mass"]) or not all(odb.numbers_close(x, y) for x, y in zip(co["mass"], ce["mass"])):
                diffs.append("core MASSPROP differs: %s | %s" % (co["mass"][:3], ce["mass"][:3]))
            new_messages = [m for m in ce["messages"] if m not in co["messages"]]
            if new_messages:
                diffs.append("core messages only in the output: " + " | ".join(new_messages[:3]))
            if co["list"] is not None and ce["list"] is not None:
                gone, new = odb.multiset_diff(co["list"], ce["list"])
                if gone or new:
                    diffs.append("core LIST: only in original %d, only in output %d" % (len(gone), len(new)))
        elif co and ce and co["completed"] != ce["completed"]:
            diffs.append("core: original %s, output %s" % ("opened" if co["completed"] else "NOT opened", "opened" if ce["completed"] else "NOT opened"))
        oo, oe = orig.get("oda"), e.get("oda")
        if oo and oe and oo["made"] and oe["made"]:
            if oo["counts"] != oe["counts"]:
                keys = sorted(set(oo["counts"]) | set(oe["counts"]))
                changed = ["%s %d>%d" % (k, oo["counts"].get(k, 0), oe["counts"].get(k, 0)) for k in keys if oo["counts"].get(k, 0) != oe["counts"].get(k, 0)]
                diffs.append("ODA's output record counts: " + ", ".join(changed[:8]) + (" ..." if len(changed) > 8 else ""))
        zo, ze = ez.get((job.key, "original")), ez.get((job.key, role))
        if zo and ze and zo.get("ok") and ze.get("ok"):
            kind = "roundtrip" if role.startswith("roundtrip") else "anew"
            diffs += ez_compare(zo, ze, kind)
        elif zo and ze and zo.get("available") and zo.get("ok") != ze.get("ok"):
            diffs.append("ezdxf: original %s, output %s" % ("ok" if zo.get("ok") else "FAILED", "ok" if ze.get("ok") else "FAILED"))
        job.compare.append({"role": role, "differences": diffs})
        for d in diffs:
            job.finding("%s against the original: %s" % (role, d))


# ---------------------------------------------------------------------------
# Results and summary
# ---------------------------------------------------------------------------

def result_path(work, key):
    return os.path.join(work, "results", key + ".json")


def save_result(work, job):
    data = {"id": job.id, "key": job.key, "relative": job.relative, "producer": job.producer, "kind": job.kind, "sha256": job.sha, "files": job.files,
            "findings": job.findings, "compare": job.compare, "hdxf": job.hdxf, "finished": now()}
    with open(result_path(work, job.key), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, sort_keys=True)


def finish_job(work, job, args):
    save_result(work, job)
    logs = os.path.join(work, "logs", job.key)
    os.makedirs(logs, exist_ok=True)
    for name in os.listdir(job.dir):
        if name.endswith((".core.log", ".scr", ".err", "roundtrip_errors.txt")):
            shutil.move(os.path.join(job.dir, name), os.path.join(logs, name))
    with open(os.path.join(logs, "hdxf_roundtrip.txt"), "w", encoding="utf-8") as f:
        f.write("exit %s, %s s\n%s\n" % (job.hdxf["exit"], job.hdxf["seconds"], "\n".join(job.hdxf["lines"])))
    if args.keep_outputs or job.findings:
        pass
    else:
        shutil.rmtree(job.dir, ignore_errors=True)


def load_results(work):
    out = []
    folder = os.path.join(work, "results")
    if os.path.isdir(folder):
        for name in sorted(os.listdir(folder)):
            if name.endswith(".json"):
                with open(os.path.join(folder, name), encoding="utf-8") as f:
                    out.append(json.load(f))
    return out


def cell(x):
    return "" if x is None else str(x)


def write_summary(work, results, started, log_lines, args):
    header = ["original", "role", "producer", "kind", "ACADVER", "format", "bytes", "entities", "objects", "core", "core s", "audit errors", "audit fixed",
              "model extents", "paper extents", "SELECT ALL model", "SELECT ALL paper", "MASSPROP lines", "core messages", "ODA", "ODA ACADVER",
              "ODA entities", "ODA objects", "ODA .err", "ezdxf", "ezdxf audit errors/fixes", "ezdxf entities", "differences"]
    rows = []
    for r in results:
        diffs = {c["role"]: len(c["differences"]) for c in r["compare"]}
        for role in ROLES:
            e = r["files"].get(role, {})
            if not e.get("exists"):
                rows.append([r["relative"], role, r["producer"], r["kind"], "", "", "", "", "", "MISSING"] + [""] * 17 + [""])
                continue
            c, o, z = e.get("core"), e.get("oda"), e.get("ezdxf")
            ent = sum(v for k, v in e["counts"].items() if k.startswith("E:"))
            obj = sum(v for k, v in e["counts"].items() if k.startswith("O:"))
            status = "-" if not c else ("TIMEOUT" if c["timeout"] else ("DISCARDED" if c["discarded"] else ("ok" if c["completed"] else "NOT-COMPLETED")))
            rows.append([r["relative"], role, r["producer"], r["kind"], e["acadver"], e["format"], e["size"], ent, obj, status,
                         c["seconds"] if c else "", cell(c["audit_errors"]) if c else "", cell(c["audit_fixed"]) if c else "",
                         (c["extents"][0] if c and c["extents"] else ""), (c["extents"][1] if c and len(c["extents"]) > 1 else ""),
                         (c["found"][0] if c and c["found"] else ""), (c["found"][1] if c and len(c["found"]) > 1 else ""),
                         len(c["mass"]) if c else "", len(c["messages"]) if c else "",
                         ("made" if o["made"] else "NONE") if o else "-", o.get("acadver", "") if o else "",
                         o.get("entities", "") if o else "", o.get("objects", "") if o else "", len(o["err"]) if o else "",
                         (("ok" if z["ok"] else "FAILED") + ("/recover" if z.get("mode") == "recover" else "")) if z and z.get("available") else "-",
                         ("%s/%s" % (z["audit_errors"], z["audit_fixes"])) if z and z.get("ok") else "",
                         z.get("entities", "") if z and z.get("ok") else "",
                         diffs.get(role, "") if role != "original" else ""])
    with open(os.path.join(work, "summary.tsv"), "w", encoding="utf-8", newline="") as f:
        f.write("\t".join(header) + "\n")
        for row in rows:
            f.write("\t".join(cell(c).replace("\t", " ").replace("\n", " ") for c in row) + "\n")
    with open(os.path.join(work, "compare.tsv"), "w", encoding="utf-8", newline="") as f:
        f.write("original\tproducer\tkind\trole\tdifferences\tdetail\n")
        for r in results:
            for c in r["compare"]:
                f.write("\t".join([r["relative"], r["producer"], r["kind"], c["role"], str(len(c["differences"])), " ;; ".join(c["differences"])]) + "\n")
    findings = [x for r in results for x in r["findings"]]
    with open(os.path.join(work, "findings.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(findings) + ("\n" if findings else ""))

    lines = []
    say = lines.append
    say("fas9_verify_batch: %d originals with a result (%s)" % (len(results), now()))
    say("producers: " + ", ".join("%s %d" % kv for kv in sorted(collections.Counter(r["producer"] for r in results).items())))
    say("kinds: AutoCAD's own files %d, other producers (robustness inputs) %d" % (sum(r["kind"] == "autocad" for r in results),
                                                                                    sum(r["kind"] == "robustness" for r in results)))
    files = [(r, role, r["files"][role]) for r in results for role in ROLES if r["files"].get(role, {}).get("exists")]
    say("files read by the tools: %d (%d originals, %d HDxf outputs); HDxf outputs missing: %d" % (
        len(files), sum(1 for f in files if f[1] == "original"), sum(1 for f in files if f[1] != "original"),
        sum(1 for r in results for role in ROLES[1:] if not r["files"].get(role, {}).get("exists"))))
    by_version = collections.Counter((f[2]["acadver"], f[2]["format"]) for f in files if f[1] == "original")
    say("originals per $ACADVER and format: " + ", ".join("%s %s %d" % (k[0], k[1], v) for k, v in sorted(by_version.items())))
    cores = [f[2]["core"] for f in files if f[2].get("core")]
    if cores:
        times = [c["seconds"] for c in cores]
        say("core: %d runs, %d completed, %d not completed, %d discarded, %d timeouts; with audit errors %d, with fixes %d; seconds per file: mean %.1f, max %.1f" % (
            len(cores), sum(c["completed"] for c in cores), sum(not c["completed"] for c in cores), sum(c["discarded"] for c in cores),
            sum(c["timeout"] for c in cores), sum(1 for c in cores if c["audit_errors"]), sum(1 for c in cores if c["audit_fixed"]),
            sum(times) / len(times), max(times)))
        for role in ROLES:
            sub = [f[2]["core"] for f in files if f[1] == role and f[2].get("core")]
            if sub:
                say("  core %-17s %d runs, %d completed, audit errors in %d, fixes in %d" % (role, len(sub), sum(c["completed"] for c in sub),
                                                                                           sum(1 for c in sub if c["audit_errors"]), sum(1 for c in sub if c["audit_fixed"])))
    odas = [f[2]["oda"] for f in files if f[2].get("oda")]
    if odas:
        say("ODA: %d files, %d with a DXF out, %d with a .err" % (len(odas), sum(o["made"] for o in odas), sum(1 for o in odas if o["err"])))
    ezs = [f[2]["ezdxf"] for f in files if f[2].get("ezdxf") and f[2]["ezdxf"].get("available")]
    if ezs:
        say("ezdxf: %d files, %d not read, %d recovery mode, %d with audit errors, %d with audit fixes" % (
            len(ezs), sum(not z["ok"] for z in ezs), sum(z.get("mode") == "recover" for z in ezs), sum(1 for z in ezs if z.get("audit_errors")),
            sum(1 for z in ezs if z.get("audit_fixes"))))
    compared = [(r, c) for r in results for c in r["compare"]]
    say("HDxf outputs compared with their original: %d, with a difference: %d" % (len(compared), sum(1 for r, c in compared if c["differences"])))
    for role in ROLES[1:]:
        sub = [c for r, c in compared if c["role"] == role]
        say("  %-17s %d compared, %d with a difference" % (role, len(sub), sum(1 for c in sub if c["differences"])))
    hd = [r["hdxf"]["seconds"] for r in results if r["hdxf"].get("seconds")]
    if hd:
        say("hdxf_roundtrip: mean %.1f s per original (4 files), max %.1f s" % (sum(hd) / len(hd), max(hd)))
    say("")
    say("FINDINGS: %d (AutoCAD's own originals: %d, robustness inputs: %d); originals without a finding: %d of %d" % (
        len(findings), sum(len(r["findings"]) for r in results if r["kind"] == "autocad"),
        sum(len(r["findings"]) for r in results if r["kind"] == "robustness"), sum(1 for r in results if not r["findings"]), len(results)))
    kinds = collections.Counter()
    for r in results:
        for x in r["findings"]:
            kinds[(r["kind"], re.sub(r"\d+", "N", x.split(": ", 2)[-1])[:70])] += 1
    for (kind, text), n in kinds.most_common(40):
        say("  %4d x [%s] %s" % (n, kind, text))
    say("")
    say("every FINDING line is in findings.txt; per file in results/<key>.json, logs in logs/<key>/ (the key is in the result)")
    say("Not checked: a visual check (the core draws nothing), MASSPROP/AREA of surfaces and meshes (no command without a pick), proxy "
        "graphics as the GUI draws them, the version the core opened (the file's $ACADVER is compared instead)")
    say("started %s, summary written %s" % (started, now()))
    with open(os.path.join(work, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return lines


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def self_test():
    log = """Version Number: V.72.0.0 (UNICODE)
CoreHeartBeat
Regenerating model.
Substituting [simplex.shx] for [x.shx].
Command: _.AUDIT
Auditing Header
Auditing Entities Pass 1
Pass 1 100     objects audited
Invalid something at 1F
Total errors found 1 fixed 1
Erased 0 objects
Command: _.SETVAR
Enter variable name or [?] <CMDDIA>: EXTMIN

EXTMIN = -1.00000000,2.00000000,0.00000000 (read only)
Command: _.SETVAR
Enter variable name or [?] <EXTMIN>: EXTMAX

EXTMAX = 5.00000000,6.00000000,0.00000000 (read only)
Command: _.SELECT

Select objects: _ALL
32 found
5 were not in current space.

Select objects:

Command: _.MASSPROP

Select objects: _ALL
2 found
 ----------------   SOLIDS    ----------------

Mass:                    10.5
Volume:                  10.5

Write analysis to a file? [Yes/No] <N>: _N

Command: _.QUIT
_Y
"""
    r = parse_core_log(log, True, False)
    checks = [(r["completed"], "completed"), (r["audit_errors"] == 1 and r["audit_fixed"] == 1, "audit"),
              (r["extents"] == ["-1.00000000,2.00000000,0.00000000;5.00000000,6.00000000,0.00000000"], "extents %r" % r["extents"]),
              (r["found"] == [32] and r["other_space"] == [5], "select %r" % r["found"]),
              (r["mass"][1] == "Mass: 10.5", "mass %r" % r["mass"]),
              (any("Substituting" in m for m in r["messages"]), "messages %r" % r["messages"]),
              (any("Invalid" in m for m in r["messages"]), "problem line"),
              (not r["discarded"], "not discarded"),
              (job_id("doc/facit/dyntypes/Civil_-_Metric/acad_2018_dxf_dyntypes.dxf").startswith("doc__facit__dyntypes"), "id"),
              (expected_acadver("doc/facit/acad_r12_dxf_x.dxf") == "AC1009" and expected_acadver("doc/acad_2018_dxf.dxf") == "AC1032"
               and expected_acadver("acadsharp/x.dxf") == "", "expected version"),
              (extents_equal("0,0,0;1,1,1", "0,0,0;1.0000001,1,1") and not extents_equal("0,0,0;1,1,1", "0,0,0;2,1,1"), "extents_equal")]
    bad = [m for ok, m in checks if not ok]
    if bad:
        raise SystemExit("self-test failed: " + "; ".join(bad))
    print("fas9_verify_batch self-test: %d checks passed" % len(checks))
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--work", required=True, help="the directory the batch works in (outside the repository)")
    p.add_argument("--data-root", default=REPO, help="the HDxf directory that holds build/test-data, doc/facit and doc/acad_*_dxf.dxf "
                   "(default: this repository)")
    p.add_argument("--roundtrip", default=ROUNDTRIP, help="hdxf_roundtrip.exe")
    p.add_argument("--core", default=CORE)
    p.add_argument("--oda", default=ODA)
    p.add_argument("--only", action="append", help="only originals whose path contains this (repeatable)")
    p.add_argument("--acadver", help="only originals of this $ACADVER")
    p.add_argument("--limit", type=int)
    p.add_argument("--resume", action="store_true")
    p.add_argument("--summarize-only", action="store_true")
    p.add_argument("--skip-twins", action="store_true", help="skip a binary original whose ASCII twin is in the corpus")
    p.add_argument("--chunk", type=int, default=20, help="originals per round (the unit --resume works in)")
    p.add_argument("--core-jobs", type=int, default=4, help="cores at a time (at most 4)")
    p.add_argument("--timeout", type=int, default=240, help="seconds per core run")
    p.add_argument("--roundtrip-timeout", type=int, default=900)
    p.add_argument("--list", action="store_true", help="the core also LISTs every object (compared with the original's)")
    p.add_argument("--keep-outputs", action="store_true")
    p.add_argument("--skip-roundtrip", action="store_true", help="use the HDxf files already in job/<key>/")
    p.add_argument("--skip-core", action="store_true")
    p.add_argument("--skip-oda", action="store_true")
    p.add_argument("--skip-ezdxf", action="store_true")
    args = p.parse_args()
    args.core_jobs = max(1, min(4, args.core_jobs))

    work = os.path.abspath(args.work)
    work_real = os.path.realpath(work)
    if work_real == REPO or work_real.startswith(REPO + os.sep) or work_real.startswith(os.path.realpath(args.data_root) + os.sep):
        sys.exit("--work must be outside the repository and the data root")
    for d in ("results", "logs", "job"):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    started = now()
    log_lines = []
    with open(os.path.join(work, "run.log"), "a", encoding="utf-8") as f:
        f.write("%s command: %s\n" % (started, " ".join([sys.executable] + sys.argv)))

    def log(text):
        print(text, flush=True)
        with open(os.path.join(work, "run.log"), "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (time.strftime("%H:%M:%S"), text))

    if args.summarize_only:
        results = load_results(work)
        for line in write_summary(work, results, started, log_lines, args)[:12]:
            print(line)
        return 0

    for what, path in (("hdxf_roundtrip", args.roundtrip), ("AutoCAD's core", args.core), ("ODA File Converter", args.oda)):
        skipped = (what == "AutoCAD's core" and args.skip_core) or (what == "ODA File Converter" and args.skip_oda) or \
                  (what == "hdxf_roundtrip" and args.skip_roundtrip)
        if not skipped and not os.path.exists(path):
            sys.exit("%s is not at %s" % (what, path))

    roots, files = corpus(args.data_root)
    found_resources = odb.collect_resources([r for r, _ in roots], args.data_root, [])
    jobs, duplicates, unreadable = [], [], []
    seen = {}
    names = {relative for _, relative in files}
    for full, relative in files:
        if args.only and not any(o in relative for o in args.only):
            continue
        if args.skip_twins:
            twin = inv.twin_of(relative)
            if twin and twin in names and relative.endswith(("_bin.dxf", "_binary.dxf")):
                continue
        digest = sha256_of(full)
        if digest in seen:
            duplicates.append((relative, seen[digest]))
            continue
        seen[digest] = relative
        job = Job(full, relative)
        if args.acadver:
            v = read_info(full, relative)["acadver"]
            if v != args.acadver:
                continue
        jobs.append(job)
    if args.limit:
        jobs = jobs[:args.limit]
    todo = [j for j in jobs if not (args.resume and os.path.exists(result_path(work, j.key)))]
    log("corpus: %d files found in %s, %d selected (%d duplicates by content skipped), %d to do%s" % (
        len(files), ", ".join(r for r, _ in roots), len(jobs), len(duplicates), len(todo), ", resuming" if args.resume else ""))
    for dup, same in duplicates[:20]:
        log("  duplicate content: %s = %s" % (dup, same))
    started_run = time.time()
    done = 0
    for i in range(0, len(todo), args.chunk):
        chunk = todo[i:i + args.chunk]
        t = time.time()
        process_chunk(chunk, args, work, found_resources, log)
        for j in chunk:
            finish_job(work, j, args)
            for line in j.findings:
                log(line)
        done += len(chunk)
        elapsed = time.time() - started_run
        log("chunk of %d done in %.0f s; %d of %d originals, %.1f s per original, about %.0f min left" % (
            len(chunk), time.time() - t, done, len(todo), elapsed / done, (len(todo) - done) * elapsed / done / 60))
        write_summary(work, load_results(work), started, log_lines, args)
    results = load_results(work)
    for line in write_summary(work, results, started, log_lines, args):
        log(line)
    for d in ("oda_in", "oda_out"):
        shutil.rmtree(os.path.join(work, d), ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
