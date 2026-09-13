#!/usr/bin/env python3
"""Compile and run production PCM regressions; requires a C compiler and ALSA development files."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="quectel-pcm-") as directory:
        compiler = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c99", "-D_POSIX_C_SOURCE=200809L", "-DAST_CONFIG_H",
            "-Wall", "-Wextra", "-Werror",
            "-I" + str(root / "test/pcm-stubs"), "-I" + str(root / "src"),
            str(root / "src/pcm.c"),
        ]
        for source in ("pcm.c", "pcm_null.c"):
            executable = Path(directory) / Path(source).stem
            command = compiler + [str(root / "test" / source), "-lasound", "-o", str(executable)]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
