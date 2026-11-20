import sys, collections, subprocess, xml.etree.ElementTree as ET
# syscalls.py <syscall.xml> <atos_pid> [process] [thread-prefix]
# Per syscall: count, total wait, and how many waits fall in frame-sized buckets; top user callers for long waits.
path, apid = sys.argv[1], sys.argv[2]
proc = sys.argv[3] if len(sys.argv) > 3 else 'WindowServer'
tpre = sys.argv[4] if len(sys.argv) > 4 else 'Main Thread'
ids = {}; cols = None
agg = collections.defaultdict(lambda: [0, 0.0, 0.0, 0])  # count, wait ms, cpu ms, waits >= 2ms
callers = collections.defaultdict(collections.Counter)
for ev, el in ET.iterparse(path, events=('end',)):
    if el.tag == 'schema':
        cols = [c.findtext('mnemonic') for c in el.findall('col')]
    if el.tag != 'row':
        continue
    def reg(e):
        if 'id' in e.attrib: ids[e.attrib['id']] = e
        for c in e: reg(c)
    reg(el)
    d = {c: (ids[e.attrib['ref']] if 'ref' in e.attrib else e) for c, e in zip(cols, list(el))}
    t = d.get('thread'); tf = t.attrib.get('fmt', '') if t is not None else ''
    if proc in tf and tf.startswith(tpre):
        name = d['call'].attrib.get('fmt', '?')
        w = int(d['waittime'].text) / 1e6 if d.get('waittime') is not None and d['waittime'].text else 0.0
        c = int(d['cputime'].text) / 1e6 if d.get('cputime') is not None and d['cputime'].text else 0.0
        a = agg[name]; a[0] += 1; a[1] += w; a[2] += c
        if w >= 2:
            a[3] += 1
            bt = d.get('backtrace')
            if bt is not None:
                fr = [(ids[f.attrib['ref']] if 'ref' in f.attrib else f).attrib.get('addr', '') for f in bt.iter('frame')]
                callers[name][tuple(fr[:8])] += w
    el.clear()
print(f'{"syscall":34s} {"count":>6s} {"wait ms":>9s} {"cpu ms":>8s} {"waits>=2ms":>10s}')
for n, (cnt, w, c, big) in sorted(agg.items(), key=lambda kv: -kv[1][1])[:14]:
    print(f'{n[:34]:34s} {cnt:6d} {w:9.1f} {c:8.1f} {big:10d}')
# symbolicate callers of the top long-wait syscalls
top = sorted(callers, key=lambda n: -sum(callers[n].values()))[:3]
addrs = sorted({a for n in top for fr in callers[n] for a in fr if a.startswith('0x7ff')})
out = subprocess.run(['atos', '-p', apid] + addrs, capture_output=True, text=True).stdout.splitlines() if addrs else []
sym = {a: (o.split(' (in ')[0][:80] if ' (in ' in o else a) for a, o in zip(addrs, out)}
for n in top:
    print(f'\n== long waits in {n}, by caller (ms)')
    for fr, w in callers[n].most_common(5):
        names = [sym.get(a, a) for a in fr if not any(k in sym.get(a, '') for k in ('mach_msg', 'trap'))][:4]
        print(f'{w:8.1f}  ' + ' < '.join(names))
