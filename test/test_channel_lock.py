#!/usr/bin/env python3
"""Run real channel callbacks and lock acquisition against pthread interleavings."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

import test_audio_lifecycle as lifecycle

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=ROOT)
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be positive")
    chunks = []
    channel = (args.source_dir / "src/channel.c").read_text()
    if "static struct pvt* channel_lock_pvt(" in channel:
        chunks.append(lifecycle.extract(channel, "channel_lock_pvt"))
    cpvt = (args.source_dir / "src/cpvt.c").read_text()
    chunks.extend(lifecycle.extract(cpvt, name) for name in ["cpvt_try_lock", "cpvt_unlock"])
    chunks.append(lifecycle.extract((args.source_dir / "src/chan_quectel.c").read_text(), "pvt_unlock"))
    chunks.extend(lifecycle.extract(channel, name) for name in ["channel_func_read", "channel_func_write"])
    # Build the same production chunks as the media suite, with the real lock
    # functions declared before the channel callbacks.
    for filename, names in lifecycle.FUNCTIONS.items():
        source = (args.source_dir / filename).read_text()
        chunks.extend(lifecycle.extract(source, name) for name in names)
    disconnect = lifecycle.extract((args.source_dir / "src/chan_quectel.c").read_text(), "pvt_disconnect")
    chunks.append(disconnect[:disconnect.index("    if (pvt->initialized)")] + "}\n")
    chunks.append("static void initial_audio_fd(struct pvt *pvt, struct ast_channel *channel) {}")
    fixture = (ROOT / "test/audio_lifecycle_harness.c").read_text()
    generated = "#define CHANNEL_LOCK_REGRESSION\n" + fixture.replace("/* INSERT PRODUCTION FUNCTIONS */", "\n\n".join(chunks))
    with tempfile.TemporaryDirectory(prefix="quectel-lock-test-") as temp:
        source = Path(temp) / "channel_lock.c"
        binary = Path(temp) / "channel_lock"
        source.write_text(generated)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
            "-Wno-sign-compare", "-I", str(ROOT / "test"),
        ]
        command += shlex.split(os.environ.get("LOCK_TEST_CFLAGS", ""))
        subprocess.run(command + [str(source), "-o", str(binary)], check=True)
        for _ in range(args.repeat):
            try:
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            except subprocess.TimeoutExpired as error:
                print(error.stderr.decode() if isinstance(error.stderr, bytes) else error.stderr)
                raise
            if result.returncode:
                print(result.stderr)
                result.check_returncode()
        print(result.stdout, end="")


if __name__ == "__main__":
    main()
