"""AlphaFold DB structures for the 3D view: fetch once by UniProt accession, cache on disk, map the alignment.

Source: AlphaFold Protein Structure Database (EMBL-EBI / Google DeepMind), CC BY 4.0. The prediction API
(https://alphafold.ebi.ac.uk/api/prediction/<accession>) names the current model files; file names carry a version
token (v6 in November 2026), so URLs are always taken from the API rather than built by hand.

Cache: data/structures/<accession>/prediction.json + model.pdb. The four example proteins are cached in advance by
scripts/cache_example_structures.py, so the demo works offline. Only the public accession leaves the machine.

Highlighting: AlphaFold models are numbered along the UniProt sequence. The aligned region is highlighted only when
the model's sequence agrees with the hit's aligned residues at the same positions; otherwise the reason is reported.
"""
import json
import os
import re
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "data", "structures")
API = "https://alphafold.ebi.ac.uk/api/prediction/{}"
FILE_HOST = "https://alphafold.ebi.ac.uk/files/"
MAX_BYTES = 30 << 20
ACCESSION = re.compile(r"^([OPQ][0-9][A-Z0-9]{3}[0-9]|[A-NR-Z][0-9]([A-Z][A-Z0-9]{2}[0-9]){1,2})$")
ATTRIBUTION = ("Predicted structure from the AlphaFold Protein Structure Database (EMBL-EBI and Google DeepMind), "
               "CC BY 4.0. A computational prediction, not an experimental structure.")


class StructureError(Exception):
    def __init__(self, kind, message):
        super().__init__(message)
        self.kind = kind          # invalid | not_found | not_cached | offline | refused | failed


def _get(url, timeout=20):
    req = urllib.request.Request(url, headers={"User-Agent": "HySW-local-app/0.1 (structure viewer)"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            data = r.read(MAX_BYTES + 1)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise StructureError("not_found", "AlphaFold DB has no predicted structure for this protein.")
        if e.code in (403, 429, 503):
            raise StructureError("refused", f"AlphaFold DB refused the download (HTTP {e.code}); it may be limiting "
                                            "requests. Try again in a minute.")
        raise StructureError("failed", f"AlphaFold DB returned HTTP {e.code}.")
    except (urllib.error.URLError, TimeoutError, OSError):
        raise StructureError("offline", "Could not reach AlphaFold DB (no internet connection?), and this structure "
                                        "is not cached on this computer.")
    if len(data) > MAX_BYTES:
        raise StructureError("failed", "The structure file is unexpectedly large.")
    return data


def _choose(records, acc):
    mine = [r for r in records if r.get("uniprotAccession") == acc and r.get("pdbUrl")]
    if not mine:
        raise StructureError("not_found", "AlphaFold DB has no predicted structure for this protein.")
    mine.sort(key=lambda r: (not str(r.get("entryId", "")).endswith("-F1"), r.get("sequenceStart", 1)))
    return mine[0]


def fetch(acc, cache=CACHE, allow_network=True):
    """Return (meta, cached_before) for an accession, downloading it once if needed."""
    if not ACCESSION.match(acc or ""):
        raise StructureError("invalid", "Not a UniProt accession.")
    d = os.path.join(cache, acc)
    meta_path, pdb_path = os.path.join(d, "prediction.json"), os.path.join(d, "model.pdb")
    if os.path.exists(meta_path) and os.path.exists(pdb_path):
        return json.load(open(meta_path)), True
    if not allow_network:
        raise StructureError("not_cached", "This structure is not cached on this computer yet.")
    rec = _choose(json.loads(_get(API.format(acc))), acc)
    url = rec["pdbUrl"]
    if not url.startswith(FILE_HOST):
        raise StructureError("failed", "Unexpected file location in the AlphaFold DB response.")
    pdb = _get(url, timeout=60)
    if not pdb.startswith((b"HEADER", b"TITLE", b"ATOM", b"MODEL", b"REMARK", b"CRYST1")):
        raise StructureError("failed", "AlphaFold DB did not return a PDB file.")
    meta = {k: rec.get(k) for k in ("entryId", "uniprotAccession", "uniprotId", "uniprotDescription", "organismScientificName",
                                    "latestVersion", "modelCreatedDate", "globalMetricValue", "fractionPlddtVeryHigh",
                                    "fractionPlddtConfident", "fractionPlddtLow", "fractionPlddtVeryLow", "sequence",
                                    "sequenceStart", "sequenceEnd", "pdbUrl", "toolUsed")}
    meta["source"] = f"https://alphafold.ebi.ac.uk/entry/{acc}"
    meta["attribution"] = ATTRIBUTION
    os.makedirs(d, exist_ok=True)
    for path, data in ((pdb_path, pdb), (meta_path, json.dumps(meta, indent=1).encode())):
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)             # never leave a half-written cache entry
    return meta, False


def model_path(acc, cache=CACHE):
    if not ACCESSION.match(acc or ""):
        return None
    p = os.path.join(cache, acc, "model.pdb")
    return p if os.path.exists(p) else None


def plddt(acc, cache=CACHE):
    """Per-residue confidence (pLDDT, stored in the B-factor column of AlphaFold PDB files), from C-alpha atoms."""
    path = model_path(acc, cache)
    out = {}
    if path:
        for line in open(path):
            if line.startswith("ATOM") and line[12:16].strip() == "CA":
                out[int(line[22:26])] = float(line[60:66])
    return out


def map_alignment(meta, hit):
    """Residue numbers (model numbering) to colour, and whether highlighting is valid for this model."""
    a = hit.get("alignment")
    if not a:
        return {"ok": False, "reason": "This hit has no alignment."}
    seq, start = meta.get("sequence") or "", meta.get("sequenceStart") or 1
    segment = a["t"].replace("-", "")
    lo = a["t_start"] - start                      # index into the model's sequence
    if lo < 0 or a["t_end"] > (meta.get("sequenceEnd") or start + len(seq) - 1):
        return {"ok": False, "reason": "The aligned region lies outside the part of the protein this model covers."}
    if seq[lo:lo + len(segment)] != segment:
        return {"ok": False, "reason": "The AlphaFold model's sequence differs from this database's version of the protein "
                                       "in the aligned region, so the region is not highlighted."}
    identical, similar, different = [], [], []
    pos = a["t_start"]
    for qc, mc, tc in zip(a["q"], a["mid"], a["t"]):
        if tc == "-":
            continue
        if qc != "-":
            (identical if mc not in (" ", "+") else similar if mc == "+" else different).append(pos)
        else:
            different.append(pos)                     # hit residue opposite a gap in the query
        pos += 1
    return {"ok": True, "start": a["t_start"], "end": a["t_end"], "identical": identical, "similar": similar,
            "different": different}
