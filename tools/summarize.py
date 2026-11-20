import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# summarize.py <tag>... : per scenario, WindowServer main-thread finish() waits (System Trace) and frame
# pacing (Metal System Trace). Expects traces/<tag>-sys.trace and traces/<tag>-metal.trace.

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

def finish_waits(trace):
    """WindowServer main thread: count/total of psynch_cvwait (waitUntilCompleted) >= 1 ms, and total blocked."""
    n = 0; total = 0.0
    for r in stream(export(trace, 'syscall')):
        t = fmt(r.get('thread'))
        if 'WindowServer' in t and t.startswith('Main Thread') and fmt(r['call']) == 'psynch_cvwait':
            w = int(r['waittime'].text) / 1e6 if r['waittime'] is not None and r['waittime'].text else 0
            if w >= 1:
                n += 1; total += w
    return n, total

def pacing(trace):
    d = [int(r['duration'].text) / 1e6 for r in stream(export(trace, 'displayed-surfaces-interval'))
         if fmt(r['display-name']) == 'Display 1']
    anim = [x for x in d if x < 200]
    missed = sum(x > 20 for x in anim)
    return len(anim), missed

print(f'{"scenario":22s} {"finish waits":>12s} {"wait ms":>8s} {"swaps":>6s} {"missed>20ms":>12s}')
for tag in sys.argv[1:]:
    n, w = finish_waits(f'traces/{tag}-sys.trace')
    s, m = pacing(f'traces/{tag}-metal.trace')
    print(f'{tag:22s} {n:12d} {w:8.0f} {s:6d} {m:6d} ({100 * m / max(1, s):4.1f}%)')
