#!/usr/bin/env python3
import os
import struct
import sys

SECTOR = 512

def collect(root):
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        if rel == '.':
            rel = ''
        for d in dirnames:
            relpath = os.path.join(rel, d).replace('\\', '/')
            entries.append((relpath, os.path.join(dirpath, d), 0))
        for f in filenames:
            relpath = os.path.join(rel, f).replace('\\', '/')
            entries.append((relpath, os.path.join(dirpath, f), 1))
    entries.sort(key=lambda x: (x[0] != '0000Kernel.BIN.HM', x[0]))
    return entries

def main():
    if len(sys.argv) != 3:
        print("usage: pack_files.py <input_dir> <output_bin>")
        sys.exit(1)

    root = sys.argv[1]
    out_path = sys.argv[2]

    entries = collect(root)

    table_size = 0
    for rel, full, ftype in entries:
        table_size += 32
    table_size += 2

    content_start = 2 + table_size
    pad = (SECTOR - (content_start % SECTOR)) % SECTOR
    content_start += pad
    cur_sector = content_start // SECTOR + 4

    table = b''
    blobs = []

    for rel, full, ftype in entries:
        name = rel.encode('utf-8')

        if ftype == 1:
            with open(full, 'rb') as fp:
                data = fp.read()
            payload = name + data
        else:
            payload = name

        total = len(payload)
        secs = (total + SECTOR - 1) // SECTOR
        if secs == 0:
            secs = 0
        payload += b'\x00' * (secs * SECTOR - total)

        table += struct.pack('<QQQQ', cur_sector, secs, len(name), ftype)
        blobs.append(payload)
        cur_sector += secs

    with open(out_path, 'wb') as fp:
        fp.write(struct.pack('<H', 0xFDFD))
        fp.write(table)
        fp.write(struct.pack('<H', 0xDFDF))
        fp.write(b'\x00' * pad)
        for b in blobs:
            fp.write(b)

if __name__ == '__main__':
    main()
