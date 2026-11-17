#!/usr/bin/env python3
"""Rebuild a (possibly sparse) GameCube disc image from a Dolphin "Extract Entire Disc" tree.

Usage: rebuild_iso.py <extracted root containing sys/ and files/> <out.iso> [--list]

Every file is placed at the exact disc offset the original FST gives it, so the result is
byte-identical to the original disc wherever the source file is present. Files missing from
the tree are left as holes (zeros) and reported -- useful for development machines that only
hold part of the game. --list prints the FST and exits.

This tool never downloads or redistributes game data; it only reassembles files the user
extracted from their own disc.
"""
import os
import struct
import sys

DISC_SIZE = 0x57058000  # standard GameCube mini-DVD image size


def parse_fst(fst):
    n = struct.unpack('>I', fst[8:12])[0]
    strtab = n * 12
    entries = []
    stack = [('', n)]  # (path prefix, end index)
    i = 1
    while i < n:
        while stack and i >= stack[-1][1]:
            stack.pop()
        w0, off, size = struct.unpack('>III', fst[i * 12:i * 12 + 12])
        is_dir = w0 >> 24
        name_off = w0 & 0xFFFFFF
        end = fst.index(b'\0', strtab + name_off)
        name = fst[strtab + name_off:end].decode('shift_jis', 'replace')
        prefix = stack[-1][0] if stack else ''
        path = prefix + name
        if is_dir:
            stack.append((path + '/', size))
        else:
            entries.append((path, off, size))
        i += 1
    return entries


def find_ci(root, rel):
    """Case-insensitive lookup (FST names and extracted names can differ in case on Windows)."""
    cur = root
    for part in rel.split('/'):
        if not os.path.isdir(cur):
            return None
        hit = next((e for e in os.listdir(cur) if e.lower() == part.lower()), None)
        if hit is None:
            return None
        cur = os.path.join(cur, hit)
    return cur


def main():
    root, out = sys.argv[1], sys.argv[2]
    sysd = os.path.join(root, 'sys')
    boot = open(os.path.join(sysd, 'boot.bin'), 'rb').read()
    bi2 = open(os.path.join(sysd, 'bi2.bin'), 'rb').read()
    apl = open(os.path.join(sysd, 'apploader.img'), 'rb').read()
    fst = open(os.path.join(sysd, 'fst.bin'), 'rb').read()
    dol = open(os.path.join(sysd, 'main.dol'), 'rb').read()
    dol_off, fst_off = struct.unpack('>II', boot[0x420:0x428])
    entries = parse_fst(fst)
    if '--list' in sys.argv:
        for p, o, s in entries:
            print(f'{o:08X} {s:10d} {p}')
        return
    present = missing = 0
    with open(out, 'wb') as f:
        f.truncate(DISC_SIZE)  # sparse on filesystems that support it
        for off, data in ((0, boot), (0x440, bi2), (0x2440, apl), (dol_off, dol), (fst_off, fst)):
            f.seek(off)
            f.write(data)
        for path, off, size in entries:
            src = find_ci(os.path.join(root, 'files'), path)
            if src and os.path.getsize(src) == size:
                f.seek(off)
                with open(src, 'rb') as s:
                    while True:
                        chunk = s.read(1 << 22)
                        if not chunk:
                            break
                        f.write(chunk)
                present += 1
            else:
                missing += 1
                print(f'missing: {path} ({size} bytes)', file=sys.stderr)
    print(f'{out}: {present} files placed, {missing} missing')


if __name__ == '__main__':
    main()
