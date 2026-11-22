# HySW explainer video (about 6:45, 1920x1080)

A narrated, animated explainer of the project from first principles, ending with a real recording of the app.
Built with the free Nafay 3D skills (Faceless Shorts engine, adapted here to 16:9 long form; subtitle style from the
Style Library): narration by the local Kokoro voice, word timings from local Whisper, every shot drawn on a canvas in
code, frames captured by headless Chrome, sound mixed with numpy, encoded with ffmpeg. No AI images or AI video.

- `script.json`: the narration (one line = one shot) and sources.
- `src/shots.js`: every shot. Charts use measured data: `data/e4_passes.json` (E4) and `data/e3_trace.json` (E3 traces).
- `tools/record_demo.mjs`: drives the real app (127.0.0.1:8372) in headless Chrome and records it; `tools/demo_to_mp4.py`.

Rebuild (needs the faceless-shorts skill set up, the app running, Chrome at `$CHROME`):
```
node tools/record_demo.mjs demo && python3 tools/demo_to_mp4.py demo
ffmpeg -i demo/demo.mp4 -q:v 3 -start_number 0 demo/f/%05d.jpg
~/.claude/skills/faceless-shorts/scripts/py ~/.claude/skills/faceless-shorts/scripts/voice.py .
~/.claude/skills/faceless-shorts/scripts/py ~/.claude/skills/faceless-shorts/scripts/words.py .
node tools/capture.mjs check && node tools/capture.mjs cues && python tools/mix.py .
node tools/capture.mjs video renders/HySW_explainer.mp4
```
The demo segment of the search engine activity is slowed down 6x and labelled so on screen; everything else plays in real time.

Not included here (the Faceless Shorts skill ships them with no stated licence, so they are not redistributed):
`tools/capture.mjs`, `tools/mix.py`, `src/core/timing.js`. Copy them from
`~/.claude/skills/faceless-shorts/styles/inside-out/example/`, then in `capture.mjs` change the window size to 1920,1080,
the sheet scale to 384:216, and `await OP.frame(...)` in `FRAME` (OP.frame is async here).
