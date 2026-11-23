#!/usr/bin/env python3
"""Build the portable QuickSift core and run every deterministic check program.

CODE GUIDE: See TESTING.md -> deterministic check programs. Used by CI
(.github/workflows/ci.yml, job "portable-checks") and by hand on any non-Windows
host. Standard library only: no third-party Python packages are needed.

    python3 scripts/run-portable-checks.py --build-dir /tmp/qs-core --log-dir /tmp/qs-check-logs

Every program in scripts/checks/*.cpp is compiled against libQuickSiftCore.a
(localization_checks also links src/localization.cpp) and must exit 0. The
build directory must be outside the source tree (verify-source.ps1 rejects
build*/ folders inside it).
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTRA_SOURCES = {"localization_checks": ["src/localization.cpp"]}
RUN_TIMEOUT_SECONDS = 120


def run(command: list[str], log: Path, cwd: Path = ROOT, timeout: int | None = None) -> int:
    with log.open("w", encoding="utf-8", errors="replace") as handle:
        handle.write("> " + " ".join(command) + "\n")
        handle.flush()
        try:
            return subprocess.run(command, cwd=cwd, stdout=handle, stderr=subprocess.STDOUT,
                                  timeout=timeout, check=False).returncode
        except subprocess.TimeoutExpired:
            handle.write(f"\nTIMEOUT after {timeout} s\n")
            return 124


def last_line(path: Path) -> str:
    lines = [line for line in path.read_text(encoding="utf-8", errors="replace").splitlines() if line.strip()]
    return lines[-1] if lines else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, default=Path(tempfile.gettempdir()) / "quicksift-portable-core")
    parser.add_argument("--log-dir", type=Path, default=Path(tempfile.gettempdir()) / "quicksift-check-logs")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"), help="C++ compiler (default: $CXX or c++)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--skip-core-build", action="store_true", help="reuse an existing --build-dir")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    log_dir = args.log_dir.resolve()
    if build_dir == ROOT or ROOT in build_dir.parents:
        print(f"error: --build-dir must be outside the source tree: {build_dir}", file=sys.stderr)
        return 2
    log_dir.mkdir(parents=True, exist_ok=True)
    binaries = log_dir / "bin"
    binaries.mkdir(exist_ok=True)

    if not args.skip_core_build:
        configure = ["cmake", "-S", str(ROOT), "-B", str(build_dir), "-DQS_WARNINGS_AS_ERRORS=ON",
                     "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_CXX_COMPILER={args.cxx}"]
        if shutil.which("ninja"):
            configure[1:1] = ["-G", "Ninja"]
        if run(configure, log_dir / "core-configure.log") != 0:
            print((log_dir / "core-configure.log").read_text(errors="replace"))
            return 1
        if run(["cmake", "--build", str(build_dir), "--parallel", str(args.jobs)], log_dir / "core-build.log") != 0:
            print((log_dir / "core-build.log").read_text(errors="replace"))
            return 1
    library = build_dir / "libQuickSiftCore.a"
    if not library.is_file():
        print(f"error: {library} was not built", file=sys.stderr)
        return 1

    checks = sorted((ROOT / "scripts" / "checks").glob("*.cpp"))
    if not checks:
        print("error: no check programs found in scripts/checks", file=sys.stderr)
        return 1

    def check(source: Path) -> tuple[str, bool, str]:
        name = source.stem
        executable = binaries / name
        compile_command = [args.cxx, "-std=c++20", "-O1", "-Wall", "-Wextra", f"-I{ROOT / 'src'}", str(source)]
        compile_command += [str(ROOT / extra) for extra in EXTRA_SOURCES.get(name, [])]
        compile_command += [str(library), "-lpthread", "-o", str(executable)]
        if run(compile_command, log_dir / f"{name}.compile.log") != 0:
            return name, False, "compile failed (see " + str(log_dir / f"{name}.compile.log") + ")"
        run_log = log_dir / f"{name}.run.log"
        code = run([str(executable)], run_log, cwd=ROOT, timeout=RUN_TIMEOUT_SECONDS)
        return name, code == 0, last_line(run_log) or f"exit {code}"

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(check, checks))
    for name, ok, message in results:
        print(("PASS " if ok else "FAIL ") + name + ": " + message)
    passed = sum(ok for _, ok, _ in results)
    print(f"TOTAL {passed} / {len(results)} check programs passed (logs: {log_dir})")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
