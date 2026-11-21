"""Everything the web server needs from the C engine, kept apart from the HTTP layer so it can be tested alone.

  parse_query       checks and cleans the user's protein input (rules in research/APP_DESIGN.md §2.2)
  Databases         the prepared databases the user can search
  Job / JobStore    one engine process per search: progress relayed from its JSON-lines stderr, result from stdout
  hits_csv, alignment_text, scalar_estimate   exports and the labelled estimate in the run summary

The engine is always started as a separate process with an argument list (never through a shell), so
nothing the user types is ever interpreted as a command.
"""
import csv
import io
import json
import os
import signal
import statistics
import subprocess
import tempfile
import threading
import time
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "bin", "hysw_engine")

AMINO = set("ACDEFGHIKLMNPQRSTVWY")          # the 20 standard amino acids
AMBIGUOUS = set("BZX")                       # in BLOSUM62: Asx, Glx, unknown
NOT_IN_MATRIX = set("UOJ")                   # selenocysteine, pyrrolysine, Leu/Ile: the engine scores them as X
NUCLEOTIDE = set("ACGTUN")
MIN_LEN, MAX_LEN = 10, 5000
MODES = ("auto", "scalar", "simd", "simd_threads")
CORES = ("auto", "p", "e", "physical", "all")
TOP_K = (10, 25, 50)


class QueryError(ValueError):
    """Input the user must fix; the message is shown to them as is."""


def parse_query(text):
    """Return (header, sequence, warnings) for one protein given as raw letters or one FASTA record."""
    text = (text or "").replace("\r\n", "\n").replace("\r", "\n").strip()
    if not text:
        raise QueryError("Paste a protein sequence, upload a FASTA file, or choose an example.")
    header = "query"
    if text.startswith(">"):
        records = text.count("\n>") + 1
        if records > 1:
            raise QueryError(f"Only one sequence at a time: the input contains {records} FASTA records.")
        first, _, body = text.partition("\n")
        header = " ".join(first[1:].split())[:500] or "query"
    else:
        body = text
    letters, bad = [], set()
    chars = [c for c in body if not c.isspace() and not c.isdigit()]     # sequences copied with line numbers
    if chars and chars[-1] == "*":                                       # a trailing stop codon symbol
        chars.pop()
    for c in chars:
        u = c.upper()
        if "A" <= u <= "Z":
            letters.append(u)
        else:
            bad.add(c)
    if bad:
        shown = ", ".join(repr(c) for c in sorted(bad)[:8])
        raise QueryError(f"Unexpected characters in the sequence: {shown}. Use amino-acid letters only.")
    seq = "".join(letters)
    if not seq:
        raise QueryError("The input contains no sequence letters.")
    if len(seq) >= 20 and sum(c in NUCLEOTIDE for c in seq) >= 0.9 * len(seq):
        share = round(100 * sum(c in "ACGT" for c in seq) / len(seq))
        raise QueryError(f"This looks like DNA or RNA ({share}% A, C, G, T), not a protein. "
                         "HySW searches protein sequences: translate the gene first.")
    if len(seq) < MIN_LEN:
        raise QueryError(f"The sequence has {len(seq)} letters; at least {MIN_LEN} are needed for a meaningful search.")
    if len(seq) > MAX_LEN:
        raise QueryError(f"The sequence has {len(seq):,} letters; the limit is {MAX_LEN:,}.")
    warnings = []
    odd = [c for c in seq if c in NOT_IN_MATRIX]
    if odd:
        warnings.append(f"{len(odd)} letter(s) {''.join(sorted(set(odd)))} are not in the BLOSUM62 matrix and are scored as X (unknown).")
    amb = [c for c in seq if c in AMBIGUOUS]
    if amb:
        warnings.append(f"{len(amb)} ambiguous letter(s) ({''.join(sorted(set(amb)))}) are scored with BLOSUM62's B/Z/X rows.")
    return header, seq, warnings


def run_engine(*args, timeout=120):
    p = subprocess.run([ENGINE, *args], capture_output=True, text=True, timeout=timeout)
    try:
        out = json.loads(p.stdout)
    except json.JSONDecodeError:
        out = None
    if p.returncode != 0 or out is None or "error" in out:
        msg = (out or {}).get("error") or p.stderr.strip() or f"engine exited with status {p.returncode}"
        raise RuntimeError(msg)
    return out


class Databases:
    """The databases offered in the app. E. coli is bundled (prepared on first start if needed)."""
    SPECS = [
        {"id": "swissprot", "label": "UniProtKB/Swiss-Prot", "bin": "data/swissprot_2026_04/swissprot.bin",
         "fasta": None, "about": "All reviewed (curated) proteins of every organism",
         "source": "UniProtKB/Swiss-Prot Release 2026_04 of 04-Nov-2026, CC BY 4.0",
         "missing": "Not prepared on this computer. Run scripts/get_swissprot.sh (downloads about 94 MB, licence CC BY 4.0)."},
        {"id": "ecoli", "label": "E. coli K-12 proteome", "bin": "data/ecoli_k12.bin", "fasta": "data/ecoli_k12.fasta",
         "about": "Reviewed proteins of Escherichia coli K-12 (bundled, small)",
         "source": "UniProtKB reviewed proteome UP000000625, CC BY 4.0", "missing": "Bundled FASTA file is missing."},
    ]

    def __init__(self, root=ROOT):
        self.root = root
        self.items = []
        for spec in self.SPECS:
            d = dict(spec)
            path = os.path.join(root, d["bin"])
            if not os.path.exists(path) and d["fasta"] and os.path.exists(os.path.join(root, d["fasta"])):
                run_engine("prep", "--fasta", os.path.join(root, d["fasta"]), "--out", path, timeout=600)
            d["path"] = path
            d["ready"] = os.path.exists(path)
            if d["ready"]:
                info = run_engine("dbinfo", "--db", path)
                d.update(sequences=info["sequences"], residues=info["residues"], longest=info["longest"])
            self.items.append(d)

    def public(self):
        keys = ("id", "label", "about", "source", "ready", "sequences", "residues", "longest", "missing")
        return [{k: d.get(k) for k in keys} for d in self.items]

    def get(self, db_id):
        d = next((d for d in self.items if d["id"] == db_id), None)
        if d is None:
            raise QueryError("Unknown database.")
        if not d["ready"]:
            raise QueryError(f"{d['label']} is not available: {d['missing']}")
        return d

    def default(self):
        return next((d["id"] for d in self.items if d["ready"]), None)


class Job:
    def __init__(self, params, header, seq, warnings, workdir):
        self.id = uuid.uuid4().hex[:12]
        self.params, self.header, self.seq, self.warnings = params, header, seq, warnings
        self.state = "running"                  # running | done | error | cancelled (set once the engine has exited)
        self.cancel_requested = False
        self.events = []                        # progress events, in order (relayed by SSE)
        self.result = self.error = None
        self.created = time.time()
        self.finished = None
        self.proc = None
        self.cond = threading.Condition()
        self.dir = os.path.join(workdir, self.id)
        os.makedirs(self.dir)
        self.query_path = os.path.join(self.dir, "query.fasta")
        with open(self.query_path, "w") as f:
            f.write(f">{header}\n")
            for i in range(0, len(seq), 60):
                f.write(seq[i:i + 60] + "\n")

    def argv(self, db_path):
        p = self.params
        argv = [ENGINE, "search", "--db", db_path, "--query", self.query_path, "--top-k", str(p["top_k"]),
                "--mode", p["mode"], "--progress"]
        if p["mode"] in ("auto", "simd_threads"):     # scalar and simd mean one thread: the engine's own default
            argv += ["--cores", p["cores"]]
        return argv

    def _push(self, ev):
        with self.cond:
            self.events.append(ev)
            self.cond.notify_all()

    def run(self, db_path, on_finish):
        try:
            self.proc = subprocess.Popen(self.argv(db_path), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                         start_new_session=True)
            if self.cancel_requested:           # cancelled before the process existed
                os.killpg(self.proc.pid, signal.SIGTERM)
            out_chunks, err_lines = [], []
            reader = threading.Thread(target=lambda: out_chunks.append(self.proc.stdout.read()), daemon=True)
            reader.start()                      # stdout and stderr are drained concurrently (no pipe deadlock)
            for line in self.proc.stderr:
                line = line.strip()
                if line.startswith("{"):
                    try:
                        ev = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if ev.get("type") == "progress":
                        self._push({"type": "progress", "done": ev["done"], "total": ev["total"],
                                    "elapsed": ev["elapsed"], "thread_items": ev["thread_items"]})
                elif line:
                    err_lines.append(line)
            rc = self.proc.wait()
            reader.join()
            out = "".join(out_chunks)
            with self.cond:
                if self.cancel_requested:
                    self.state = "cancelled"
                elif rc == 0:
                    self.result = json.loads(out)
                    self.state = "done"
                else:
                    try:
                        self.error = json.loads(out).get("error")
                    except (json.JSONDecodeError, AttributeError):
                        self.error = None
                    self.error = self.error or (err_lines[-1] if err_lines else f"engine exited with status {rc}")
                    self.state = "error"
        except Exception as e:                  # report, never leave a job stuck in "running"
            with self.cond:
                self.error, self.state = f"server error: {e}", "error"
        finally:
            with self.cond:
                self.finished = time.time()
                self.cond.notify_all()
            on_finish(self)

    def cancel(self):
        with self.cond:
            if self.state != "running":
                return False
            self.cancel_requested = True
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        return True

    def summary(self):
        return {"id": self.id, "state": self.state, "cancelling": self.cancel_requested and self.state == "running",
                "error": self.error, "warnings": self.warnings,
                "query": {"header": self.header, "length": len(self.seq), "sequence": self.seq}, "params": self.params,
                "progress": self.events[-1] if self.events else None}


class JobStore:
    """At most one search runs at a time: each search already uses every core it is given."""
    KEEP = 20

    def __init__(self, databases):
        self.dbs = databases
        self.jobs = {}
        self.order = []
        self.lock = threading.Lock()
        self.active = None
        self.workdir = tempfile.mkdtemp(prefix="hysw_jobs_")

    def busy(self):
        return self.active is not None

    def start(self, params, text):
        header, seq, warnings = parse_query(text)
        db = self.dbs.get(params["database"])
        return self.start_task(lambda: Job(params, header, seq, warnings, self.workdir),
                               lambda job: job.run(db["path"], self._finished))

    def start_task(self, make, run):
        """Register a task built by make() and run it in a thread; refused while another engine task runs."""
        with self.lock:
            if self.active is not None:
                raise BusyError("Another search or Lab run is still running. Wait for it to finish or cancel it.")
            job = make()
            self.active = job
            self.jobs[job.id] = job
            self.order.append(job.id)
            while len(self.order) > self.KEEP:
                old = self.jobs.pop(self.order.pop(0), None)
                if old:
                    _rmtree(old.dir)
        threading.Thread(target=run, args=(job,), daemon=True).start()
        return job

    def _finished(self, job):
        with self.lock:
            if self.active is job:
                self.active = None

    def get(self, job_id):
        return self.jobs.get(job_id)

    def claim(self):
        """Reserve the engine for a non-search task (calibration). Returns False if a search is running."""
        with self.lock:
            if self.active is not None:
                return False
            self.active = "calibration"
            return True

    def release(self):
        with self.lock:
            if self.active == "calibration":
                self.active = None


class BusyError(RuntimeError):
    pass


def _rmtree(path):
    for name in os.listdir(path):
        os.remove(os.path.join(path, name))
    os.rmdir(path)


def validate_params(body, dbs):
    try:
        top_k = int(body.get("top_k", 25))
    except (TypeError, ValueError):
        raise QueryError("Number of hits must be 10, 25 or 50.")
    params = {"database": str(body.get("database") or dbs.default() or ""), "top_k": top_k,
              "mode": str(body.get("mode", "auto")), "cores": str(body.get("cores", "auto"))}
    if top_k not in TOP_K:
        raise QueryError("Number of hits must be 10, 25 or 50.")
    if params["mode"] not in MODES:
        raise QueryError("Unknown engine mode.")
    if params["cores"] not in CORES:
        raise QueryError("Unknown core selection.")
    return params


# ---------------------------------------------------------------- run summary estimate and exports
def scalar_estimate_rate(root=ROOT):
    """Median scalar speed measured in M6 E1 (GCUPS), with its source; None if the results are missing."""
    path = os.path.join(root, "results", "m6", "e1_kernels.csv")
    if not os.path.exists(path):
        return None
    rows = [r for r in csv.DictReader(open(path)) if r["kernel"] == "scalar"]
    if not rows:
        return None
    return {"gcups": statistics.median(float(r["gcups"]) for r in rows),
            "source": "measured scalar speed, one P-core thread, Swiss-Prot, query length 400 (M6 E1, results/m6/)"}


def _cell(v):
    s = "" if v is None else str(v)
    return "'" + s if s[:1] in ("=", "+", "@", "\t", "\r") else s      # keep spreadsheet formulas inert


def hits_csv(result):
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(["rank", "accession", "entry", "protein_name", "organism", "length", "score", "identity", "positives",
                "query_coverage", "query_start", "query_end", "hit_start", "hit_end"])
    for h in result["queries"][0]["hits"]:
        a = h.get("alignment", {})
        w.writerow([h["rank"], _cell(h["accession"]), _cell(h["entry"]), _cell(h["name"]), _cell(h["organism"]),
                    h["length"], h["score"], h.get("identity", ""), h.get("positives", ""), h.get("coverage", ""),
                    a.get("q_start", ""), a.get("q_end", ""), a.get("t_start", ""), a.get("t_end", "")])
    return buf.getvalue()


def alignment_text(result, rank, width=60):
    q = result["queries"][0]
    h = next((h for h in q["hits"] if h["rank"] == rank), None)
    if h is None or "alignment" not in h:
        return None
    a = h["alignment"]
    s = result["settings"]
    lines = [f"Query: {q['query']['header']}  (length {q['query']['length']})",
             f"Hit {rank}: {h['header']}  (length {h['length']})",
             f"Score {h['score']} ({s['matrix']}, gap open {s['gap_open']}, extend {s['gap_extend']}); "
             f"identities {a['identities']}/{a['length']} ({100 * h['identity']:.1f}%), "
             f"positives {a['positives']}/{a['length']} ({100 * h['positives']:.1f}%), gaps {a['gaps']}", ""]
    qi, ti = a["q_start"], a["t_start"]
    for i in range(0, a["length"], width):
        qs, ms, ts = a["q"][i:i + width], a["mid"][i:i + width], a["t"][i:i + width]
        qn, tn = sum(c != "-" for c in qs), sum(c != "-" for c in ts)
        lines += [f"Query {qi:>6}  {qs}  {qi + qn - 1}", f"{'':12}  {ms}", f"Hit   {ti:>6}  {ts}  {ti + tn - 1}", ""]
        qi += qn
        ti += tn
    return "\n".join(lines)
