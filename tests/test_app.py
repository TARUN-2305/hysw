"""Web app tests (M6a): input rules, the HTTP API, and that a search run through the app returns exactly the
engine's own output. Uses Flask's test client (no browser, no network). Run: .venv/bin/python -I tests/test_app.py
"""
import csv
import io
import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "app"))
import engine as eng  # noqa: E402
import server  # noqa: E402

checks = failures = 0


def check(cond, msg):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("  FAIL:", msg)


def fasta_text(path):
    return open(os.path.join(ROOT, "tests", "queries", path)).read()


def direct(db, query_text, top_k, mode, cores):
    """The engine run by hand, exactly as the app runs it."""
    with tempfile.NamedTemporaryFile("w", suffix=".fasta", delete=False) as f:
        h, s, _ = eng.parse_query(query_text)
        f.write(f">{h}\n" + "".join(s[i:i + 60] + "\n" for i in range(0, len(s), 60)))
    cores_arg = ["--cores", cores] if mode in ("auto", "simd_threads") else []
    p = subprocess.run([eng.ENGINE, "search", "--db", db, "--query", f.name, "--top-k", str(top_k), "--mode", mode,
                        *cores_arg], capture_output=True, text=True)
    os.unlink(f.name)
    return json.loads(p.stdout)


def wait(client, job_id, timeout=120):
    t0 = time.time()
    while time.time() - t0 < timeout:
        j = client.get(f"/api/jobs/{job_id}").get_json()
        if j["state"] != "running":
            return j
        time.sleep(0.05)
    return j


def test_parse():
    print("[1] input rules")
    ok = lambda t: eng.parse_query(t)  # noqa: E731

    def err(t, part):
        try:
            eng.parse_query(t)
        except eng.QueryError as e:
            check(part.lower() in str(e).lower(), f"error for {t[:30]!r} should mention {part!r}: {e}")
            return
        check(False, f"{t[:30]!r} should be rejected")

    h, s, w = ok("MKTAYIAKQR QISFVKSHFS\n")
    check((h, s, w) == ("query", "MKTAYIAKQRQISFVKSHFS", []), f"raw letters with spaces: {(h, s, w)}")
    h, s, w = ok(">sp|P1|X_Y  some   protein\r\nmktayiakqr\r\n1 qisfvkshfs 20\n*")
    check(h == "sp|P1|X_Y some protein" and s == "MKTAYIAKQRQISFVKSHFS", f"FASTA, CRLF, digits, lower case, stop: {(h, s)}")
    h, s, w = ok("MKTAYIAKQRUUOJBZX")
    check(len(w) == 2 and "scored as X" in w[0] and "ambiguous" in w[1], f"U/O/J and B/Z/X warnings: {w}")
    err("", "paste")
    err(">a\nMKTAYIAKQR\n>b\nMKTAYIAKQR", "one sequence")
    err("ACGTACGTACGTACGTACGTACGTAAA", "DNA")
    err("MKTAY", "at least 10")
    err("M" * 5001, "limit")
    err("MKTAYIAKQR-QISF", "unexpected")
    err(">only a header\n", "no sequence")
    s5000 = ok("A" * 4000 + "MK" * 500)[1]
    check(len(s5000) == 5000, "5,000 letters accepted")


def test_api(client, app):
    print("[2] API: databases, system, examples, guards")
    d = client.get("/api/databases").get_json()
    ec = next(x for x in d["databases"] if x["id"] == "ecoli")
    check(ec["ready"] and ec["sequences"] == 4403, f"E. coli database ready: {ec}")
    sysinfo = client.get("/api/system").get_json()
    check(sysinfo["info"]["logical_cpus"] >= 1 and sysinfo["limits"]["max_length"] == 5000, "system info")
    ex = client.get("/api/examples").get_json()
    check(len(ex["examples"]) >= 3 and all(eng.parse_query(e["sequence"])[1] == e["sequence"] for e in ex["examples"]),
          "examples are valid queries")
    check(client.get("/", headers={"Host": "evil.example:8372"}).status_code == 403, "foreign Host header refused")
    r = client.post("/api/search", data="query=MKTAYIAKQRQISF", content_type="application/x-www-form-urlencoded")
    check(r.status_code == 415, f"non-JSON POST refused: {r.status_code}")
    check(client.get("/").status_code == 200 and b"<html" in client.get("/").data.lower(), "front page served")
    for body, part in (({"query": "MKTAY"}, "at least"), ({"query": "MKTAYIAKQRQISF", "top_k": 7}, "10, 25 or 50"),
                       ({"query": "MKTAYIAKQRQISF", "mode": "gpu"}, "mode"), ({"query": "MKTAYIAKQRQISF", "cores": "x"}, "core"),
                       ({"query": "MKTAYIAKQRQISF", "database": "nr"}, "unknown database")):
        r = client.post("/api/search", json=body)
        check(r.status_code == 400 and part.lower() in r.get_json()["error"].lower(), f"{body}: {r.status_code} {r.get_json()}")
    check(client.get("/api/jobs/nope").status_code == 404, "unknown job: 404")


def test_search_equals_engine(client, app):
    print("[3] searches through the app equal the engine's direct output")
    db = app.extensions["hysw"]["dbs"].get("ecoli")["path"]
    cases = [("q_medium.fasta", 25, "auto", "physical"), ("q_short.fasta", 10, "scalar", "auto"),
             ("q_long.fasta", 50, "simd", "auto"), ("q_selenocys.fasta", 25, "simd_threads", "all")]
    for qf, k, mode, cores in cases:
        text = fasta_text(qf)
        r = client.post("/api/search", json={"query": text, "database": "ecoli", "top_k": k, "mode": mode, "cores": cores})
        check(r.status_code == 202, f"{qf}: search accepted ({r.status_code} {r.get_json()})")
        if r.status_code != 202:
            continue
        j = wait(client, r.get_json()["job"]["id"])
        ref = direct(db, text, k, mode, cores)
        got = j.get("result", {})
        check(j["state"] == "done", f"{qf}: state {j['state']} {j.get('error')}")
        if j["state"] != "done":
            continue
        check(got["queries"][0]["hits"] == ref["queries"][0]["hits"], f"{qf} {mode}/{cores}: hits differ from the direct engine run")
        check(got["settings"] == ref["settings"] and got["database"]["residues"] == ref["database"]["residues"],
              f"{qf}: settings differ")
        check(len(got["queries"][0]["hits"]) == k, f"{qf}: {k} hits")
        want = {"scalar": ("scalar", 1), "simd": ("simd", 1)}.get(mode)
        if want:
            check((got["settings"]["kernel"], got["settings"]["threads"]) == want, f"{qf}: mode {mode} must run {want}")
        job = app.extensions["hysw"]["jobs"].get(j["id"])
        last = job.events[-1] if job.events else {}
        check(last.get("done") == last.get("total") and len(last.get("thread_items", [])) == got["settings"]["threads"],
              f"{qf}: progress ends complete with one entry per thread: {last}")
        check("scalar_estimate" in j and j["scalar_estimate"]["seconds"] > 0, f"{qf}: labelled scalar estimate present")
    return j


def test_exports_and_events(client, j):
    print("[4] progress stream and exports")
    jid = j["id"]
    body = client.get(f"/api/jobs/{jid}/events").get_data(as_text=True)
    check(body.count("data: {\"type\": \"progress\"") >= 2 and "event: end" in body and '"state": "done"' in body,
          "SSE replays progress then ends with state done")
    rows = list(csv.reader(io.StringIO(client.get(f"/api/jobs/{jid}/hits.csv").get_data(as_text=True))))
    hits = j["result"]["queries"][0]["hits"]
    check(len(rows) == len(hits) + 1 and [int(r[6]) for r in rows[1:]] == [h["score"] for h in hits], "CSV has every hit and score")
    txt = client.get(f"/api/jobs/{jid}/alignment/1.txt").get_data(as_text=True)
    a = hits[0]["alignment"]
    q_from_txt = "".join(l.split()[2] for l in txt.splitlines() if l.startswith("Query ") and len(l.split()) == 4)
    check(q_from_txt == a["q"], "alignment text spells the aligned query")
    check(client.get(f"/api/jobs/{jid}/alignment/999.txt").status_code == 404, "alignment of a missing rank: 404")
    rep = json.loads(client.get(f"/api/jobs/{jid}/report.json").get_data(as_text=True))
    check(rep["result"] == j["result"] and rep["request"] == j["params"] and "system" in rep, "report holds request, system and result")


def test_busy_cancel_calibrate(client, app):
    print("[5] one search at a time, cancel, calibration")
    slow = {"query": fasta_text("q_longest.fasta"), "database": "ecoli", "top_k": 10, "mode": "scalar"}
    r = client.post("/api/search", json=slow)
    check(r.status_code == 202, f"slow search accepted: {r.status_code}")
    jid = r.get_json()["job"]["id"]
    time.sleep(0.3)
    r2 = client.post("/api/search", json={"query": "MKTAYIAKQRQISFVKSHFSRQ", "database": "ecoli"})
    check(r2.status_code == 409, f"second search while busy: {r2.status_code}")
    check(client.post("/api/calibrate", json={}).status_code == 409, "calibration while a search runs: 409")
    check(client.get(f"/api/jobs/{jid}/hits.csv").status_code == 409, "export of a running job: 409")
    t0 = time.time()
    check(client.post(f"/api/jobs/{jid}/cancel", json={}).get_json()["cancelled"], "cancel accepted")
    j = wait(client, jid, 10)
    proc = app.extensions["hysw"]["jobs"].get(jid).proc
    check(j["state"] == "cancelled" and proc.poll() is not None and time.time() - t0 < 5, f"cancelled and engine stopped: {j['state']}")
    r3 = client.post("/api/search", json={"query": "MKTAYIAKQRQISFVKSHFSRQ", "database": "ecoli"})
    check(r3.status_code == 202, "new search accepted after cancel")
    wait(client, r3.get_json()["job"]["id"])
    c = client.post("/api/calibrate", json={"database": "ecoli"}).get_json()
    names = [s["name"] for s in c.get("calibration", {}).get("sets", [])]
    check(c.get("calibration", {}).get("chosen") in names and client.get("/api/system").get_json()["calibration"],
          f"calibration result stored: {c}")


def test_structures(client, app):
    print("[6] structures: cached AlphaFold models, alignment mapping, confidence (network off)")
    import structures
    r = client.post("/api/search", json={"query": next(e for e in json.load(open(os.path.join(ROOT, "app", "examples.json")))["examples"]
                                                          if e["accession"] == "P00582")["sequence"], "database": "ecoli", "top_k": 10})
    j = wait(client, r.get_json()["job"]["id"])
    h1 = j["result"]["queries"][0]["hits"][0]
    st = client.get(f"/api/jobs/{j['id']}/structure/1").get_json()
    m = st.get("mapping", {})
    check(h1["accession"] == "P00582" and st.get("cached") and m.get("ok") and m["start"] == 1 and m["end"] == 928
          and len(m["identical"]) == 928 and not m["similar"] and not m["different"], f"self hit maps fully: {str(st)[:200]}")
    check(len(st["plddt"]["values"]) == 928 and st["plddt"]["first"] == 1 and all(0 <= v <= 100 for v in st["plddt"]["values"]),
          "per-residue confidence for every residue")
    pdb = client.get(st["model_url"])
    check(pdb.status_code == 200 and pdb.data.startswith((b"HEADER", b"TITLE", b"ATOM", b"MODEL", b"REMARK", b"CRYST1")), "model file served")
    other = next(h for h in j["result"]["queries"][0]["hits"] if structures.model_path(h["accession"]) is None)
    r2 = client.get(f"/api/jobs/{j['id']}/structure/{other['rank']}?cached_only=1")
    check(r2.status_code == 404 and r2.get_json()["kind"] == "not_cached", f"uncached model, cached_only: {r2.status_code} {r2.get_json()}")
    r3 = client.get(f"/api/jobs/{j['id']}/structure/{other['rank']}")
    check(r3.status_code == 404 and r3.get_json()["kind"] == "not_cached", "network disabled: nothing is fetched")
    check(client.get("/api/structure/not-an-acc/model.pdb").status_code == 404, "invalid accession: 404")
    meta = json.load(open(os.path.join(structures.CACHE, "P00582", "prediction.json")))
    a = h1["alignment"]
    q = "".join("A" if c == "-" else c for c in a["q"])
    mid = "".join(" " if c == "-" else c for c in a["mid"])
    alt = dict(h1, alignment=dict(a, q="W" + q[1:], mid=" " + mid[1:]))
    mm = structures.map_alignment(meta, alt)
    check(mm["ok"] and mm["different"] == [1] and len(mm["identical"]) == 927, "a changed residue is classed different")
    bad = dict(h1, alignment=dict(a, t="W" + a["t"][1:]))
    check(not structures.map_alignment(meta, bad)["ok"], "a sequence that differs from the model is not highlighted")


def test_lab(client, app):
    print("[7] Performance Lab: stored results, race, traced schedules, SIMD batch")
    r = client.get("/api/lab/results").get_json()
    check(all(k in r for k in ("e1", "e2", "e3", "e4", "e6", "e8b")) and len(r["verification"]) == 2
          and all(v["identical"] == v["pairs"] for v in r["verification"]), f"stored results: {list(r)}")
    check(r["e1"]["rows"][0]["gcups"] < r["e1"]["rows"][1]["gcups"] < r["e1"]["rows"][2]["gcups"], "E1 rows ordered scalar < SIMD < threads")
    check(client.post("/api/lab/race", json={"example": "P99999"}).status_code == 400, "unknown example refused")
    t = client.post("/api/lab/race", json={"example": "P01308"})
    check(t.status_code == 202, f"race accepted: {t.status_code}")
    check(client.post("/api/search", json={"query": "MKTAYIAKQRQISFVKSHFSRQ", "database": "ecoli"}).status_code == 409,
          "a search is refused while a Lab run uses the engine")
    tid = t.get_json()["task"]["id"]
    wait(client, tid)
    task = client.get(f"/api/lab/tasks/{tid}").get_json()["task"]
    res = task["results"]
    check(task["state"] == "done" and [(x["kernel"], x["threads"]) for x in res] == [("scalar", 1), ("simd", 1), ("simd", 10)],
          f"race: three engines ran: {task['state']} {task['error']}")
    check(len({(x["top_hit"]["accession"], x["top_hit"]["score"]) for x in res}) == 1, "all three engines find the same top hit and score")
    body = client.get(f"/api/jobs/{tid}/events").get_data(as_text=True)
    check(body.count('"type": "step_done"') == 3 and "event: end" in body, "race progress stream has three finished steps")
    t = client.post("/api/lab/schedules", json={"example": "P01308"})
    tid = t.get_json()["task"]["id"]
    wait(client, tid)
    res = client.get(f"/api/lab/tasks/{tid}").get_json()["task"]["results"]
    check([x["policy"] for x in res] == ["static", "dynamic,1"] and all(all(th.get("busy") for th in x["thread_stats"]) for x in res),
          "schedules: two traced runs with busy intervals for every thread")
    s = client.get("/api/lab/simd?accession=P00582").get_json()
    lane = s.get("target_lane", -1)
    check(lane >= 0 and s["sequences"][lane]["accession"] == "P00582" and len(s["H"]) == 24 and len(s["H"][0]) == 48, "SIMD batch trace served")
    check(client.get("/api/lab/simd?accession=XYZ").status_code == 400, "SIMD trace: unknown example refused")
    check(client.get(f"/api/jobs/{tid}").get_json()["state"] == "done" and client.get(f"/api/jobs/{tid}/hits.csv").status_code == 404,
          "Lab task through the search routes: state served, no search exports")


def main():
    test_parse()
    app = server.create_app(extra_hosts=("localhost",), allow_network=False)
    client = app.test_client()
    test_api(client, app)
    j = test_search_equals_engine(client, app)
    test_exports_and_events(client, j)
    test_busy_cancel_calibrate(client, app)
    test_structures(client, app)
    test_lab(client, app)
    print(f"APP TESTS {'PASS' if failures == 0 else 'FAIL'}: {checks} checks, {failures} failures")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
