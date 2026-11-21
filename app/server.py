"""HySW local web server (research/APP_DESIGN.md §3). Binds to 127.0.0.1 only; no accounts, nothing leaves the machine.

    .venv/bin/python -I app/server.py [--port 8372] [--open]

Routes (all JSON unless stated):
  GET  /                              the single-page front end (app/static/)
  GET  /api/system                    hardware card (engine `info`), last calibration, scalar-rate source
  GET  /api/databases | /api/examples
  POST /api/search                    {query, database, top_k, mode, cores} -> 202 {job}; 400 bad input; 409 busy
  GET  /api/jobs/<id>                 state, progress, and the engine's result when done
  GET  /api/jobs/<id>/events          Server-Sent Events: the engine's progress lines, then `end`
  POST /api/jobs/<id>/cancel
  GET  /api/jobs/<id>/hits.csv | /alignment/<rank>.txt | /report.json     exports
  POST /api/calibrate                 {database} -> thread-set calibration (DECISIONS.md D5)
  GET  /api/jobs/<id>/structure/<rank>  AlphaFold DB model of a hit (fetched once, then cached) + alignment mapping
  GET  /api/structure/<accession>/model.pdb   the cached model file
  GET  /api/lab/results               stored benchmark results (E1, E2, E3, E4, E8b) and verification records
  POST /api/lab/race | /api/lab/schedules   {example} -> 202 {task}; progress via /api/jobs/<id>/events
  GET  /api/lab/tasks/<id>            a Lab task's state and per-step results
  GET  /api/lab/simd?accession=ACC    one real batch of 16 sequences, H values from the tiled kernel

Requests must name this server as Host (127.0.0.1 or localhost), and POSTs must be JSON: together these stop
other web pages open in the browser from driving the local server (DNS rebinding, cross-site form posts).
"""
import argparse
import datetime
import json
import os
import sys
import threading
import webbrowser

from flask import Flask, Response, abort, jsonify, request, send_from_directory

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine as eng  # noqa: E402
import lab  # noqa: E402
import structures  # noqa: E402

STATIC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")


def create_app(port=8372, root=eng.ROOT, extra_hosts=(), structure_cache=structures.CACHE, allow_network=True):
    app = Flask(__name__, static_folder=None)
    app.config["MAX_CONTENT_LENGTH"] = 1 << 20
    allowed = {f"127.0.0.1:{port}", f"localhost:{port}", *extra_hosts}
    dbs = eng.Databases(root)
    jobs = eng.JobStore(dbs)
    system = eng.run_engine("info")
    rate = eng.scalar_estimate_rate(root)
    examples = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "examples.json")))
    state = {"calibration": None}
    lab_db = os.path.join(root, "data", "ecoli_x20.bin")
    if not os.path.exists(lab_db):
        lab_db = dbs.get("ecoli")["path"]
    lab_info = {k: v for k, v in eng.run_engine("dbinfo", "--db", lab_db).items() if k != "path"}
    by_acc = {e["accession"]: e for e in examples["examples"]}
    app.extensions["hysw"] = {"dbs": dbs, "jobs": jobs}

    @app.before_request
    def guard():
        if request.host not in allowed:
            abort(403)
        if request.method == "POST" and not request.is_json:
            return jsonify(error="Requests must be JSON."), 415

    @app.after_request
    def headers(resp):
        resp.headers["X-Content-Type-Options"] = "nosniff"
        resp.headers["Referrer-Policy"] = "no-referrer"
        resp.headers["Content-Security-Policy"] = ("default-src 'self'; img-src 'self' data:; style-src 'self'; "
                                                   "script-src 'self'; connect-src 'self'; frame-ancestors 'none'")
        return resp

    @app.get("/")
    def index():
        return send_from_directory(STATIC, "index.html")

    @app.get("/static/<path:name>")
    def static_file(name):
        return send_from_directory(STATIC, name)

    @app.get("/api/system")
    def api_system():
        return jsonify(info=system, calibration=state["calibration"], scalar_rate=rate, busy=jobs.busy(),
                       limits={"min_length": eng.MIN_LEN, "max_length": eng.MAX_LEN, "top_k": list(eng.TOP_K)})

    @app.get("/api/databases")
    def api_databases():
        return jsonify(databases=dbs.public(), default=dbs.default())

    @app.get("/api/examples")
    def api_examples():
        return jsonify(examples)

    @app.post("/api/search")
    def api_search():
        body = request.get_json(silent=True) or {}
        try:
            params = eng.validate_params(body, dbs)
            job = jobs.start(params, body.get("query", ""))
        except eng.QueryError as e:
            return jsonify(error=str(e)), 400
        except eng.BusyError as e:
            return jsonify(error=str(e)), 409
        return jsonify(job=job.summary()), 202

    def job_or_404(job_id):
        job = jobs.get(job_id)
        if job is None:
            abort(404)
        return job

    def with_estimate(job):
        out = job.summary()
        if job.state == "done":
            out["result"] = job.result
            if rate:
                cells = job.result["queries"][0]["timing"]["cells"]
                out["scalar_estimate"] = {"seconds": cells / (rate["gcups"] * 1e9), **rate}
        return out

    @app.get("/api/jobs/<job_id>")
    def api_job(job_id):
        job = job_or_404(job_id)
        if isinstance(job, lab.LabTask):
            return jsonify(job.summary())
        return jsonify(with_estimate(job))

    @app.get("/api/jobs/<job_id>/events")
    def api_events(job_id):
        job = job_or_404(job_id)

        def stream():
            i = 0
            while True:
                with job.cond:
                    if i >= len(job.events) and job.state == "running":
                        job.cond.wait(timeout=15)
                    new, i, st = job.events[i:], len(job.events), job.state
                for ev in new:
                    yield f"data: {json.dumps(ev)}\n\n"
                if st != "running":
                    yield f"event: end\ndata: {json.dumps({'state': st, 'error': job.error})}\n\n"
                    return
                if not new:
                    yield ": keep-alive\n\n"

        return Response(stream(), mimetype="text/event-stream", headers={"Cache-Control": "no-cache"})

    @app.post("/api/jobs/<job_id>/cancel")
    def api_cancel(job_id):
        return jsonify(cancelled=job_or_404(job_id).cancel())

    def done_job(job_id):
        job = job_or_404(job_id)
        if not isinstance(job, eng.Job):
            abort(404)
        if job.state != "done":
            abort(409)
        return job

    def download(text, mimetype, name):
        return Response(text, mimetype=mimetype, headers={"Content-Disposition": f'attachment; filename="{name}"'})

    @app.get("/api/jobs/<job_id>/hits.csv")
    def api_csv(job_id):
        return download(eng.hits_csv(done_job(job_id).result), "text/csv", f"hysw_{job_id}_hits.csv")

    @app.get("/api/jobs/<job_id>/alignment/<int:rank>.txt")
    def api_alignment(job_id, rank):
        text = eng.alignment_text(done_job(job_id).result, rank)
        if text is None:
            abort(404)
        return download(text, "text/plain", f"hysw_{job_id}_hit{rank}.txt")

    @app.get("/api/jobs/<job_id>/report.json")
    def api_report(job_id):
        job = done_job(job_id)
        report = {"app": "HySW", "generated": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
                  "request": job.params, "query": {"header": job.header, "sequence": job.seq, "warnings": job.warnings},
                  "system": system, **{k: v for k, v in with_estimate(job).items() if k in ("result", "scalar_estimate")}}
        return download(json.dumps(report, indent=1), "application/json", f"hysw_{job_id}_report.json")

    @app.get("/api/jobs/<job_id>/structure/<int:rank>")
    def api_structure(job_id, rank):
        hit = next((h for h in done_job(job_id).result["queries"][0]["hits"] if h["rank"] == rank), None)
        if hit is None:
            abort(404)
        try:
            meta, cached = structures.fetch(hit["accession"], structure_cache,
                                            allow_network and request.args.get("cached_only") != "1")
        except structures.StructureError as e:
            code = {"invalid": 400, "not_found": 404, "not_cached": 404}.get(e.kind, 503)
            return jsonify(error=str(e), kind=e.kind), code
        conf = structures.plddt(hit["accession"], structure_cache)
        first = min(conf) if conf else 1
        return jsonify(meta=meta, cached=cached, mapping=structures.map_alignment(meta, hit),
                       plddt={"first": first, "values": [conf.get(r) for r in range(first, first + len(conf))]},
                       model_url=f"/api/structure/{hit['accession']}/model.pdb")

    @app.get("/api/structure/<acc>/model.pdb")
    def api_model(acc):
        path = structures.model_path(acc, structure_cache)
        if path is None:
            abort(404)
        return send_from_directory(os.path.dirname(path), "model.pdb", mimetype="text/plain", max_age=86400)

    @app.get("/api/lab/results")
    def api_lab_results():
        return jsonify(lab.stored_results())

    def example_from(body):
        ex = by_acc.get(str(body.get("example") or ""))
        if ex is None:
            raise eng.QueryError("Choose one of the example proteins.")
        return ex

    def start_lab(kind, steps):
        body = request.get_json(silent=True) or {}
        try:
            ex = example_from(body)
            task = jobs.start_task(lambda: lab.LabTask(kind, steps, lab_db, ex["header"], ex["sequence"], jobs.workdir),
                                   lambda t: t.run(jobs._finished))
        except eng.QueryError as e:
            return jsonify(error=str(e)), 400
        except eng.BusyError as e:
            return jsonify(error=str(e)), 409
        return jsonify(task=task.summary(), database=lab_info), 202

    @app.post("/api/lab/race")
    def api_race():
        return start_lab("race", lab.RACE)

    @app.post("/api/lab/schedules")
    def api_schedules():
        return start_lab("schedules", lab.SCHEDULES)

    @app.get("/api/lab/tasks/<task_id>")
    def api_task(task_id):
        task = job_or_404(task_id)
        if not isinstance(task, lab.LabTask):
            abort(404)
        return jsonify(task=task.summary(), database=lab_info)

    @app.get("/api/lab/simd")
    def api_simd():
        ex = by_acc.get(request.args.get("accession", ""))
        if ex is None:
            return jsonify(error="Choose one of the example proteins."), 400
        sp = next((d for d in dbs.items if d["id"] == "swissprot" and d["ready"]), None) or dbs.get("ecoli")
        try:
            out = lab.simd_trace(sp["path"], ex["accession"], ex["header"], ex["sequence"])
        except RuntimeError as e:
            return jsonify(error=str(e)), 404 if "not found" in str(e) else 500
        out["database"] = sp["label"]
        return jsonify(out)

    @app.post("/api/calibrate")
    def api_calibrate():
        body = request.get_json(silent=True) or {}
        try:
            db = dbs.get(str(body.get("database") or dbs.default()))
        except eng.QueryError as e:
            return jsonify(error=str(e)), 400
        if not jobs.claim():
            return jsonify(error="A search is running. Calibrate when it has finished."), 409
        try:
            out = eng.run_engine("calibrate", "--db", db["path"], timeout=60)
        except (RuntimeError, OSError) as e:
            return jsonify(error=f"Calibration failed: {e}"), 500
        finally:
            jobs.release()
        state["calibration"] = {**out["calibration"], "database": db["id"],
                                "when": datetime.datetime.now().astimezone().isoformat(timespec="seconds")}
        return jsonify(calibration=state["calibration"])

    return app


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8372)
    ap.add_argument("--open", action="store_true", help="open the browser once the server is up")
    a = ap.parse_args()
    if not os.access(eng.ENGINE, os.X_OK):
        sys.exit(f"engine not built: run ./build.sh first ({eng.ENGINE})")
    app = create_app(a.port)
    url = f"http://127.0.0.1:{a.port}/"
    print(f"HySW running at {url}  (Ctrl+C to stop)", flush=True)
    if a.open:
        threading.Timer(0.8, webbrowser.open, args=(url,)).start()
    app.run(host="127.0.0.1", port=a.port, threaded=True, debug=False, use_reloader=False)


if __name__ == "__main__":
    main()
