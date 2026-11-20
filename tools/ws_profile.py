import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# ws_profile.py <trace> <atos_pid> [topN] [start_s end_s] : symbolicated inclusive CPU profile of WindowServer's
# main thread, optionally only for samples inside [start_s, end_s).

def export(trace, schema):
    out = f'{trace}.{schema}.xml'
    if not os.path.exists(out):
        with open(out, 'w') as f:
            subprocess.run(['xctrace', 'export', '--input', trace, '--xpath',
                            f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],
                           stdout=f, stderr=subprocess.DEVNULL)
    return out

trace, apid = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 45
win = (float(sys.argv[4]), float(sys.argv[5])) if len(sys.argv) > 5 else None
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
    in_win = win is None or (tm is not None and win[0] <= int(tm.text) / 1e9 < win[1])
    if in_win and p is not None and 'WindowServer' in p.attrib.get('fmt', '') and t.attrib.get('fmt', '').startswith('Main Thread'):
        b = d.get('stack') if 'stack' in d else d.get('tagged-backtrace')
        if b is not None:
            if b.tag == 'tagged-backtrace': b = R(b.find('backtrace'))
            samples.append([R(f).attrib.get('addr', '') for f in b.findall('frame')])
    el.clear()

addrs = sorted({a for s in samples for a in s if a.startswith('0x7ff')})
sym = {}
for i in range(0, len(addrs), 3000):
    ch = addrs[i:i + 3000]
    out = subprocess.run(['atos', '-p', apid] + ch, capture_output=True, text=True).stdout.splitlines()
    sym.update({a: (o.split(' (in ')[0][:110] if ' (in ' in o else a) for a, o in zip(ch, out)})
incl = collections.Counter(); leaf = collections.Counter()
for s in samples:
    names = [sym.get(a, a) for a in s]
    if names: leaf[names[0]] += 1
    for n in set(names): incl[n] += 1
print(f'WindowServer main-thread samples (ms): {len(samples)}')
# FOCUS="substr1|substr2" env: for each, direct callers and callees (innermost matching frame of each sample), then exit.
for foc in filter(None, os.environ.get('FOCUS', '').split('|')):
    up = collections.Counter(); down = collections.Counter(); n = 0
    for s in samples:
        names = [sym.get(a, a) for a in s]                 # leaf first
        i = next((k for k, x in enumerate(names) if foc in x), None)
        if i is None: continue
        n += 1
        up[names[i + 1] if i + 1 < len(names) else '(root)'] += 1
        down[names[i - 1] if i else '(self)'] += 1
    print(f'=== {foc}: {n} ms'); print('  callers: ' + '; '.join(f'{c} {x[:60]}' for x, c in up.most_common(6)))
    print('  callees: ' + '; '.join(f'{c} {x[:60]}' for x, c in down.most_common(10)))
if os.environ.get('FOCUS'): sys.exit()
print('--- inclusive')
for n, c in incl.most_common(top):
    print(f'{c:6d}  {n}')
print('--- leaf')
for n, c in leaf.most_common(15):
    print(f'{c:6d}  {n}')
