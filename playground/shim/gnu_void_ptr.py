#!/usr/bin/env python3
"""Rewrite GCC's void-pointer arithmetic into explicit byte arithmetic.

TEMPO's SampleManager.h and SliceEngine.h add offsets to void* pointers. GCC
accepts that as an extension (it treats sizeof(void) as 1); clang, which
Emscripten uses, rejects it in C++. This writes a copy of a firmware header
with each such expression cast through char*, which means exactly what GCC
does, and the Makefile points the compiler at the copy with a VFS overlay.
The firmware file itself is never modified.

Only the void* variables listed below are touched, and the script fails if
it finds none, so a firmware change that makes it stale is caught at build
time.

usage: gnu_void_ptr.py <in.h> <out.h>
"""
import re
import sys

VOID_PTRS = r"(?:start_addr|sample_start_|sdram_[a-z_]+_)"

src = open(sys.argv[1]).read()

# p += n   ->   p = (char*)p + n
out, n1 = re.subn(rf"\b({VOID_PTRS})\s*\+=\s*", r"\1 = (char*)\1 + ", src)
# p + n    ->   (char*)p + n      (not ++, not the p = (char*)p we just wrote)
out, n2 = re.subn(rf"(?<!\(char\*\))\b({VOID_PTRS})\s*\+(?![+=])", r"(char*)\1 +", out)

if n1 + n2 == 0:
    sys.exit(f"{sys.argv[1]}: no void* arithmetic found; update {sys.argv[0]}")
open(sys.argv[2], "w").write(out)
