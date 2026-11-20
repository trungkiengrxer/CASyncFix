import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# passes.py <trace> <atos_pid> : from a Time Profiler trace, WindowServer main-thread update passes
# (runs of consecutive samples inside CGXUpdateDisplay) -> pass duration and pass-to-pass gap distributions.
# Long passes with long gaps = load-bound. Short passes with irregular gaps = pacing/vsync-bound.
# atos_pid: a process with the same frameworks loaded (tools/symhost), since atos cannot attach to WindowServer.

def export(trace, schema):
    out = f'{trace}.{schema}.xml'
    if not os.path.exists(out):
        with open(out, 'w') as f:
            subprocess.run(['xctrace', 'export', '--input', trace, '--xpath',
                            f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],
                           stdout=f, stderr=subprocess.DEVNULL)
    return out

trace, apid = sys.argv[1], sys.argv[2]
ids = {}; cols = None; samples = []
for ev, el in ET.iterparse(export(trace, 'time-profile'), events=('end',)):
    if el.tag == 'schema':
        cols = [c.findtext('mnemonic') for c in el.findall('col')]
    if el.tag != 'row':
        continue
    def reg(e):
        if 'id' in e.attrib: ids[e.attrib['id']] = e
        for c in e: reg(c)
    reg(el)
    R = lambda e: ids[e.attrib['ref']] if e is not None and 'ref' in e.attrib else e
    d = {c: R(e) for c, e in zip(cols, list(el))}
    p, t = d.get('process'), d.get('thread')
    tm = d.get('time') if 'time' in d else d.get('sample-time')
    if p is not None and 'WindowServer' in p.attrib.get('fmt', '') and t.attrib.get('fmt', '').startswith('Main Thread'):
        b = d.get('stack') if 'stack' in d else d.get('tagged-backtrace')
        if b is not None and tm is not None:
            if b.tag == 'tagged-backtrace': b = R(b.find('backtrace'))
            samples.append((int(tm.text) / 1e6, [R(f).attrib.get('addr', '') for f in b.findall('frame')]))
    el.clear()

addrs = sorted({a for _, s in samples for a in s if a.startswith('0x7ff')})
sym = {}
for i in range(0, len(addrs), 3000):
    ch = addrs[i:i + 3000]
    out = subprocess.run(['atos', '-p', apid] + ch, capture_output=True, text=True).stdout.splitlines()
    sym.update({a: o.split(' (in ')[0] for a, o in zip(ch, out)})

upd = sorted(ms for ms, s in samples if any(sym.get(a, '') == 'CGXUpdateDisplay' for a in s))
passes = []   # (start_ms, duration_ms)
for ms in upd:
    if passes and ms - (passes[-1][0] + passes[-1][1]) <= 1.5:
        passes[-1] = (passes[-1][0], ms - passes[-1][0] + 1)
    else:
        passes.append((ms, 1))

def pct(v, q): return sorted(v)[min(len(v) - 1, int(q * len(v)))] if v else 0
dur = [d for _, d in passes]
gaps = [b[0] - a[0] for a, b in zip(passes, passes[1:])]
print(f'{trace}: {len(passes)} update passes, WS main samples {len(samples)} ms')
print(f'  pass duration ms: p50 {pct(dur, .5):.0f}  p90 {pct(dur, .9):.0f}  p99 {pct(dur, .99):.0f}  max {max(dur, default=0):.0f}')
print(f'  passes >= 16 ms: {sum(d >= 16 for d in dur)} / {len(dur)}')
busy = [g for g in gaps if g < 200]   # ignore idle stretches between gestures
hist = collections.Counter(min(int(round(g / 16.67)), 6) for g in busy)
print('  pass-to-pass gap (vsync multiples, during activity): ' +
      '  '.join(f'{k}x={hist[k]}' for k in sorted(hist)))
short_but_late = sum(1 for (s, d), g in zip(passes, gaps) if d < 12 and 25 < g < 200)
print(f'  short passes (<12 ms) followed by a gap > 25 ms: {short_but_late}')

# What happened in the late gaps after short passes: other main-thread work (samples) or idle/blocked (no samples)?
late = [(s + d, s2) for (s, d), (s2, _) in zip(passes, passes[1:]) if d < 12 and 25 < s2 - s < 200]
busy_in_gap = collections.Counter(); idle_ms = busy_ms = 0
for a, b in late:
    inside = [st for ms, st in samples if a <= ms < b]
    busy_ms += len(inside); idle_ms += (b - a) - len(inside)
    for st in inside:
        names = [sym.get(x, x) for x in st]
        key = next((n for n in names if n in ('CGXRunOneEventPass', 'run_timer_pass', 'CGXRunOneServicesPass')), names[-1] if names else '?')
        inner = next((n for n in names[:12] if not n.startswith('0x') and 'mach_msg' not in n), '?')
        busy_in_gap[f'{key} / {inner[:70]}'] += 1
if late:
    print(f'  late gaps after short passes: {len(late)}, main thread busy {busy_ms} ms, idle/blocked {idle_ms:.0f} ms')
    for k, v in busy_in_gap.most_common(8): print(f'    {v:4d}  {k}')
