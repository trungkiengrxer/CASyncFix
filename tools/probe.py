import sys, re, collections, subprocess
# probe.py <start> <end> : CASyncFix probe build (FZ_PROBE=1). Every CDDisplay::present_update logs
#   "*** Solver: Testing proposed tile addition for spid = <mach ns> {origin=(<site>, <now s>), size=(<S s>, <target s>)}"
# (SkyLight's own os_log format, borrowed). Joined with the FrameLifetime signposts by mach time, this shows for each
# frame which branch computed its present time, how old S was, and whether the frame actually went up at the target.
start, end = sys.argv[1], sys.argv[2]
def show(pred):
    return subprocess.run(['/usr/bin/log', 'show', '--signpost', '--style', 'compact', '--start', start, '--end', end,
                           '--predicate', pred], capture_output=True, text=True).stdout.splitlines()

probe = []   # (mach_ns, site, now_s, S_s, target_s)
pat = re.compile(r'spid = (\d+) \{origin=\(([-\d.e+]+), ([-\d.e+]+)\), size=\(([-\d.e+]+), ([-\d.e+]+)\)\}')
for l in show('process == "WindowServer" AND eventMessage CONTAINS "Testing proposed tile addition"'):
    m = pat.search(l)
    if m: probe.append((int(m[1]), int(float(m[2])), float(m[3]), float(m[4]), float(m[5])))
num = lambda k, l: (lambda m: int(m.group(1)) if m else None)(re.search(k, l))
frames = []  # (begin_ns, end_ns, prev_ns, refresh_ns, buffer_count)
for l in show('process == "WindowServer" AND subsystem == "com.apple.SkyLight" AND category == "tracing"'):
    if 'FrameLifetime' not in l: continue
    b, e, p, r = (num(k, l) for k in (r'#begin_time#_##_#(\d+)', r'#end_time#_##_#(\d+)', r'prev_frame=(\d+)',
                                      r'refresh_interval=(\d+)'))
    if b and e and r: frames.append((b, e, p or 0, r, num(r'buffer_count=(\d+)', l)))
print(f'{len(probe)} present records, {len(frames)} frames')
if not probe: sys.exit(0)

P = 1 / 60
sites = collections.Counter(s for _, s, _, _, _ in probe)
print('branch:', dict(sites))
age = collections.Counter()      # now - S, in refreshes (S = the vblank the update was scheduled for)
lead = collections.Counter()     # target - now, in refreshes (> 0: future target, <= 0: present at once)
for mt, site, now, S, tgt in probe:
    age['S=0' if S == 0 else min(max(round((now - S) / P), -2), 6)] += 1
    lead[min(max(round((tgt - now) / P * 2) / 2, -2), 4)] += 1
print('now - S (refreshes):', sorted(age.items(), key=lambda kv: str(kv[0])))
print('target - now (refreshes):', sorted(lead.items()))

# Join: the present record of a frame is the last one with mach_ns in [begin, end].
probe.sort()
ts = [p[0] for p in probe]
import bisect
rows = collections.Counter()
glass_s = collections.Counter()   # glass - S in refreshes: CoreAnimation draws for S, the frame shows at glass
for b, e, p, r, bc in frames:
    i = bisect.bisect_right(ts, e) - 1
    if i >= 0 and ts[i] >= b:
        glass_s[round((e / 1e9 - probe[i][3]) / (r / 1e9))] += 1
    i = bisect.bisect_right(ts, e) - 1
    if i < 0 or ts[i] < b: rows[('no present record', round((e - b) / r))] += 1; continue
    mt, site, now, S, tgt = probe[i]
    want_ahead = (tgt - now) > 0.004            # target at least 4 ms in the future
    glass_after_call = (e - mt) / 1e6           # ms from the present call to the frame going up
    rows[(site, 'future target' if want_ahead else 'past target', round(glass_after_call / (r / 1e6)))] += 1
print('glass - S (refreshes; constant = even motion):', sorted(glass_s.items()))
print('frames by (branch, target, refreshes from present call to glass):')
for k, v in sorted(rows.items(), key=lambda kv: -kv[1])[:20]: print(f'  {v:5d}  {k}')
