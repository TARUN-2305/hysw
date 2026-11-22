"""demo/frames.json (screencast timestamps) -> demo/demo.mp4 at a constant 30 fps (each frame held until the next one)."""
import json, subprocess, sys
from pathlib import Path
D = Path(sys.argv[1] if len(sys.argv) > 1 else 'demo')
fr = json.loads((D / 'frames.json').read_text())
lst = []
for a, b in zip(fr, fr[1:] + [{'t': fr[-1]['t'] + 1.0}]):
    lst += [f"file 'frames/{a['f']}'", f"duration {max(0.001, b['t'] - a['t']):.4f}"]
lst.append(f"file 'frames/{fr[-1]['f']}'")
(D / 'concat.txt').write_text('\n'.join(lst) + '\n')
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(D / 'concat.txt'), '-vf', 'scale=1920:1080:flags=lanczos,fps=30',
                '-c:v', 'libx264', '-crf', '18', '-preset', 'veryfast', '-pix_fmt', 'yuv420p', str(D / 'demo.mp4')], check=True)
ev = json.loads((D / 'events.json').read_text()); t0 = fr[0]['t']
print('events (s into demo.mp4):', ', '.join(f"{e['name']} {e['t'] - t0:.1f}" for e in ev))
