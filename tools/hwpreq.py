import sys
# Decode voltageshift "read 0x774" output lines (8 bytes, MSB first) into IA32_HWP_REQUEST fields.
for line in sys.stdin:
    bits = line.strip().strip('()').replace(' ', '')
    if len(bits) != 64:
        continue
    v = int(bits, 2)
    print(f'0x{v:016X}  min={v & 0xFF} max={(v >> 8) & 0xFF} desired={(v >> 16) & 0xFF} '
          f'epp=0x{(v >> 24) & 0xFF:02X} window=0x{(v >> 32) & 0x3FF:03X} pkg_ctl={(v >> 42) & 1}')
