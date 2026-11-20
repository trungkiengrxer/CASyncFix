import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# trace_cmp.py <trace>... : frame pacing on Display 1 + WindowServer main-thread CPU (ms) per trace.

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
    if node is None or node.find('schema') is None:
        return
    cols = [c.findtext('mnemonic') for c in node.find('schema').findall('col')]
    ids = {}
    def reg(e):
        if 'id' in e.attrib: ids[e.attrib['id']] = e
        for c in e: reg(c)
    for r in node.findall('row'):
        reg(r)
        yield {c: (ids[e.attrib['ref']] if 'ref' in e.attrib else e) for c, e in zip(cols, list(r))}

fmt = lambda e: e.attrib.get('fmt', '') if e is not None else ''
ms = lambda e: int(e.text) / 1e6

for trace in sys.argv[1:]:
    disp = [ms(r['duration']) for r in rows(export(trace, 'displayed-surfaces-interval'))
            if fmt(r['display-name']) == 'Display 1']
    anim = [d for d in disp if d < 200]          # drop idle holds of a static screen
    missed = sum(d > 20 for d in anim)
    busy = collections.Counter()
    for r in rows(export(trace, 'time-profile')):
        if 'WindowServer' in fmt(r.get('process')) and fmt(r.get('thread')).startswith('Main Thread'):
            t = r.get('time') if 'time' in r else r.get('sample-time')
            busy[int(int(t.text) // 500_000_000)] += 1
    peak = sorted(busy.values())[-6:]
    print(f'{os.path.basename(trace):24s} swaps={len(anim):4d} missed>20ms={missed:4d} '
          f'({100 * missed / max(1, len(anim)):4.1f}%)  WS main CPU total={sum(busy.values())}ms '
          f'peak 500ms windows={peak}')
