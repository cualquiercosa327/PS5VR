#!/usr/bin/env python3
"""Read decrypted PS5 PRX/SPRX files (ps5-self-pager output).

For each library it reports the module, the libraries it exports and imports,
every exported symbol (address, size) and every import (GOT slot, PLT stub),
with NIDs resolved to names wherever a candidate name hashes to the NID.

Candidate names come from identifiers found in the input files themselves,
from any --names files/dirs (e.g. KytyPS5 sources), and from names.txt next
to this script, which accumulates every name that has resolved so far.

--elf-dir writes a copy of each library with section headers and a symbol
table added, so llvm-objdump / Ghidra show sceXxx names instead of raw NIDs.
--disasm prints annotated disassembly of chosen exports from that copy.
"""

import argparse
import base64
import hashlib
import itertools
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

NID_SUFFIX = bytes.fromhex("518D64A635DED8C1E6B039B1C3E55230")
B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
NAMES_DB = Path(__file__).with_name("names.txt")

PT_LOAD, PT_DYNAMIC, PT_GNU_EH_FRAME = 1, 2, 0x6474E550
PF_X, PF_W = 1, 2
PHDR_NAMES = {0x61000001: "SCE_PROCPARAM", 0x61000002: "SCE_MODULE_PARAM",
              0x6FFFFF00: "SCE_COMMENT", 0x6FFFFF01: "SCE_VERSION"}

DT_NULL, DT_NEEDED, DT_PLTRELSZ, DT_HASH, DT_STRTAB, DT_SYMTAB = 0, 1, 2, 4, 5, 6
DT_RELA, DT_RELASZ, DT_STRSZ, DT_INIT, DT_FINI, DT_SONAME = 7, 8, 10, 12, 13, 14
DT_JMPREL = 23
DT_SCE_EXPORT_LIB_ATTR, DT_SCE_IMPORT_LIB_ATTR = 0x61000017, 0x61000019
DT_SCE_FILENAME = 0x61000041
DT_SCE_MODULE_INFO = 0x61000043
DT_SCE_NEEDED_MODULE = 0x61000045
DT_SCE_EXPORT_LIB = 0x61000047
DT_SCE_IMPORT_LIB = 0x61000049

R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT = 1, 6, 7
STT_NAMES = {0: "NOTYPE", 1: "OBJECT", 2: "FUNC", 6: "TLS"}
STB_NAMES = {0: "LOCAL", 1: "GLOBAL", 2: "WEAK"}


def nid_of(name):
    digest = hashlib.sha1(name.encode() + NID_SUFFIX).digest()
    # The first 8 digest bytes are read as a little-endian u64 and base64'd big-endian.
    return base64.b64encode(digest[7::-1], b"+-").rstrip(b"=").decode()


def decode_id(text):
    value = 0
    for ch in text:
        value = value * 64 + B64.index(ch)
    return value


class NameDB:
    IDENT = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{3,200}")

    def __init__(self):
        self.by_nid = {}
        self.persistent = set()

    def add(self, name):
        if name not in self.persistent:
            self.by_nid.setdefault(nid_of(name), name)

    def add_blob(self, blob):
        for raw in set(self.IDENT.findall(blob)):
            name = raw.decode()
            self.add(name)
            if name.startswith("_") and not name.startswith("_Z"):
                self.add(name.lstrip("_"))
            elif not name.startswith("_"):
                self.add("_" + name)
            if name[0].isupper():  # Kyty drops the prefix: Hmd2Initialize
                self.add("sce" + name)
            # RTTI type strings ("N3sce4Hmd26DeviceE") name vtables/typeinfo.
            if re.match(r"N?\d", name):
                for pre in ("_ZTV", "_ZTI", "_ZTS"):
                    self.add(pre + name)

    def add_path(self, path):
        files = path.rglob("*") if path.is_dir() else [path]
        for f in files:
            if f.is_file():
                self.add_blob(f.read_bytes())

    def load_persistent(self):
        if NAMES_DB.exists():
            for line in NAMES_DB.read_text().split():
                self.by_nid[nid_of(line)] = line
                self.persistent.add(line)

    def save_persistent(self, names):
        merged = sorted(self.persistent | set(names))
        NAMES_DB.write_text("\n".join(merged) + "\n")
        return len(merged) - len(self.persistent)

    def brute(self, nids, prefixes, words, depth):
        """Try prefix + up to `depth` CamelCase words for the given NIDs."""
        # Compare raw digest bytes and reuse the hashed prefix state: ~3x faster.
        wanted = {base64.b64decode(n + "=", b"+-")[::-1]: n for n in nids}
        encoded = [w.encode() for w in words]
        found = {}
        for prefix in prefixes:
            base = hashlib.sha1(prefix.encode())
            for n in range(1, depth + 1):
                for combo in itertools.product(encoded, repeat=n):
                    h = base.copy()
                    h.update(b"".join(combo) + NID_SUFFIX)
                    nid = wanted.pop(h.digest()[:8], None)
                    if nid:
                        name = prefix + b"".join(combo).decode()
                        found[nid] = name
                        self.by_nid[nid] = name
                        if not wanted:
                            return found
        return found


class Prx:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise ValueError(f"{path}: not an ELF (still encrypted?)")
        self.e_type, = struct.unpack_from("<H", d, 16)
        phoff, = struct.unpack_from("<Q", d, 32)
        phentsize, phnum = struct.unpack_from("<HH", d, 54)
        self.phdrs = []
        for i in range(phnum):
            p_type, flags, off, va, _pa, filesz, memsz, align = struct.unpack_from(
                "<IIQQQQQQ", d, phoff + i * phentsize)
            self.phdrs.append(dict(type=p_type, flags=flags, offset=off, vaddr=va,
                                   filesz=filesz, memsz=memsz, align=align))
        self.loads = [p for p in self.phdrs if p["type"] == PT_LOAD]
        self._parse_dynamic()
        self._parse_symbols()
        self._parse_relocs()
        self._find_plt_stubs()
        self._parse_eh_frame_hdr()

    # -- address helpers -------------------------------------------------
    def va_to_off(self, va):
        for p in self.loads:
            if p["vaddr"] <= va < p["vaddr"] + p["filesz"]:
                return p["offset"] + va - p["vaddr"]
        return None

    def read(self, va, size):
        off = self.va_to_off(va)
        return None if off is None else self.data[off:off + size]

    def cstring(self, va, limit=256):
        off = self.va_to_off(va)
        if off is None:
            return None
        end = self.data.find(b"\0", off, off + limit)
        return None if end < 0 else self.data[off:end]

    def strtab(self, offset):
        start = self.strtab_off + offset
        return self.data[start:self.data.index(b"\0", start)].decode(errors="replace")

    # -- dynamic table ---------------------------------------------------
    def _parse_dynamic(self):
        dyn = next(p for p in self.phdrs if p["type"] == PT_DYNAMIC)
        self.dynamic = []
        for i in range(dyn["filesz"] // 16):
            tag, val = struct.unpack_from("<QQ", self.data, dyn["offset"] + i * 16)
            if tag == DT_NULL:
                break
            self.dynamic.append((tag, val))
        tags = {}
        for tag, val in self.dynamic:
            tags.setdefault(tag, val)
        self.tags = tags
        self.strtab_off = self.va_to_off(tags[DT_STRTAB])

        self.needed = [self.strtab(v) for t, v in self.dynamic if t == DT_NEEDED]
        self.soname = self.strtab(tags[DT_SONAME]) if DT_SONAME in tags else None
        self.filename = self.strtab(tags[DT_SCE_FILENAME]) if DT_SCE_FILENAME in tags else None

        def entry(v):
            return dict(id=v >> 48, name=self.strtab(v & 0xFFFFFFFF),
                        version=f"{(v >> 32) & 0xFF}.{(v >> 40) & 0xFF}")

        info = tags.get(DT_SCE_MODULE_INFO)
        self.module = entry(info) if info is not None else None
        self.needed_modules = {}
        self.export_libs = {}
        self.import_libs = {}
        for tag, val in self.dynamic:
            if tag == DT_SCE_NEEDED_MODULE:
                e = entry(val)
                self.needed_modules[e["id"]] = e
            elif tag in (DT_SCE_EXPORT_LIB, DT_SCE_IMPORT_LIB):
                e = entry(val)
                e["version"] = str((val >> 32) & 0xFFFF)
                (self.export_libs if tag == DT_SCE_EXPORT_LIB else self.import_libs)[e["id"]] = e
        for tag, val in self.dynamic:
            libs = {DT_SCE_EXPORT_LIB_ATTR: self.export_libs,
                    DT_SCE_IMPORT_LIB_ATTR: self.import_libs}.get(tag)
            if libs is not None and (val >> 48) in libs:
                libs[val >> 48]["attr"] = val & 0xFFFF

    # -- symbols and relocations -----------------------------------------
    def _parse_symbols(self):
        hash_off = self.va_to_off(self.tags[DT_HASH])
        _nbucket, nchain = struct.unpack_from("<II", self.data, hash_off)
        sym_off = self.va_to_off(self.tags[DT_SYMTAB])
        self.symbols = []
        for i in range(nchain):
            st_name, st_info, _other, shndx, value, size = struct.unpack_from(
                "<IBBHQQ", self.data, sym_off + i * 24)
            raw = self.strtab(st_name)
            sym = dict(index=i, raw=raw, nid=raw, lib=None, module=None, value=value,
                       size=size, type=STT_NAMES.get(st_info & 0xF, st_info & 0xF),
                       bind=STB_NAMES.get(st_info >> 4, st_info >> 4),
                       defined=shndx != 0, name=None)
            parts = raw.split("#")
            if len(parts) == 3 and len(parts[0]) == 11:
                sym["nid"] = parts[0]
                sym["lib"], sym["module"] = decode_id(parts[1]), decode_id(parts[2])
            elif raw:
                sym["name"] = raw  # plain (un-NID'd) symbol name
            self.symbols.append(sym)

    def _parse_relocs(self):
        self.got = {}  # GOT slot VA -> symbol index
        tables = [(DT_JMPREL, DT_PLTRELSZ), (DT_RELA, DT_RELASZ)]
        for start_tag, size_tag in tables:
            if start_tag not in self.tags:
                continue
            off = self.va_to_off(self.tags[start_tag])
            for i in range(self.tags[size_tag] // 24):
                r_offset, r_info, _addend = struct.unpack_from("<QQq", self.data, off + i * 24)
                rtype, sym = r_info & 0xFFFFFFFF, r_info >> 32
                if sym and rtype in (R_X86_64_JUMP_SLOT, R_X86_64_GLOB_DAT, R_X86_64_64):
                    self.got[r_offset] = sym

    def _find_plt_stubs(self):
        """PLT stubs are `jmp *slot(%rip)` (ff 25 rel32) into a JUMP_SLOT GOT entry."""
        self.plt = {}  # stub VA -> symbol index
        for p in self.loads:
            if not p["flags"] & PF_X:
                continue
            seg = self.data[p["offset"]:p["offset"] + p["filesz"]]
            for m in re.finditer(rb"\xff\x25", seg):
                pos = m.start()
                if pos + 6 > len(seg):
                    break
                rel, = struct.unpack_from("<i", seg, pos + 2)
                stub_va = p["vaddr"] + pos
                target = stub_va + 6 + rel
                if target in self.got:
                    self.plt[stub_va] = self.got[target]

    def _parse_eh_frame_hdr(self):
        """Function starts/sizes from the .eh_frame_hdr search table (gives sub_XXXX)."""
        self.functions = {}
        hdr = next((p for p in self.phdrs if p["type"] == PT_GNU_EH_FRAME), None)
        if hdr is None:
            return
        d, base = self.data, hdr["offset"]
        version, _ptr_enc, count_enc, table_enc = d[base:base + 4]
        if version != 1 or count_enc != 0x03 or table_enc != 0x3B:
            return  # only the usual udata4 count + datarel sdata4 table
        count, = struct.unpack_from("<I", d, base + 8)
        for i in range(count):
            loc, fde = struct.unpack_from("<ii", d, base + 12 + i * 8)
            start, fde_va = hdr["vaddr"] + loc, hdr["vaddr"] + fde
            size = 0
            fde_off = self.va_to_off(fde_va)
            if fde_off is not None:
                # FDE: length, CIE ptr, pc_begin (pcrel sdata4), pc_range (udata4).
                pc_rel, pc_range = struct.unpack_from("<iI", d, fde_off + 8)
                if fde_va + 8 + pc_rel == start:
                    size = pc_range
            self.functions[start] = size

    # -- resolution ------------------------------------------------------
    def resolve(self, names):
        for sym in self.symbols:
            if sym["name"] is None and sym["nid"] in names.by_nid:
                sym["name"] = names.by_nid[sym["nid"]]

    def exports(self):
        return [s for s in self.symbols if s["defined"] and s["raw"]]

    def imports(self):
        return [s for s in self.symbols if not s["defined"] and s["raw"]]

    def label(self, sym):
        return sym["name"] or sym["nid"]

    def to_json(self):
        got_by_sym = {v: k for k, v in self.got.items()}
        plt_by_sym = {v: k for k, v in self.plt.items()}
        exports = [dict(nid=s["nid"], name=s["name"], addr=hex(s["value"]), size=s["size"],
                        type=s["type"], bind=s["bind"],
                        lib=self.export_libs.get(s["lib"], {}).get("name"))
                   for s in self.exports()]
        imports = [dict(nid=s["nid"], name=s["name"], type=s["type"], bind=s["bind"],
                        lib=self.import_libs.get(s["lib"], {}).get("name"),
                        module=self.needed_modules.get(s["module"], {}).get("name"),
                        got=hex(got_by_sym[s["index"]]) if s["index"] in got_by_sym else None,
                        plt=hex(plt_by_sym[s["index"]]) if s["index"] in plt_by_sym else None)
                   for s in self.imports()]
        return dict(file=self.path.name, soname=self.soname, filename=self.filename,
                    module=self.module, needed=self.needed,
                    needed_modules=list(self.needed_modules.values()),
                    export_libs=list(self.export_libs.values()),
                    import_libs=list(self.import_libs.values()),
                    segments=[dict(type=PHDR_NAMES.get(p["type"], hex(p["type"])),
                                   vaddr=hex(p["vaddr"]), memsz=hex(p["memsz"]),
                                   flags="".join(f for b, f in ((4, "R"), (2, "W"), (1, "X"))
                                                 if p["flags"] & b))
                              for p in self.phdrs],
                    init=hex(self.tags.get(DT_INIT, 0)), fini=hex(self.tags.get(DT_FINI, 0)),
                    exports=exports, imports=imports)

    def report(self):
        j = self.to_json()
        mod = j["module"] or {}
        out = [f"== {j['file']}  module {mod.get('name')} v{mod.get('version')}"
               f"  soname {j['soname']}  file {j['filename']}"]
        for p in self.phdrs:
            kind = {PT_LOAD: "LOAD", PT_DYNAMIC: "DYNAMIC"}.get(p["type"]) or \
                PHDR_NAMES.get(p["type"], hex(p["type"]))
            out.append(f"   seg {kind:<16} va {p['vaddr']:#010x} mem {p['memsz']:#09x}"
                       f" file {p['filesz']:#09x} flags {p['flags']:#x}")
        out.append("needed modules: " + ", ".join(
            f"{m['name']}(id {m['id']})" for m in j["needed_modules"]))
        out.append("export libs: " + ", ".join(
            f"{l['name']}(id {l['id']} attr {l.get('attr', 0):#x})" for l in j["export_libs"]))
        out.append("import libs: " + ", ".join(
            f"{l['name']}(id {l['id']})" for l in j["import_libs"]))

        exports = sorted(j["exports"], key=lambda e: (e["lib"] or "", int(e["addr"], 16)))
        named = sum(1 for e in exports if e["name"])
        out.append(f"\nexports: {len(exports)} ({named} named)")
        for e in exports:
            out.append(f"  {e['addr']:>10} {e['size']:>6} {e['type']:<6} {e['lib'] or '?':<24}"
                       f" {e['nid']}  {e['name'] or ''}")

        imports = j["imports"]
        named = sum(1 for i in imports if i["name"])
        out.append(f"\nimports: {len(imports)} ({named} named)")
        for lib in sorted({i["lib"] or "?" for i in imports}):
            out.append(f"  [{lib}]")
            for i in sorted((i for i in imports if (i["lib"] or "?") == lib),
                            key=lambda i: i["name"] or "~" + i["nid"]):
                out.append(f"    plt {i['plt'] or '-':>10}  got {i['got'] or '-':>10}"
                           f"  {i['nid']}  {i['name'] or ''}")
        return "\n".join(out)

    # -- sectioned ELF for objdump / Ghidra ------------------------------
    def write_sym_elf(self, out_path):
        """Copy of the file with section headers + .symtab, typed as ET_DYN."""
        d = bytearray(self.data)
        d += b"\0" * (-len(d) % 16)

        shstr = bytearray(b"\0")

        def sh_name(s):
            off = len(shstr)
            shstr.extend(s.encode() + b"\0")
            return off

        sections = [dict(name=0, type=0, flags=0, addr=0, offset=0, size=0, link=0, info=0,
                         align=0, entsize=0)]
        sec_of = []  # (start, end, index) for symbol section lookup
        counts = {}
        for p in self.loads:
            if p["flags"] & PF_X:
                base, flags = ".text", 0x6
            elif p["flags"] & PF_W:
                base, flags = ".data", 0x3
            else:
                base, flags = ".rodata", 0x2
            n = counts.get(base, 0)
            counts[base] = n + 1
            name = base if n == 0 else f"{base}.{n}"
            if p["filesz"]:
                sections.append(dict(name=sh_name(name), type=1, flags=flags, addr=p["vaddr"],
                                     offset=p["offset"], size=p["filesz"], link=0, info=0,
                                     align=16, entsize=0))
                sec_of.append((p["vaddr"], p["vaddr"] + p["filesz"], len(sections) - 1))
            if p["memsz"] > p["filesz"]:
                sections.append(dict(name=sh_name(name.replace(".data", ".bss")), type=8,
                                     flags=0x3, addr=p["vaddr"] + p["filesz"], offset=0,
                                     size=p["memsz"] - p["filesz"], link=0, info=0, align=16,
                                     entsize=0))
                sec_of.append((p["vaddr"] + p["filesz"], p["vaddr"] + p["memsz"],
                               len(sections) - 1))

        def shndx(va):
            for lo, hi, idx in sec_of:
                if lo <= va < hi:
                    return idx
            return 0xFFF1  # SHN_ABS

        strtab = bytearray(b"\0")
        local_syms, global_syms = [], []

        def add_sym(bucket, name, value, size, stt, stb):
            off = len(strtab)
            strtab.extend(name.encode() + b"\0")
            bucket.append(struct.pack("<IBBHQQ", off, (stb << 4) | stt, 0, shndx(value),
                                      value, size))

        for va, idx in sorted(self.plt.items()):
            add_sym(local_syms, self.label(self.symbols[idx]) + "@plt", va, 16, 2, 0)
        for va, idx in sorted(self.got.items()):
            add_sym(local_syms, "__imp_" + self.label(self.symbols[idx]), va, 8, 1, 0)
        if self.tags.get(DT_INIT):
            add_sym(local_syms, "_init", self.tags[DT_INIT], 0, 2, 0)
        if self.tags.get(DT_FINI):
            add_sym(local_syms, "_fini", self.tags[DT_FINI], 0, 2, 0)
        exported = {s["value"] for s in self.exports()} | set(self.plt) | \
            {self.tags.get(DT_INIT), self.tags.get(DT_FINI)}
        for va, size in sorted(self.functions.items()):
            if va not in exported:
                add_sym(local_syms, f"sub_{va:x}", va, size, 2, 0)
        for s in self.exports():
            stt = {"FUNC": 2, "OBJECT": 1}.get(s["type"], 0)
            add_sym(global_syms, self.label(s), s["value"], s["size"], stt, 1)

        symtab = struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0) + b"".join(local_syms + global_syms)
        first_global = 1 + len(local_syms)

        def append_blob(blob):
            nonlocal d
            off = len(d)
            d += blob
            d += b"\0" * (-len(d) % 16)
            return off

        strtab_idx = len(sections) + 1
        sections.append(dict(name=sh_name(".symtab"), type=2, flags=0, addr=0,
                             offset=append_blob(symtab), size=len(symtab), link=strtab_idx,
                             info=first_global, align=8, entsize=24))
        sections.append(dict(name=sh_name(".strtab"), type=3, flags=0, addr=0,
                             offset=append_blob(bytes(strtab)), size=len(strtab), link=0,
                             info=0, align=1, entsize=0))
        shstrndx = len(sections)
        name_off = sh_name(".shstrtab")
        sections.append(dict(name=name_off, type=3, flags=0, addr=0,
                             offset=append_blob(bytes(shstr)), size=len(shstr), link=0,
                             info=0, align=1, entsize=0))
        shoff = len(d)
        for s in sections:
            d += struct.pack("<IIQQQQIIQQ", s["name"], s["type"], s["flags"], s["addr"],
                             s["offset"], s["size"], s["link"], s["info"], s["align"],
                             s["entsize"])
        struct.pack_into("<H", d, 16, 3)  # ET_DYN
        struct.pack_into("<Q", d, 40, shoff)
        struct.pack_into("<HHH", d, 58, 64, len(sections), shstrndx)
        Path(out_path).write_bytes(d)


def find_objdump():
    for cand in ("llvm-objdump", "/opt/homebrew/opt/llvm/bin/llvm-objdump",
                 "/usr/local/opt/llvm/bin/llvm-objdump"):
        path = shutil.which(cand)
        if path:
            return path
    return None


def annotate(prx, text):
    """Append the C string a rip-relative operand points at, when it points at one."""
    lines = []
    for line in text.splitlines():
        m = re.search(r"# 0x([0-9a-f]+)$", line)
        if m:
            s = prx.cstring(int(m.group(1), 16))
            if s and len(s) >= 3 and all(32 <= c < 127 or c in (9, 10) for c in s):
                line += '  "' + s.decode()[:100].replace("\n", "\\n") + '"'
        lines.append(line)
    return "\n".join(lines)


def write_asm(prx, elf_path, out_path):
    objdump = find_objdump()
    if not objdump:
        sys.exit("llvm-objdump not found (brew install llvm)")
    text = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(elf_path)],
                          capture_output=True, text=True, check=True).stdout
    Path(out_path).write_text(annotate(prx, text) + "\n")


def disasm(prx, elf_path, wanted):
    objdump = find_objdump()
    if not objdump:
        sys.exit("llvm-objdump not found (brew install llvm)")
    by_label = {}
    for s in prx.exports():
        by_label[prx.label(s)] = s
        by_label[s["nid"]] = s
    out = []
    for name in wanted:
        sym = by_label.get(name)
        m = re.fullmatch(r"(?:sub_|0x)([0-9a-fA-F]+)", name)
        if sym is None and m and int(m.group(1), 16) in prx.functions:
            va = int(m.group(1), 16)
            sym = dict(value=va, size=prx.functions[va], nid="-", name=f"sub_{va:x}")
        if sym is None:
            out.append(f"# {name}: not an export or eh_frame function of {prx.path.name}")
            continue
        start, stop = sym["value"], sym["value"] + max(sym["size"], 1)
        text = subprocess.run(
            [objdump, "-d", "--no-show-raw-insn", f"--start-address={start:#x}",
             f"--stop-address={stop:#x}", str(elf_path)],
            capture_output=True, text=True, check=True).stdout
        body = "\n".join(l for l in annotate(prx, text).splitlines()
                         if l.strip() and not l.startswith(str(elf_path)))
        out.append(f"\n### {prx.label(sym)} ({sym['nid']}) @ {start:#x} size {sym['size']}\n"
                   + body)
    return "\n".join(out)


def camel_words(names, prefixes, limit):
    """Words from names under the prefixes first, then the most common sce* words."""
    def count(pool, strip):
        words = {}
        for n in pool:
            for w in re.findall(r"[A-Z][a-z0-9]*|[0-9]+", n[strip(n):]):
                words[w] = words.get(w, 0) + 1
        return [w for w, _ in sorted(words.items(), key=lambda kv: -kv[1])]

    own = [n for n in names if any(n.startswith(p) for p in prefixes)]
    first = count(own, lambda n: max(len(p) for p in prefixes if n.startswith(p)))
    rest = count([n for n in names if n.startswith("sce") and n.isidentifier()], lambda n: 3)
    out = list(dict.fromkeys(first + rest))
    return out[:max(limit, len(first))]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("libs", nargs="+", type=Path, help="decrypted .sprx/.prx files or dirs")
    ap.add_argument("--names", action="append", type=Path, default=[],
                    help="extra file/dir to harvest candidate names from (repeatable)")
    ap.add_argument("--out", type=Path, help="write <lib>.txt and <lib>.json reports here")
    ap.add_argument("--elf-dir", type=Path, help="write <lib>.sym.elf with sections+symbols")
    ap.add_argument("--asm", action="store_true",
                    help="with --elf-dir, also write a full annotated <lib>.asm listing")
    ap.add_argument("--disasm", help="comma-separated exports (name or NID) to disassemble;"
                                     " needs exactly one library")
    ap.add_argument("--brute", metavar="PREFIXES",
                    help="comma-separated name prefixes (e.g. sceHmd2,sceVrTracker2) to"
                         " brute-force unresolved exports with CamelCase words")
    ap.add_argument("--words", type=Path,
                    help="extra brute-force words (whitespace separated), tried first")
    ap.add_argument("--brute-depth", type=int, default=2)
    ap.add_argument("--brute-words", type=int, default=120)
    ap.add_argument("--quiet", action="store_true", help="summary only on stdout")
    args = ap.parse_args()

    assert nid_of("sceHmd2Initialize") == "c812oYs7Vsc", "NID hash self-test failed"

    files = []
    for p in args.libs:
        files += sorted(f for f in p.iterdir() if f.suffix in (".sprx", ".prx", ".elf")) \
            if p.is_dir() else [p]
    prxs = [Prx(f) for f in files]

    names = NameDB()
    for f in files:
        names.add_blob(f.read_bytes())
    for extra in args.names:
        names.add_path(extra)
    names.load_persistent()
    for prx in prxs:
        prx.resolve(names)

    if args.brute:
        prefixes = args.brute.split(",")
        words = camel_words(names.by_nid.values(), prefixes, args.brute_words)
        if args.words:
            words = list(dict.fromkeys(args.words.read_text().split() + words))
        missing = {s["nid"] for prx in prxs for s in prx.symbols
                   if s["raw"] and not s["name"]}
        found = names.brute(missing, prefixes, words, args.brute_depth)
        print(f"brute force: {len(words)} words, depth {args.brute_depth},"
              f" {len(found)} new names", file=sys.stderr)
        for prx in prxs:
            prx.resolve(names)

    resolved = {s["name"] for prx in prxs for s in prx.symbols
                if s["name"] and "#" in s["raw"]}
    added = names.save_persistent(resolved)

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
    if args.elf_dir:
        args.elf_dir.mkdir(parents=True, exist_ok=True)

    summary = []
    for prx in prxs:
        rep = prx.report()
        if args.out:
            stem = prx.path.name.rsplit(".", 1)[0]
            (args.out / f"{stem}.txt").write_text(rep + "\n")
            (args.out / f"{stem}.json").write_text(json.dumps(prx.to_json(), indent=1) + "\n")
        if args.elf_dir:
            elf_path = args.elf_dir / (prx.path.name.rsplit(".", 1)[0] + ".sym.elf")
            prx.write_sym_elf(elf_path)
            if args.asm:
                write_asm(prx, elf_path, (args.out or args.elf_dir) /
                          (prx.path.name.rsplit(".", 1)[0] + ".asm"))
        if not args.quiet and not args.disasm:
            print(rep + "\n")
        ex, im = prx.exports(), prx.imports()
        summary.append(f"{prx.path.name:<28} exports {sum(1 for s in ex if s['name']):>4}/"
                       f"{len(ex):<4} imports {sum(1 for s in im if s['name']):>4}/{len(im):<4}"
                       f" plt stubs {len(prx.plt)} functions {len(prx.functions)}")

    if args.disasm:
        if len(prxs) != 1:
            sys.exit("--disasm needs exactly one library")
        elf_dir = args.elf_dir or Path("/tmp")
        elf_path = elf_dir / (prxs[0].path.name.rsplit(".", 1)[0] + ".sym.elf")
        if not args.elf_dir:
            prxs[0].write_sym_elf(elf_path)
        print(disasm(prxs[0], elf_path, args.disasm.split(",")))

    print("\n".join(["named / total:"] + summary), file=sys.stderr)
    print(f"{len(names.by_nid)} candidate names; names.txt +{added}", file=sys.stderr)


if __name__ == "__main__":
    main()
