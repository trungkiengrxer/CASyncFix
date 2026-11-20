import sys, os, collections, subprocess, xml.etree.ElementTree as ET
# kcalls.py <trace> <atos_pid> : WindowServer main thread (Time Profiler).
#  1) share of key CoreAnimation/Metal functions (surface allocation, encoders, submission)
#  2) kernel time (leaf mach_msg2_trap) grouped by the first meaningful caller

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
    if p is not None and 'WindowServer' in p.attrib.get('fmt', '') and t.attrib.get('fmt', '').startswith('Main Thread'):
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
    sym.update({a: (o.split(' (in ')[0][:100] if ' (in ' in o else a) for a, o in zip(ch, out)})
named = [[sym.get(a, a) for a in s] for s in samples]
total = len(named)
print(f'WindowServer main-thread samples (ms): {total}')

keys = ['CA::OGL::render(', 'WS::Updater::prepare_coreanimation', 'ImagingNode::render', 'ImagingNode::retain_surface',
        'create_surface', 'release_surface', 'newTextureWithDescriptor', 'IOAccelResourceCreate', 'MTLIOAccelResource dealloc',
        'set_surface', 'start_render_encoder', 'endEncoding', 'submitCommandBuffer', 'IOAccelCommandQueueSubmitCommandBuffers',
        'IOSurface', 'capture_backdrop', 'blur', 'Shadow', 'mach_msg2_trap', '__kdebug_trace64']
print('--- function share (inclusive ms, % of main thread)')
for k in keys:
    n = sum(1 for s in named if any(k in f for f in s))
    if n: print(f'{n:6d} {100 * n / total:5.1f}%  {k}')

skip = ('mach_msg', 'io_connect', 'IOConnectCall', 'iokit_', 'IOAccelResourceCreate', '_IOSurfaceClient', 'trap')
grp = collections.Counter()
for s in named:
    if s and s[0] == 'mach_msg2_trap':
        callers = [f for f in s[1:] if not any(k in f for k in skip)][:2]
        grp[' < '.join(callers)] += 1
print('--- kernel time (leaf mach_msg2_trap) by caller')
for k, v in grp.most_common(12):
    print(f'{v:6d}  {k}')
