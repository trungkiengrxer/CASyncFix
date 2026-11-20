import sys, collections, xml.etree.ElementTree as ET
# thread_states.py <thread-state.xml> [process] [thread-prefix]
# Sum time per state (Running/Blocked/Runnable/...) for one thread, plus the longest blocked/runnable intervals.
path = sys.argv[1]
proc = sys.argv[2] if len(sys.argv) > 2 else 'WindowServer'
tpre = sys.argv[3] if len(sys.argv) > 3 else 'Main Thread'
ids = {}
tot = collections.Counter(); notes = collections.Counter(); longest = []
cols = None
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
    t = d.get('thread')
    tf = t.attrib.get('fmt', '') if t is not None else ''
    if proc in tf and tf.startswith(tpre):
        st = d['state'].attrib.get('fmt', '')
        dur = int(d['duration'].text) / 1e6
        tot[st] += dur
        note = d.get('note'); nf = note.attrib.get('fmt', '') if note is not None else ''
        if st != 'Running':
            notes[(st, nf[:90])] += dur
            longest.append((dur, int(d['start'].text) / 1e9, st, nf[:90]))
    el.clear()
print('state totals (ms):', {k: round(v) for k, v in tot.most_common()})
print('--- non-running time by note (ms)')
for (st, nf), v in notes.most_common(15):
    print(f'{v:8.1f}  {st:10s} {nf}')
print('--- longest non-running intervals')
for dur, s, st, nf in sorted(longest, reverse=True)[:12]:
    print(f'{dur:7.2f}ms at {s:6.3f}s {st:10s} {nf}')
# interval histogram of blocked spans (frame-relevant sizes)
h = collections.Counter()
for dur, _, st, _ in longest:
    if st != 'Running':
        h['<1ms' if dur < 1 else '1-4ms' if dur < 4 else '4-8ms' if dur < 8 else '8-16ms' if dur < 16 else '>=16ms'] += 1
print('blocked/runnable span histogram:', dict(h))
