import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# start_wait.py <trace> : for WindowServer GPU encoders that waited >= 2 ms between submission and GPU start,
# what was the GPU doing during that wait? (idle vs. running another process's work)

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
iv = []  # (start, end, process, latency)
for r in rows(export(trace, 'metal-gpu-intervals')):
    if r['gpu'].attrib.get('fmt', '').find('Intel') < 0:
        continue
    s = int(r['start'].text); e = s + int(r['duration'].text)
    lat = int(r['start-latency'].text) if r['start-latency'].text and int(r['start-latency'].text) < 10**12 else 0
    iv.append((s, e, r['process'].attrib.get('fmt', '?').split(' (')[0], lat))
iv.sort()
busy_by = collections.Counter(); idle = 0.0; waits = 0; wait_total = 0.0
for s, e, p, lat in iv:
    if not p.startswith('WindowServer') or lat < 2_000_000:
        continue
    waits += 1; wait_total += lat / 1e6
    a, b = s - lat, s
    covered = 0
    for s2, e2, p2, _ in iv:
        if e2 <= a or s2 >= b:
            continue
        ov = min(b, e2) - max(a, s2)
        busy_by[p2] += ov / 1e6; covered += ov
    idle += max(0, (b - a) - covered) / 1e6
print(f'{os.path.basename(trace)}: WindowServer encoders with start wait >= 2ms: {waits}, total wait {wait_total:.1f} ms')
print(f'  GPU idle during those waits: {idle:.1f} ms (overlap can double-count, see below)')
for p, v in busy_by.most_common(8):
    print(f'  GPU running {p:40s} {v:8.1f} ms')
