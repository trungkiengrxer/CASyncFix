import sys, os, subprocess, statistics, xml.etree.ElementTree as ET
# completion_latency.py <trace> : for WindowServer command buffers, time from last GPU work end to the
# CPU-side "completed" event. Large values = slow completion signalling (interrupt -> driver -> wakeup).

def export(trace, schema):
    out = f'{trace}.{schema}.xml'
    if not os.path.exists(out):
        with open(out, 'w') as f:
            subprocess.run(['xctrace', 'export', '--input', trace, '--xpath',
                            f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],
                           stdout=f, stderr=subprocess.DEVNULL)
    return out

def rows(path):
    node = ET.parse(path).getroot().find('node')
    cols = [c.findtext('mnemonic') for c in node.find('schema').findall('col')]
    ids = {}
    def reg(e):
        if 'id' in e.attrib: ids[e.attrib['id']] = e
        for c in e: reg(c)
    for r in node.findall('row'):
        reg(r)
        yield {c: (ids[e.attrib['ref']] if 'ref' in e.attrib else e) for c, e in zip(cols, list(r))}

trace = sys.argv[1]
gpu_end = {}; gpu_start = {}
for r in rows(export(trace, 'metal-gpu-intervals')):
    if not r['process'].attrib.get('fmt', '').startswith('WindowServer'):
        continue
    cb = int(r['cmdbuffer-id'].text)
    s = int(r['start'].text); e = s + int(r['duration'].text)
    gpu_end[cb] = max(gpu_end.get(cb, 0), e); gpu_start[cb] = min(gpu_start.get(cb, s), s)
lat = []
for r in rows(export(trace, 'metal-command-buffer-completed')):
    cb = int(r['cmdbuffer-id'].text)
    if cb in gpu_end:
        lat.append((int(r['timestamp'].text) - gpu_end[cb]) / 1e6)
lat.sort()
q = lambda p: lat[min(len(lat) - 1, int(len(lat) * p))]
print(f'{os.path.basename(trace)}: WindowServer cmdbufs matched={len(lat)}')
print(f'  GPU-end -> completed (ms): p50={q(.5):.2f} p90={q(.9):.2f} p99={q(.99):.2f} max={lat[-1]:.2f} '
      f'mean={statistics.mean(lat):.2f}')
print(f'  >2ms: {sum(x > 2 for x in lat)}  >5ms: {sum(x > 5 for x in lat)}  >10ms: {sum(x > 10 for x in lat)}')
