#!/usr/bin/env python3
import re
import subprocess
import sys

def write_c(out_path, funcs, files, entries):
    with open(out_path, "w") as f:
        f.write('#include "types.h"\n#include "riscv.h"\n#include "defs.h"\n\n')
        f.write("const char * const ksym_funcs[] = {\n")
        for name in funcs:
            f.write(f'  "{name}",\n')
        f.write("};\n\n")

        f.write("const char * const ksym_files[] = {\n")
        for path in files:
            f.write(f'  "{path}",\n')
        f.write("};\n\n")

        f.write("const struct ksym ksyms[] = {\n")
        for addr, fn_i, fl_i, line in entries:
            f.write(f"  {{ 0x{addr:x}ULL, {fn_i}, {fl_i}, {line} }},\n")
        f.write("};\n\n")
        f.write(f"const int ksyms_count = {len(entries)};\n")

def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--empty":
        write_c(sys.argv[2], ["<unknown>"], ["<unknown>"], [(0, 0, 0, 0)])
        return

    objdump = sys.argv[1]
    kernel_elf = sys.argv[2]
    out_path = sys.argv[3]

    out = subprocess.check_output([objdump, "-d", "-l", kernel_elf], text=True)

    funcs = ["<unknown>"]
    func_map = {"<unknown>": 0}
    files = ["<unknown>"]
    file_map = {"<unknown>": 0}

    cur_fn = 0
    cur_fl = 0
    cur_line = 0
    last_state = None
    entries = []

    re_func = re.compile(r"^[0-9a-f]+\s+<([^>]+)>:")
    re_loc = re.compile(r"^([^:\s][^:]*):(\d+)(?:\s+\(discriminator\s+\d+\))?\s*$")
    re_insn = re.compile(r"^\s+([0-9a-f]+):\s+")

    for raw in out.splitlines():
        m = re_func.match(raw)
        if m:
            name = m.group(1)
            if name not in func_map:
                func_map[name] = len(funcs)
                funcs.append(name)
            cur_fn = func_map[name]
            continue

        m = re_loc.match(raw)
        if m:
            path = m.group(1)
            if path.startswith("./"):
                path = path[2:]
            if path not in file_map:
                file_map[path] = len(files)
                files.append(path)
            cur_fl = file_map[path]
            cur_line = int(m.group(2))
            continue

        m = re_insn.match(raw)
        if m:
            addr = int(m.group(1), 16)
            state = (cur_fn, cur_fl, cur_line)
            if state != last_state:
                entries.append((addr, cur_fn, cur_fl, cur_line))
                last_state = state

    entries.sort(key=lambda x: x[0])
    write_c(out_path, funcs, files, entries)

if __name__ == "__main__":
    main()