#!/usr/bin/env python3
"""Writes a C header that holds a file's bytes as a static array: embed-file.py <file> <name> <header>."""
import sys

source, name, header = sys.argv[1:]
data = open(source, "rb").read()
with open(header, "w", newline="\n") as out:
    out.write(f"/* Generated from {source} by tools/embed-file.py. */\n")
    out.write(f"static const unsigned char {name}[] = {{\n")
    for i in range(0, len(data), 20):
        out.write("    " + ", ".join(str(b) for b in data[i:i + 20]) + ",\n")
    out.write("};\n")
