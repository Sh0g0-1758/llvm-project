#!/usr/bin/env python3
"""Regression tests of `invisible preprocess`.

Every test is a translation unit in cases/ (or a directory of them in link/), run in each of the
two modes, the default one and monomorphization (INVISIBLE_MONO=1). A test passes when:

  1. invisible preprocesses it,
  2. the output compiles as a compiler builds it (-w -Wno-out-of-line-declaration),
  3. the output leaves nothing undefined that the original defines, so that it links where
     the original does,
  4. a program, one with `main` that links, prints and returns what the original does,
  5. and the output matches the test's CHECK lines.

A test states what differs from the defaults in comment lines anywhere in it:

  // STD: c++17                 the language standard (default: c++23, or c17 for a .c file)
  // MODES: mono                the modes it runs in (default: normal mono; a .c file: normal)
  // XFAIL: mono                a known failure, reported but not counted
  // CHECK: <regex>             the output of every mode matches it
  // CHECK-NOT: <regex>         the output of no mode matches it
  // CHECK-MONO: <regex>        and the same for the output of one mode
  // CHECK-NOT-NORMAL: <regex>

usage: run.py [-j N] [--invisible PATH] [--keep DIR] [NAME_FILTER ...]
"""
import argparse
import concurrent.futures
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
CXX = os.environ.get("CXX", "/usr/lib/llvm-22/bin/clang++")
INVISIBLE = HERE.parents[3] / "build" / "bin" / "invisible"
MODES = ("normal", "mono")
COMPILE_FLAGS = ["-w", "-Wno-out-of-line-declaration"]
RUN_TIMEOUT = 20


class Failure(Exception):
    pass


class Test:
    """A test: one translation unit, or the translation units of a program linked together."""

    def __init__(self, path: pathlib.Path):
        self.path = path
        self.name = str(path.relative_to(HERE))
        self.units = sorted(p for p in path.iterdir() if p.suffix in (".c", ".cc")) if path.is_dir() else [path]
        self.is_c = all(u.suffix == ".c" for u in self.units)
        text = "\n".join(u.read_text() for u in self.units)
        directive = lambda key: [m.group(1).strip() for m in re.finditer(rf"^\s*//\s*{key}:(.*)$", text, re.M)]
        self.std = (directive("STD") or ["c17" if self.is_c else "c++23"])[0]
        self.modes = (directive("MODES") or ["normal" if self.is_c else "normal mono"])[0].split()
        self.xfail = set(" ".join(directive("XFAIL")).split())
        self.checks = {mode: [(True, r) for r in directive("CHECK") + directive(f"CHECK-{mode.upper()}")] +
                             [(False, r) for r in directive("CHECK-NOT") + directive(f"CHECK-NOT-{mode.upper()}")]
                       for mode in MODES}
        self.is_program = re.search(r"\bint\s+main\s*\(", text) is not None

    def language(self) -> list[str]:
        return ["-x", "c" if self.is_c else "c++", f"-std={self.std}"]


def run(cmd: list, **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace", **kwargs)


def first_error(log: str) -> str:
    lines = log.splitlines()
    for marker in ("Assertion", "undefined reference", "multiple definition", "error:"):
        if found := next((line for line in lines if marker in line), None):
            return found
    return log.strip()[-300:]


def compile_all(test: Test, sources: list[pathlib.Path]) -> list[pathlib.Path]:
    """Compiles each source as a compiler builds it."""
    objects = []
    for source in sources:
        obj = source.with_suffix(".o")
        compiled = run([CXX, *test.language(), *COMPILE_FLAGS, "-c", source, "-o", obj])
        if compiled.returncode:
            raise Failure(f"{source.name} does not compile: {first_error(compiled.stderr)}")
        objects.append(obj)
    return objects


def link(objects: list[pathlib.Path], exe: pathlib.Path) -> None:
    linked = run([CXX, *objects, "-o", exe])
    if linked.returncode:
        raise Failure(f"does not link: {first_error(linked.stderr)}")


def symbols(objects: list[pathlib.Path], flag: str) -> set[str]:
    return {line.split()[-1] for o in objects for line in run(["nm", flag, o]).stdout.splitlines() if line.strip()}


def lost_definitions(outputs: list[pathlib.Path], originals: list[pathlib.Path]) -> list[str]:
    """What the objects of the outputs leave undefined that those of the originals define."""
    missing = symbols(outputs, "-u") - symbols(outputs, "--defined-only")
    return sorted(missing & symbols(originals, "--defined-only"))


def execute(exe: pathlib.Path) -> tuple[str, int]:
    try:
        ran = run([exe], timeout=RUN_TIMEOUT)
    except subprocess.TimeoutExpired:
        return "<timeout>", -1
    return ran.stdout, ran.returncode


def run_mode(test: Test, mode: str, work: pathlib.Path, originals: list[pathlib.Path], expected: tuple[str, int] | None) -> None:
    outputs = []
    env = dict(os.environ, INVISIBLE_MONO="1" if mode == "mono" else "")
    for unit in test.units:
        source = work / unit.name
        out = work / f"{unit.stem}.{mode}{unit.suffix}"
        pre = run([INVISIBLE, "preprocess", source, *test.language(), "-q", "-o", out], cwd=work, env=env)
        if pre.returncode:
            raise Failure(f"preprocessing {unit.name} failed: {first_error(pre.stderr)}")
        outputs.append(out)
    objects = compile_all(test, outputs)
    if lost := lost_definitions(objects, originals):
        names = run(["c++filt"], input="\n".join(lost)).stdout.splitlines()
        raise Failure(f"leaves undefined what the original defines: {'; '.join(names[:3])}")
    if expected is not None:
        link(objects, work / f"{mode}.exe")
        if (got := execute(work / f"{mode}.exe")) != expected:
            raise Failure(f"prints or returns other than the original: {got} vs {expected}")
    text = "\n".join(o.read_text() for o in outputs)
    for wanted, pattern in test.checks[mode]:
        if (re.search(pattern, text, re.M) is not None) != wanted:
            raise Failure(f"{'CHECK' if wanted else 'CHECK-NOT'}: {pattern}")


def run_test(test: Test, keep: pathlib.Path | None) -> list[tuple[str, str, str]]:
    """The results of every mode of @p test, as (mode, PASS|FAIL|XFAIL|XPASS, why)."""
    work = pathlib.Path(tempfile.mkdtemp(prefix="invisible-test-"))
    results = []
    try:
        # invisible takes a translation unit without #include and line markers
        for unit in test.units:
            expanded = run([CXX, *test.language(), "-E", "-P", unit, "-o", work / unit.name])
            if expanded.returncode:
                return [(mode, "FAIL", f"the test does not compile: {first_error(expanded.stderr)}") for mode in test.modes]
        originals = compile_all(test, [work / u.name for u in test.units])
        # a test that only declares some of what it calls is not run
        expected = None
        if test.is_program and run([CXX, *originals, "-o", work / "original.exe"]).returncode == 0:
            expected = execute(work / "original.exe")
        for mode in test.modes:
            try:
                run_mode(test, mode, work, originals, expected)
                results.append((mode, "XPASS" if mode in test.xfail else "PASS", ""))
            except Failure as failure:
                results.append((mode, "XFAIL" if mode in test.xfail else "FAIL", str(failure)))
    except Failure as failure:
        results = [(mode, "FAIL", f"the original: {failure}") for mode in test.modes]
    finally:
        if keep:
            shutil.copytree(work, keep / test.name.replace("/", "_"), dirs_exist_ok=True)
        shutil.rmtree(work, ignore_errors=True)
    return results


def discover(filters: list[str]) -> list[Test]:
    paths = sorted((HERE / "cases").glob("*.c*")) + sorted(p for p in (HERE / "link").iterdir() if p.is_dir())
    tests = [Test(p) for p in paths]
    return [t for t in tests if not filters or any(f in t.name for f in filters)]


def main() -> int:
    global INVISIBLE
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("filters", nargs="*")
    parser.add_argument("-j", type=int, default=os.cpu_count() // 2)
    parser.add_argument("--invisible", type=pathlib.Path, default=INVISIBLE)
    parser.add_argument("--keep", type=pathlib.Path, help="keep the files of every test in this directory")
    args = parser.parse_args()
    INVISIBLE = args.invisible.resolve()

    tests = discover(args.filters)
    counts = dict.fromkeys(("PASS", "FAIL", "XFAIL", "XPASS"), 0)
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        for test, results in zip(tests, pool.map(lambda t: run_test(t, args.keep), tests)):
            for mode, status, why in results:
                counts[status] += 1
                if status != "PASS":
                    print(f"{status:5} {test.name} [{mode}] {why}", flush=True)
    print(", ".join(f"{n} {status.lower()}" for status, n in counts.items()) + f" ({len(tests)} tests)")
    return 1 if counts["FAIL"] or counts["XPASS"] else 0


if __name__ == "__main__":
    sys.exit(main())
