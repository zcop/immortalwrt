#!/usr/bin/env python3
"""Exercise the shipped button handler without erasing or rebooting anything."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "package/base-files/files/etc/rc.button/reset"


class ResetButtonTest(unittest.TestCase):
    def run_handler(self, action, seen=0, overlay=True, reset_result=0):
        with tempfile.TemporaryDirectory(prefix="reset-button-test-") as directory:
            work = Path(directory)
            trace = work / "trace"
            mounts = work / "mounts"
            mounts.write_text("ubi1:userdata /overlay ubifs rw 0 0\n" if overlay else "")
            functions = work / "functions.sh"
            functions.write_text(":\n")
            diag = work / "diag.sh"
            diag.write_text('set_state() { printf "state %s\\n" "$*" >> "$TRACE"; }\n')
            for command in ("sync", "reboot", "factoryreset"):
                program = work / command
                program.write_text(
                    '#!/bin/sh\nprintf "%s %s\\n" "' + command + '" "$*" >> "$TRACE"\n'
                    + ('exit "$RESET_RESULT"\n' if command == "factoryreset" else "exit 0\n")
                )
                program.chmod(0o755)
            text = SOURCE.read_text()
            for original, replacement in {
                "/lib/functions.sh": str(functions),
                "/etc/diag.sh": str(diag),
                "/proc/mounts": str(mounts),
                "/dev/console": str(work / "console"),
            }.items():
                self.assertEqual(text.count(original), 2 if original == "/dev/console" else 1)
                text = text.replace(original, replacement)
            handler = work / "handler.sh"
            handler.write_text(text)
            environment = os.environ | {
                "PATH": str(work) + ":/usr/bin:/bin",
                "TRACE": str(trace),
                "ACTION": action,
                "SEEN": str(seen),
                "RESET_RESULT": str(reset_result),
            }
            result = subprocess.run(
                ["/bin/sh", "-c", '. "$1"', "test", str(handler)],
                env=environment, capture_output=True, text=True, timeout=5,
            )
            calls = trace.read_text().splitlines() if trace.exists() else []
            return result.returncode, [line.rstrip() for line in calls]

    def test_long_press_uses_factoryreset_builtin_reboot(self):
        for seen in (5, 6, 10):
            with self.subTest(seen=seen):
                self.assertEqual(self.run_handler("released", seen), (0, ["factoryreset -y -r"]))

    def test_reset_failure_never_starts_external_reboot(self):
        self.assertEqual(self.run_handler("released", 10, reset_result=1), (0, ["factoryreset -y -r"]))

    def test_short_press_reboots_without_erasing(self):
        self.assertEqual(self.run_handler("released", 0), (0, ["sync", "reboot"]))

    def test_intermediate_press_does_nothing(self):
        for seen in (1, 2, 4):
            with self.subTest(seen=seen):
                self.assertEqual(self.run_handler("released", seen), (0, []))

    def test_no_overlay_prevents_reset(self):
        self.assertEqual(self.run_handler("released", 10, overlay=False), (0, []))
        self.assertEqual(self.run_handler("pressed", overlay=False), (0, []))

    def test_pressed_requests_five_second_timer(self):
        self.assertEqual(self.run_handler("pressed"), (5, []))

    def test_timeout_only_changes_indicator(self):
        self.assertEqual(self.run_handler("timeout"), (0, ["state failsafe"]))


if __name__ == "__main__":
    unittest.main()
