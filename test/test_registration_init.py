#!/usr/bin/env python3
"""Exercise production initialization, command queue, parser and registration handlers."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from test_audio_lifecycle import extract

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = {
    "src/at_queue.c": [
        "at_queue_free_data", "at_queue_free", "at_queue_remove", "at_queue_add",
        "at_queue_remove_cmd", "at_queue_insert_const", "at_queue_insert_uid", "at_queue_insert",
    ],
    "src/at_command.c": [
        "at_enqueue_initialization", "at_enqueue_initialization_quectel",
        "at_enqueue_initialization_simcom", "at_enqueue_initialization_other",
    ],
    "src/helpers.c": ["gsm_is_registered"],
    "src/at_parse.c": ["mark_line", "strip_quoted", "at_parse_creg"],
    "src/at_response.c": ["at_response_creg", "at_response_cgmi"],
}


def main():
    command_header = (ROOT / "src/at_command.h").read_text()
    commands = command_header[command_header.index("#define AT_CMD_AS_ENUM"):]
    commands = commands[:commands.index("} at_cmd_t;") + len("} at_cmd_t;")]
    queue = (ROOT / "src/at_queue.h").read_text()
    queue = re.sub(r"^#include[^\n]*", "", queue, flags=re.M)
    chunks = []
    for filename, names in FUNCTIONS.items():
        source = (ROOT / filename).read_text()
        chunks.extend(extract(source, name) for name in names)
    harness = (ROOT / "test/registration_init_harness.c").read_text()
    generated = harness.replace("/* INSERT COMMAND TYPES */", commands)
    generated = generated.replace("/* INSERT QUEUE TYPES */", queue)
    generated = generated.replace("/* INSERT PRODUCTION FUNCTIONS */", "\n\n".join(chunks))
    with tempfile.TemporaryDirectory(prefix="quectel-registration-test-") as directory:
        source = Path(directory) / "registration_init.c"
        executable = Path(directory) / "registration_init"
        source.write_text(generated)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
            "-Wno-unused-variable", "-Wno-unused-function",
        ]
        command += shlex.split(os.environ.get("REGISTRATION_TEST_CFLAGS", ""))
        subprocess.run(command + [str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
