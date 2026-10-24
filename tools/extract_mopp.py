"""Dump the MOPP bytecode of every bhkMoppBvTreeShape in .nif files, for `moppcheck <dump.bin>`.

    python extract_mopp.py <folder or .nif> [more ...] -o mopp.bin

Loose files only (standard library); for meshes inside .bsa archives extract them first (BSA Browser, BAE) or
import iter_mopp() from your own BSA reader. Record format: u32 codeSize, u32 nameSize, name (utf-8), code.
"""
import argparse, os, struct, sys

# bhkMoppBvTreeShape (NIF 20.2.0.7, Skyrim LE/SE): shape ref (4), unused (12), shape scale (4),
# MOPP data size (4), origin (12), scale (4), build type (1), MOPP data
MOPP_HEADER = 41


def blocks(data):
    """(type name, block bytes) of a Skyrim NIF."""
    p = data.index(b'\n') + 1
    version, _endian, _user, nblocks, bsver = struct.unpack_from('<IBIII', data, p)
    p += 17
    for _ in range(4 if bsver >= 130 else 3):  # author, process script, export script (+ FO4 field)
        p += 1 + data[p]
    ntypes = struct.unpack_from('<H', data, p)[0]; p += 2
    types = []
    for _ in range(ntypes):
        n = struct.unpack_from('<I', data, p)[0]; p += 4
        types.append(data[p:p + n].decode('latin1')); p += n
    kinds = struct.unpack_from(f'<{nblocks}H', data, p); p += 2 * nblocks
    sizes = struct.unpack_from(f'<{nblocks}I', data, p); p += 4 * nblocks
    nstrings, _ = struct.unpack_from('<II', data, p); p += 8
    for _ in range(nstrings):
        p += 4 + struct.unpack_from('<I', data, p)[0]
    ngroups = struct.unpack_from('<I', data, p)[0]; p += 4 + 4 * ngroups
    for i in range(nblocks):
        yield types[kinds[i] & 0x7FFF], data[p:p + sizes[i]]
        p += sizes[i]


def iter_mopp(data):
    """MOPP bytecode of every bhkMoppBvTreeShape in a NIF; a block that does not match the layout is skipped."""
    if b'bhkMoppBvTreeShape' not in data:
        return
    for kind, block in blocks(data):
        if kind == 'bhkMoppBvTreeShape' and len(block) >= MOPP_HEADER:
            size = struct.unpack_from('<I', block, 20)[0]
            if len(block) == MOPP_HEADER + size:
                yield block[MOPP_HEADER:]


def iter_nifs(paths):
    for src in paths:
        if os.path.isfile(src):
            yield src
            continue
        for root, _, files in os.walk(src):
            for name in files:
                if name.lower().endswith('.nif'):
                    yield os.path.join(root, name)


def write_record(out, name, code):
    raw = name.encode('utf-8')
    out.write(struct.pack('<II', len(code), len(raw)) + raw + code)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('paths', nargs='+')
    ap.add_argument('-o', '--out', required=True)
    args = ap.parse_args()
    nifs = buffers = errors = 0
    with open(args.out, 'wb') as out:
        for path in iter_nifs(args.paths):
            nifs += 1
            try:
                for i, code in enumerate(iter_mopp(open(path, 'rb').read())):
                    write_record(out, f'{path}#{i}', code)
                    buffers += 1
            except (ValueError, struct.error, IndexError):
                errors += 1
    print(f'nifs={nifs} mopp={buffers} unreadable={errors} -> {args.out}')


if __name__ == '__main__':
    sys.exit(main())
