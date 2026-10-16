#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""用真实子进程验证日志收集，测试不依赖 macOS 或 LLDB。"""

import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest


sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "macos_gui_diagnostics",
    Path(__file__).resolve().parents[1] / "macos-gui-diagnostics.py",
)
DIAGNOSTICS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DIAGNOSTICS)


class CaptureTests(unittest.TestCase):
    def test_preserves_failure_and_both_output_streams(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "failure.log"
            status = DIAGNOSTICS.capture(
                [sys.executable, "-c",
                 "import sys; print('stdout evidence'); "
                 "print('stderr evidence', file=sys.stderr); sys.exit(7)"],
                log,
            )
            self.assertEqual(status, 7)
            self.assertIn("stdout evidence", log.read_text())
            self.assertIn("stderr evidence", log.read_text())
            self.assertIn("exit_status=7", log.read_text())

    def test_timeout_retains_partial_output_and_terminates_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "timeout.log"
            status = DIAGNOSTICS.capture(
                [sys.executable, "-u", "-c",
                 "import time; print('before timeout'); time.sleep(60)"],
                log,
                timeout=0.3,
            )
            self.assertEqual(status, 124)
            self.assertIn("before timeout", log.read_text())
            self.assertIn("timed out", log.read_text())

    def test_missing_debugger_is_reported_in_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "missing.log"
            status = DIAGNOSTICS.capture(
                [str(Path(temporary) / "missing-lldb")], log,
            )
            self.assertEqual(status, 127)
            self.assertIn("could not start", log.read_text())

    def test_timeout_also_terminates_a_descendant_in_another_session(self):
        # macOS debugserver 会另建进程组；仅终止 LLDB 的进程组会遗留 GUI。
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "descendant.log"
            descendant = None
            try:
                status = DIAGNOSTICS.capture(
                    [sys.executable, "-u", "-c",
                     "import subprocess, sys, time; "
                     "child = subprocess.Popen([sys.executable, '-c', "
                     "'import time; time.sleep(60)'], start_new_session=True); "
                     "print(f'escaped_pid={child.pid}', flush=True); time.sleep(60)"],
                    log, timeout=0.5,
                )
                self.assertEqual(status, 124)
                descendant = int(next(
                    line.removeprefix("escaped_pid=")
                    for line in log.read_text().splitlines()
                    if line.startswith("escaped_pid=")
                ))
                deadline = time.monotonic() + 1
                while time.monotonic() < deadline:
                    state = subprocess.run(
                        ["ps", "-p", str(descendant), "-o", "stat="],
                        capture_output=True, text=True, check=False,
                    ).stdout.strip()
                    if not state or state.startswith("Z"):
                        break
                    time.sleep(0.01)
                else:
                    self.fail("timed-out debugger left its detached descendant alive")
            finally:
                if descendant:
                    try:
                        os.kill(descendant, signal.SIGKILL)
                    except ProcessLookupError:
                        pass


if __name__ == "__main__":
    unittest.main()
