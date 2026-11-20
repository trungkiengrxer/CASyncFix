import sys, re, collections, subprocess
# frames.py <start> <end> : on-glass frame pacing from WindowServer's own FrameLifetime signposts (no tracing needed).
#   start/end: "YYYY-MM-DD HH:MM:SS" (log show --start/--end).
# Per frame: interval = end_time - prev_frame (refresh multiples; >1 = dropped frames), latency = end_time - begin_time,
# buffer_count = swap buffers in flight (2 = just-in-time, 3 = rendered a frame ahead).
start, end = sys.argv[1], sys.argv[2]
out = subprocess.run(['/usr/bin/log', 'show', '--signpost', '--style', 'compact', '--start', start, '--end', end,
                      '--predicate', 'process == "WindowServer" AND subsystem == "com.apple.SkyLight" AND category == "tracing"'],
                     capture_output=True, text=True).stdout
num = lambda k, l: (lambda m: int(m.group(1)) if m else None)(re.search(k, l))
c = collections.Counter(); lat = collections.defaultdict(list); seq = []
for l in out.splitlines():
    if 'FrameLifetime' not in l: continue
    b, e, p = num(r'#begin_time#_##_#(\d+)', l), num(r'#end_time#_##_#(\d+)', l), num(r'prev_frame=(\d+)', l)
    r, bc = num(r'refresh_interval=(\d+)', l), num(r'buffer_count=(\d+)', l)
    if not (b and e and p and r) or e - p > 10 * r: continue      # first frame after idle: not a drop
    c[(bc, min(round((e - p) / r), 4))] += 1
    lat[bc].append((e - b) / 1e6)
    seq.append((e - b) / 1e6)
total = sum(c.values())
for bc in sorted({k[0] for k in c}):
    n = sum(v for k, v in c.items() if k[0] == bc); L = sorted(lat[bc])
    print(f'buffer_count={bc}: {n} frames, ' + ' '.join(f'{k}x={c[(bc, k)]}' for k in range(1, 5)) +
          f', dropped {100 * (n - c[(bc, 1)]) / n:.1f}%, latency p50 {L[len(L) // 2]:.1f} ms')
ok = sum(v for k, v in c.items() if k[1] == 1)
print(f'all: {total} frames, on time {100 * ok / max(total, 1):.1f}%')
allLat = [x for v in lat.values() for x in v]
h = collections.Counter(min(int(x // 4) * 4, 48) for x in allLat)
print('latency render->glass (4 ms bins): ' + ' '.join(f'{k}:{h[k]}' for k in sorted(h)))
# CoreAnimation samples animations at the update's start (SLSCurrentRealTime -> CARenderUpdateBegin2), so a change in
# render->glass latency between consecutive frames is a position error on screen even when no frame is dropped.
jit = [abs(b - a) for a, b in zip(seq, seq[1:])]
print(f'latency jitter between consecutive frames: > 4 ms {sum(j > 4 for j in jit)}, > 8 ms {sum(j > 8 for j in jit)} '
      f'of {len(jit)} ({100 * sum(j > 4 for j in jit) / max(len(jit), 1):.1f}% > 4 ms)')
