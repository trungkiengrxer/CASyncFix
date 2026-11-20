import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# timeline.py <trace> [bucket_ms] : per time bucket, frames shown, frame holds, GPU busy per process,
# and CPU ms per process/thread (WindowServer main, Dock, Music...). Also lists long holds.
# Works on a Time Profiler-only trace too (frame/GPU columns stay empty).

def export(trace, schema):
    out = f'{trace}.{schema}.xml'
    if not os.path.exists(out):
        with open(out, 'w') as f:
            subprocess.run(['xctrace', 'export', '--input', trace, '--xpath',
                            f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],
                           stdout=f, stderr=subprocess.DEVNULL)
    return out

def stream(path):
    ids = {}; cols = None
    for ev, el in ET.iterparse(path, events=('end',)):
        if el.tag == 'schema':
            cols = [c.findtext('mnemonic') for c in el.findall('col')]
        if el.tag != 'row':
            continue
        def reg(e):
            if 'id' in e.attrib: ids[e.attrib['id']] = e
            for c in e: reg(c)
        reg(el)
        yield {c: (ids[e.attrib['ref']] if 'ref' in e.attrib else e) for c, e in zip(cols, list(el))}
        el.clear()

fmt = lambda e: e.attrib.get('fmt', '') if e is not None else ''
trace = sys.argv[1]; B = int(sys.argv[2]) if len(sys.argv) > 2 else 250

frames = sorted((int(r['start'].text) / 1e6, int(r['duration'].text) / 1e6)
                for r in stream(export(trace, 'displayed-surfaces-interval')) if fmt(r['display-name']) == 'Display 1')
swaps = collections.Counter(); worst = collections.Counter()
for s, d in frames:
    k = int(s // B); swaps[k] += 1; worst[k] = max(worst[k], d)

gpu = collections.defaultdict(collections.Counter)
for r in stream(export(trace, 'metal-gpu-intervals')):
    s = int(r['start'].text) / 1e6
    proc = fmt(r['process']).split(' (')[0] or '?'
    gname = 'NV' if 'NVIDIA' in fmt(r['gpu']) or 'Quadro' in fmt(r['gpu']) or 'NV' in fmt(r['gpu'])[:3] else 'IG'
    gpu[int(s // B)][f'{gname}:{proc[:14]}'] += int(r['duration'].text) / 1e6

cpu = collections.defaultdict(collections.Counter); cpu_tot = collections.Counter()
for r in stream(export(trace, 'time-profile')):
    p = fmt(r.get('process')).split(' (')[0]
    t = fmt(r.get('thread'))
    tm = r.get('time') if 'time' in r else r.get('sample-time')
    key = 'WS-main' if p == 'WindowServer' and t.startswith('Main Thread') else p
    k = int(int(tm.text) / 1e6 // B)
    cpu[k][key] += 1; cpu_tot[key] += 1

top_cpu = [p for p, _ in cpu_tot.most_common(6)]
print(f'top CPU processes over the trace (ms): {cpu_tot.most_common(8)}')
print(f'\n{"t(s)":>6s} {"swaps":>5s} {"worst":>6s}  {"GPU ms (top)":44s} CPU ms ' + ' '.join(f'{p[:10]:>10s}' for p in top_cpu))
ks = swaps or cpu
for k in range(min(ks), max(ks) + 1):
    g = ', '.join(f'{n}={v:.0f}' for n, v in gpu[k].most_common(2))
    print(f'{k * B / 1000:6.2f} {swaps[k]:5d} {worst[k]:6.0f}  {g[:44]:44s}        ' + ' '.join(f'{cpu[k][p]:10d}' for p in top_cpu))

print('\nholds >= 50 ms:', '  '.join(f'{s / 1000:.2f}s:{d:.0f}' for s, d in frames if d >= 50))
