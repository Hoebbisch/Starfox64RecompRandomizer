#!/usr/bin/env python3
"""
Build-Skript fuer SF64-Recomp-Mods — ersetzt das Makefile (kein 'make' noetig).
Repliziert exakt die Compiler-/Linker-Flags aus dem Template-Makefile.

Aufruf:
    python build.py <mod_dir> [--tool]

  <mod_dir>  Ordner des Mods (enthaelt src/, include/, mod.ld, mod.toml, sf64/, Starfox64RecompSyms/)
  --tool     nach dem Linken zusaetzlich RecompModTool laufen lassen -> .nrm erzeugen

LLVM (clang + ld.lld) wird automatisch gesucht unter _dev/tools/llvm*/bin,
oder per Umgebungsvariable LLVM_BIN ueberschrieben.
"""
import os, sys, glob, subprocess, shutil

DEV_DIR = os.path.dirname(os.path.abspath(__file__))

ARCHFLAGS = ("-target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg "
             "-mno-check-zero-division -fomit-frame-pointer -ffast-math "
             "-fno-unsafe-math-optimizations -fno-builtin-memset").split()
WARNFLAGS = ("-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter "
             "-Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces "
             "-Wno-unsupported-floating-point-opt -Werror=section").split()
CFLAGS  = ARCHFLAGS + WARNFLAGS + ["-D_LANGUAGE_C", "-nostdinc", "-ffunction-sections"]
CPPDEFS = ["-nostdinc", "-D_LANGUAGE_C", "-DMIPS", "-DGBI_DOWHILE", "-DF3DEX_GBI", "-DTARGET_N64"]
INCLUDE_SUBDIRS = ["include", "sf64/include", "sf64/src", "sf64/include/libc", "sf64/include/libultra"]
LDFLAGS_BASE = ["-nostdlib", "--unresolved-symbols=ignore-all", "--emit-relocs", "-e", "0",
                "--no-nmagic", "-gc-sections"]


def find_llvm_bin():
    env = os.environ.get("LLVM_BIN")
    if env and os.path.isfile(os.path.join(env, "clang.exe")):
        return env
    candidates = glob.glob(os.path.join(DEV_DIR, "tools", "llvm*", "bin")) \
               + glob.glob(os.path.join(DEV_DIR, "tools", "clang*", "bin"))
    for c in candidates:
        if os.path.isfile(os.path.join(c, "clang.exe")):
            return c
    sys.exit("FEHLER: clang.exe nicht gefunden. LLVM unter _dev/tools/llvm*/bin erwartet "
             "oder LLVM_BIN setzen.")


def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit(f"FEHLER (exit {r.returncode}): {' '.join(cmd)}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    mod_dir = os.path.abspath(sys.argv[1])
    do_tool = "--tool" in sys.argv[2:]
    if not os.path.isdir(mod_dir):
        sys.exit(f"FEHLER: Mod-Ordner nicht gefunden: {mod_dir}")

    llvm_bin = find_llvm_bin()
    clang = os.path.join(llvm_bin, "clang.exe")
    lld   = os.path.join(llvm_bin, "ld.lld.exe")
    print(f"[i] LLVM: {llvm_bin}")

    build_dir = os.path.join(mod_dir, "build")
    os.makedirs(os.path.join(build_dir, "src"), exist_ok=True)

    includes = []
    for inc in INCLUDE_SUBDIRS:
        includes += ["-I", os.path.join(mod_dir, inc)]

    srcs = glob.glob(os.path.join(mod_dir, "src", "**", "*.c"), recursive=True)
    if not srcs:
        sys.exit("FEHLER: keine .c-Dateien unter src/ gefunden.")

    objs = []
    for src in srcs:
        rel = os.path.relpath(src, mod_dir)
        obj = os.path.join(build_dir, os.path.splitext(rel)[0] + ".o")
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        print(f"[CC] {rel}")
        run([clang] + CFLAGS + CPPDEFS + includes + [src, "-c", "-o", obj])
        objs.append(obj)

    elf = os.path.join(build_dir, "mod.elf")
    mapf = os.path.join(build_dir, "mod.map")
    ldscript = os.path.join(mod_dir, "mod.ld")
    print("[LD] mod.elf")
    run([lld] + objs + ["-T", ldscript, "-Map", mapf] + LDFLAGS_BASE + ["-o", elf])
    print(f"[OK] {elf}")

    if do_tool:
        tool = os.path.join(DEV_DIR, "tools", "RecompModTool.exe")
        if not os.path.isfile(tool):
            sys.exit(f"FEHLER: RecompModTool.exe nicht gefunden: {tool}")
        print("[TOOL] RecompModTool mod.toml build")
        # RecompModTool erwartet als CWD den Mod-Ordner (Pfade in mod.toml sind relativ)
        r = subprocess.run([tool, "mod.toml", "build"], cwd=mod_dir)
        if r.returncode != 0:
            sys.exit(f"FEHLER: RecompModTool exit {r.returncode}")
        nrms = glob.glob(os.path.join(build_dir, "*.nrm"))
        print("[OK] erzeugt: " + ", ".join(os.path.basename(n) for n in nrms) if nrms
              else "[?] kein .nrm im build-Ordner gefunden")


if __name__ == "__main__":
    main()
