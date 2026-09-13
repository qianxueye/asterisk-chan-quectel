#!/usr/bin/env python3
"""Compile actual channel/call lifecycle functions against deterministic fakes.

Only the surrounding Asterisk/ALSA environment is faked. Production functions
are extracted at test time so regressions in call order, buffer offsets, and
stream ownership are exercised without a modem or an Asterisk daemon.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = {
    "src/chan_quectel.c": ["soundcard_init", "pvt_on_remove_last_channel"],
    "src/cpvt.c": ["cpvt_free", "cpvt_stop_uac", "cpvt_call_disactivate", "cpvt_call_activate"],
    "src/channel.c": ["channel_start_capture", "channel_read_uac", "channel_read", "channel_write_uac", "channel_write"],
}


def extract(source: str, name: str) -> str:
    match = re.search(r"^(?:static )?[^\n;{}]+\b" + name + r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError(f"Production function not found: {name}")
    # The selected functions contain no braces in string literals. Tokenize
    # comments and literals too, so harmless log/comment edits remain safe.
    start = source.index("{", match.start())
    tokens = re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[start:], re.S)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                return source[match.start():start + token.end()]
    raise RuntimeError(f"Unclosed production function: {name}")


def main() -> None:
    chunks = []
    for filename, names in FUNCTIONS.items():
        source = (ROOT / filename).read_text()
        for name in names:
            chunks.append(extract(source, name))
    # Exercise the actual disconnect traversal without bringing in unrelated
    # modem/SMS teardown. Its callback frees the current list member.
    disconnect = extract((ROOT / "src/chan_quectel.c").read_text(), "pvt_disconnect")
    chunks.append(disconnect[:disconnect.index("    if (pvt->initialized)")] + "}\n")
    channel_new = extract((ROOT / "src/channel.c").read_text(), "channel_new")
    initial_fd = re.search(r"ast_channel_set_fd\(channel, 0,.*?\);", channel_new).group()
    chunks.append("static void initial_audio_fd(struct pvt *pvt, struct ast_channel *channel) { " + initial_fd + " }")
    harness = (ROOT / "test/audio_lifecycle_harness.c").read_text()
    generated = harness.replace("/* INSERT PRODUCTION FUNCTIONS */", "\n\n".join(chunks))
    with tempfile.TemporaryDirectory(prefix="quectel-audio-test-") as temp:
        source = Path(temp) / "audio_lifecycle.c"
        binary = Path(temp) / "audio_lifecycle"
        source.write_text(generated)
        cmd = shlex.split(os.environ.get("CC", "cc"))
        cmd += ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-sign-compare"]
        cmd += shlex.split(os.environ.get("AUDIO_TEST_CFLAGS", ""))
        subprocess.run(cmd + [str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
