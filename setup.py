#!/usr/bin/env python3
"""
Generates the C translation of the arcade program from YOUR copy of the ROM (shangon.zip).

The translated code (source/src/gen/ and source/recomp/overrides/) is deliberately not kept in the repository:
run this once after cloning, and again whenever you change recomp/ or the patches.

    python3 setup.py                       finds shangon.zip in this folder or the one above
    python3 setup.py --rom path/to/shangon.zip
    python3 setup.py --make-patches        (developers) turn the hand-edited recomp/overrides/*.c into recomp/patches/*.patch
    python3 setup.py --skip-check          skip the final 1200-frame behaviour check
    python3 setup.py --new-coverage        record the coverage again instead of using recomp/coverage.bin (slow)

Needs: Python 3 and a C compiler (clang or gcc; on Windows the clang from llvm-mingw). Nothing else.
"""
import argparse, difflib, os, re, shutil, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "source")
RECOMP = os.path.join(SRC, "recomp")
WORK = os.path.join(SRC, "build", "setup")
EXE = ".exe" if sys.platform == "win32" else ""
MAC = sys.platform == "darwin"
M = "src/third_party/musashi"
ISYSROOT = []          # on macOS: ["-isysroot", <SDK path>], filled in by main()

# the ROM files build_images.py reads (MAME set "shangon")
ROM_FILES = """epr-10886.133 epr-10884.118 epr-10887.132 epr-10885.117 epr-10792.76 epr-10790.58 epr-10793.75 epr-10791.57
epr-10652.54 epr-10651.55 epr-10650.56 mpr-10794.8 mpr-10798.16 mpr-10795.6 mpr-10799.14 mpr-10796.4 mpr-10800.12
mpr-10797.2 mpr-10801.10 epr-10642.47 epr-10649c.88 epr-10643.66 epr-10644.67 epr-10645.68 epr-10646.69""".split()

# scripted play for the coverage pass: (cov_main mode, frames). Any code these miss still works - it falls back
# to the interpreter automatically - it is just not translated.
COVERAGE_RUNS = [("free auto", 60000), ("free auto cont", 30000), ("free brake auto", 30000),
                 ("auto", 20000), ("free", 20000), ("test", 8000)]


def die(msg):
    print("\nERROR: " + msg)
    sys.exit(1)


def run(cmd, cwd=SRC, env=None, stdout=None, quiet=False):
    if not quiet:
        print("  $ " + " ".join(cmd if len(cmd) < 8 else cmd[:6] + ["..."]))
    r = subprocess.run(cmd, cwd=cwd, env=env, stdout=stdout)
    if r.returncode:
        die("command failed (%d): %s" % (r.returncode, " ".join(cmd[:6])))


def find_cc(explicit):
    cands = [explicit] if explicit else []
    cands += [os.environ.get("CC"), "cc", "clang", "gcc", "x86_64-w64-mingw32-clang"]
    lm = os.environ.get("LLVM_MINGW")
    if lm:
        cands.append(os.path.join(lm, "bin", "x86_64-w64-mingw32-clang"))
    for c in cands:
        if c and shutil.which(c):
            return shutil.which(c)
    die("no C compiler found. Install clang or gcc (Mac: xcode-select --install; Windows: llvm-mingw, then set LLVM_MINGW "
        "or pass --cc).")


def find_rom(explicit):
    for p in ([explicit] if explicit else [os.path.join(ROOT, "shangon.zip"), os.path.join(ROOT, "..", "shangon.zip")]):
        if p and os.path.isfile(p):
            return os.path.abspath(p)
    die("shangon.zip not found. Put it next to this script, or run: python3 setup.py --rom path/to/shangon.zip")


def unzip_rom(rom, out):
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    with zipfile.ZipFile(rom) as z:
        names = {os.path.basename(n): n for n in z.namelist()}
        missing = [f for f in ROM_FILES if f not in names]
        if missing:
            die("this does not look like the MAME 'shangon' ROM set; missing: " + ", ".join(missing[:5]))
        for f in ROM_FILES:
            with open(os.path.join(out, f), "wb") as o:
                o.write(z.read(names[f]))


# ---------------------------------------------------------------------------------------------- tiny patch tools
def make_patch(orig_text, new_text, name):
    return "".join(difflib.unified_diff(orig_text.splitlines(True), new_text.splitlines(True), "a/" + name, "b/" + name, n=3))


def apply_patch(text, patch, name):
    """Applies a unified diff (as written by make_patch) by exact line matching."""
    lines = text.splitlines(True)
    out, pos = [], 0
    hunks = re.split(r"^@@ .*@@.*\n", patch, flags=re.M)[1:]
    heads = re.findall(r"^@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@", patch, flags=re.M)
    for head, body in zip(heads, hunks):
        start = int(head) - 1
        if start < pos:
            die("patch %s: overlapping hunks" % name)
        out += lines[pos:start]
        pos = start
        for l in body.splitlines(True):
            tag, rest = l[:1], l[1:]
            if tag == " ":
                if pos >= len(lines) or lines[pos] != rest:
                    die("patch %s does not fit the generated code (line %d). The ROM or recompiler differs from the one the "
                        "patch was made for." % (name, pos + 1))
                out.append(lines[pos]); pos += 1
            elif tag == "-":
                if pos >= len(lines) or lines[pos] != rest:
                    die("patch %s does not fit the generated code (line %d)." % (name, pos + 1))
                pos += 1
            elif tag == "+":
                out.append(rest)
    out += lines[pos:]
    return "".join(out)


# ---------------------------------------------------------------------------------------------- pipeline
def main():
    global ISYSROOT
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom"); ap.add_argument("--cc"); ap.add_argument("--cov", help="use this coverage file instead of recomp/coverage.bin")
    ap.add_argument("--new-coverage", action="store_true", help="ignore recomp/coverage.bin and record coverage again (about 15 minutes)")
    ap.add_argument("--make-patches", action="store_true"); ap.add_argument("--skip-check", action="store_true")
    a = ap.parse_args()

    if MAC and not os.environ.get("SDKROOT"):
        # clang called directly (not through /usr/bin/clang) does not know where Apple's system headers are
        try:
            sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], universal_newlines=True).strip()
        except Exception:
            sdk = ""
        if not sdk or not os.path.isfile(os.path.join(sdk, "usr", "include", "stdio.h")):
            die("the macOS SDK was not found. Install Apple's Command Line Tools with:  xcode-select --install")
        os.environ["SDKROOT"] = sdk
        print("macOS SDK: " + sdk)
    if MAC and os.environ.get("SDKROOT"):
        ISYSROOT = ["-isysroot", os.environ["SDKROOT"]]
    rom, cc = find_rom(a.rom), find_cc(a.cc)
    print("ROM: %s\nCompiler: %s\n" % (rom, cc))
    os.makedirs(WORK, exist_ok=True)
    py = sys.executable
    rel = lambda p: os.path.relpath(p, SRC)

    print("[1/6] Unpacking the ROM and building the program images")
    romdir, img = os.path.join(WORK, "rom"), os.path.join(WORK, "img")
    unzip_rom(rom, romdir)
    run([py, "recomp/build_images.py", romdir, img])
    for f in ("main.bin", "sub.bin"):
        if not os.path.isfile(os.path.join(img, f)):
            die(f + " was not produced")

    print("[2/6] Disassembling")
    dasm = os.path.join(WORK, "dasm_all" + EXE)
    run([cc] + ISYSROOT + ["-O2", "-w", "-I" + M, "-o", rel(dasm), "recomp/dasm_all.c", M + "/m68kcpu.c", M + "/m68kops.c", M + "/m68kdasm.c",
         M + "/softfloat/softfloat.c", "-lm"])
    for cpu in ("main", "sub"):
        with open(os.path.join(WORK, cpu + ".dasm"), "w") as o:
            run([dasm, os.path.join(img, cpu + ".bin"), "40000"], stdout=o, quiet=True)

    print("[3/6] Coverage (which code the game really runs)")
    cov = os.path.join(WORK, "cov.bin")
    shipped = os.path.join(RECOMP, "coverage.bin")
    if a.cov or (os.path.isfile(shipped) and not a.new_coverage):
        print("  using %s" % (a.cov or "recomp/coverage.bin"))
        shutil.copyfile(a.cov or shipped, cov)
    else:
        if os.path.exists(cov):
            os.remove(cov)
        covexe = os.path.join(WORK, "cov_main" + EXE)
        eng = ["src/sho_core.c", "src/sho_video.c", "src/sho_music.c", "src/sho_md.c", "src/sho_mdgfx.c", "src/sho_mdcourse.c", "src/sho_mdsnd.c",
               M + "/m68kcpu.c", M + "/m68kops.c", M + "/m68kdasm.c", M + "/softfloat/softfloat.c", "src/third_party/z80/z80.c",
               "src/third_party/nukedopn2/ym3438.c", "src/third_party/nukedopm/opm.c", "src/third_party/puff/puff.c"]
        run([cc] + ISYSROOT + ["-O2", "-w", "-Isrc", "-I" + M, "-DM68K_INSTRUCTION_HOOK=2", "-DM68K_INSTRUCTION_CALLBACK(pc)=cov_hook(pc)",
             "-o", rel(covexe), "recomp/cov_main.c", "recomp/cov_stub.c"] + eng + ["-lm"])
        for mode, frames in COVERAGE_RUNS:
            print("  coverage run: %-16s %d frames" % (mode, frames))
            run([covexe, rom, mode, str(frames), cov], quiet=True)

    print("[4/6] Translating the 68000 code to C")
    gen = os.path.join(SRC, "src", "gen"); os.makedirs(gen, exist_ok=True)
    ovr = os.path.join(RECOMP, "overrides"); os.makedirs(ovr, exist_ok=True)
    pdir = os.path.join(RECOMP, "patches")

    def recomp(cpu, out, ovrdir):
        cmd = [py, "recomp/recomp.py", cpu, os.path.join(img, cpu + ".bin"), os.path.join(WORK, cpu + ".dasm"), cov, out]
        run(cmd + ([ovrdir] if ovrdir else []))

    plain = {c: os.path.join(WORK, c + "_plain.c") for c in ("main", "sub")}
    if a.make_patches or os.path.isdir(pdir):
        for c in plain:      # untouched translation: the starting point for the hand-written overrides
            recomp(c, plain[c], None)
    cpu_of = lambda name: "main" if name.startswith("M_") else "sub"

    def original(name):
        """the generated function, wrapped exactly as extract_override.py writes it"""
        tmp = os.path.join(WORK, "extract"); shutil.rmtree(tmp, ignore_errors=True); os.makedirs(os.path.join(tmp, "overrides"))
        shutil.copy(os.path.join(RECOMP, "extract_override.py"), tmp)
        run([py, os.path.join(tmp, "extract_override.py"), plain[cpu_of(name)], name], quiet=True)
        return open(os.path.join(tmp, "overrides", name + ".c")).read()

    if a.make_patches:
        os.makedirs(pdir, exist_ok=True)
        for f in sorted(os.listdir(ovr)):
            if f.endswith(".c"):
                name = f[:-2]
                p = make_patch(original(name), open(os.path.join(ovr, f)).read(), name + ".c")
                open(os.path.join(pdir, name + ".patch"), "w", newline="\n").write(p)
                print("  wrote recomp/patches/%s.patch (%d lines)" % (name, p.count("\n")))
        print("\nDone. The patches contain only your edits (plus a few lines of context)."); return

    if os.path.isdir(pdir):
        for f in sorted(os.listdir(pdir)):
            if f.endswith(".patch"):
                name = f[:-6]; dst = os.path.join(ovr, name + ".c")
                if not os.path.exists(dst):
                    open(dst, "w", newline="\n").write(apply_patch(original(name), open(os.path.join(pdir, f)).read(), f))
                    print("  built override %s from its patch" % name)
    for c, out in (("main", "src/gen/main_rc.c"), ("sub", "src/gen/sub_rc.c")):
        recomp(c, out, "recomp/overrides")

    if a.skip_check:
        print("\nGenerated src/gen/. (Behaviour check skipped.)"); return

    print("[5/6] Building the test runner")
    shotest = os.path.join(WORK, "shotest" + EXE)
    with open(os.path.join(WORK, "overrides_all.c"), "w", newline="\n") as o:
        for f in sorted(os.listdir(ovr)):
            if f.endswith(".c"):
                o.write(open(os.path.join(ovr, f)).read())
    srcs = ["src/test_main.c", "src/sho_core.c", "src/sho_video.c", "src/sho_music.c", "src/sho_md.c", "src/sho_mdgfx.c", "src/sho_mdcourse.c",
            "src/sho_mdsnd.c", "src/rc_runtime.c", "src/gen/main_rc.c", "src/gen/sub_rc.c", rel(os.path.join(WORK, "overrides_all.c")),
            M + "/m68kcpu.c", M + "/m68kops.c", M + "/m68kdasm.c", M + "/softfloat/softfloat.c", "src/third_party/z80/z80.c",
            "src/third_party/nukedopn2/ym3438.c", "src/third_party/nukedopm/opm.c", "src/third_party/puff/puff.c"]
    flags = ["-O2", "-w", "-fno-strict-aliasing", "-Isrc"]
    if MAC:
        srcs.append("src/rc_ctx.c"); flags.append("-ffp-contract=off")
    run([cc] + ISYSROOT + flags + ["-o", rel(shotest)] + srcs + ["-lm"])

    print("[6/6] Checking behaviour: 1200 frames must match the reference exactly")
    ref = os.path.join(SRC, "tests", "selftest_1200.txt")
    rundir = os.path.join(WORK, "selftest"); shutil.rmtree(rundir, ignore_errors=True); os.makedirs(rundir)
    env = dict(os.environ, RECOMP="1", HASHLOG="hashes.txt")
    subprocess.run([shotest, rom, "1200", "auto"], cwd=rundir, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    got = os.path.join(rundir, "hashes.txt")
    norm = lambda path: open(path, "rb").read().replace(b"\r\n", b"\n")      # Windows writes/checks out CRLF; the hashes are the same
    if os.path.isfile(got) and os.path.isfile(ref) and norm(got) == norm(ref):
        print("\nSUCCESS: the generated code behaves exactly like the reference build (1200 frames identical).")
    else:
        print("\nWARNING: the 1200-frame check did NOT match tests/selftest_1200.txt. Look at source/build/setup/selftest/.")
        sys.exit(2)


if __name__ == "__main__":
    main()
