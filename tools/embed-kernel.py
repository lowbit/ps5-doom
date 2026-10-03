#!/usr/bin/env python3
"""Turns a linked AMDGPU code object into a C header for the PS5 compute path.

The PS5 path programs the compute registers itself, so it only supports kernels whose
launch needs nothing but the kernel argument pointer (after an unused private segment
buffer, if the compiler reserves one), wave64, no scratch and no LDS. Anything else fails
the build instead of failing on the console.
"""
import struct
import sys

PT_LOAD = 1
PT_DYNAMIC = 2
SHT_SYMTAB = 2
DT_RELA, DT_RELASZ, DT_REL, DT_RELSZ = 7, 8, 17, 18

PROP_PRIVATE_BUFFER = 1 << 0
PROP_KERNARG_PTR = 1 << 3
PROP_WAVE32 = 1 << 10


def fail(message):
    sys.exit(f"embed-kernel: {message}")


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: embed-kernel.py <code-object> <kernel> <header>")
    path, kernel, header = sys.argv[1:]
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        fail("not an ELF64 file")

    phoff, shoff = struct.unpack_from("<QQ", data, 0x20)
    phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", data, 0x36)

    segments = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize) for i in range(phnum)]
    loads = [s for s in segments if s[0] == PT_LOAD]
    image_size = max(s[3] + s[6] for s in loads)
    image = bytearray(image_size)
    for _, _, offset, vaddr, _, filesz, _, _ in loads:
        image[vaddr:vaddr + filesz] = data[offset:offset + filesz]

    for seg in segments:
        if seg[0] != PT_DYNAMIC:
            continue
        for i in range(0, seg[5], 16):
            tag, value = struct.unpack_from("<qQ", data, seg[2] + i)
            if tag in (DT_RELASZ, DT_RELSZ) and value:
                fail("code object needs relocations")

    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
    symbols = {}
    for sec in sections:
        if sec[1] != SHT_SYMTAB:
            continue
        strtab = sections[sec[6]]
        for i in range(0, sec[5], 24):
            name, _, _, _, value, size = struct.unpack_from("<IBBHQQ", data, sec[4] + i)
            end = data.index(b"\0", strtab[4] + name)
            symbols[data[strtab[4] + name:end].decode()] = (value, size)

    if kernel not in symbols or kernel + ".kd" not in symbols:
        fail(f"kernel {kernel} or its descriptor is missing")
    entry = symbols[kernel][0]
    descriptor = symbols[kernel + ".kd"][0]
    (group, private, kernarg_size) = struct.unpack_from("<III", image, descriptor)
    (entry_offset,) = struct.unpack_from("<q", image, descriptor + 16)
    rsrc3, rsrc1, rsrc2 = struct.unpack_from("<III", image, descriptor + 44)
    (properties,) = struct.unpack_from("<H", image, descriptor + 56)

    if descriptor + entry_offset != entry or entry % 256:
        fail("kernel entry does not match its descriptor or is misaligned")
    if group or private or rsrc2 & 1:
        fail("kernel uses LDS or scratch")
    supported = PROP_PRIVATE_BUFFER | PROP_KERNARG_PTR
    if properties & PROP_WAVE32:
        fail("kernel is wave32; the PS5 dispatch path runs compute as wave64")
    if properties & ~supported or not properties & PROP_KERNARG_PTR:
        fail(f"kernel needs unsupported launch inputs ({properties:#x})")
    kernarg_sgpr = 4 if properties & PROP_PRIVATE_BUFFER else 0
    user_sgprs = (rsrc2 >> 1) & 0x1f
    if user_sgprs != kernarg_sgpr + 2:
        fail(f"unexpected user SGPR count {user_sgprs}")

    with open(header, "w") as out:
        out.write(f"#define KERNEL_ENTRY {entry:#x}\n")
        out.write(f"#define KERNEL_KERNARG_SIZE {kernarg_size}\n")
        out.write(f"#define KERNEL_KERNARG_SGPR {kernarg_sgpr}\n")
        out.write(f"#define KERNEL_USER_SGPRS {user_sgprs}\n")
        out.write(f"#define KERNEL_RSRC1 {rsrc1:#010x}u\n")
        out.write(f"#define KERNEL_RSRC2 {rsrc2:#010x}u\n")
        out.write(f"#define KERNEL_RSRC3 {rsrc3:#010x}u\n")
        out.write("static const unsigned char kernel_image[] = {\n")
        for i in range(0, len(image), 16):
            out.write("    " + ", ".join(f"0x{b:02x}" for b in image[i:i + 16]) + ",\n")
        out.write("};\n")


main()
