#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""保留 macOS GUI 失败现场；仅收集证据，不改变原 CTest 结果。"""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess


def stop_descendants(process, output):
    # LLDB 的 debugserver 可另建进程组，必须同时清理进程树。
    try:
        listing = subprocess.run(
            ["ps", "-A", "-o", "pid=", "-o", "ppid="],
            capture_output=True, text=True, timeout=5, check=True,
        )
        children = {}
        for line in listing.stdout.splitlines():
            pid, parent = map(int, line.split())
            children.setdefault(parent, []).append(pid)
        descendants = []
        pending = list(children.get(process.pid, []))
        while pending:
            pid = pending.pop()
            descendants.append(pid)
            pending.extend(children.get(pid, []))
        for pid in reversed(descendants):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        output.write(f"could not enumerate debugger descendants: {error}\n")
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def capture(command, log, timeout=40):
    """保留标准输出、错误和退出码；限制调试器及其被调试进程的寿命。"""
    with log.open("w", encoding="utf-8") as output:
        output.write(f"$ {shlex.join(command)}\n")
        output.flush()
        try:
            process = subprocess.Popen(
                command, stdout=output, stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        except OSError as error:
            output.write(f"could not start: {error}\nexit_status=127\n")
            return 127
        try:
            status = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            stop_descendants(process, output)
            output.write(f"timed out after {timeout} seconds\n")
            status = 124
        output.write(f"exit_status={status}\n")
        return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = build / "diagnostics"
    output.mkdir(parents=True, exist_ok=True)

    # 首先保存原始测试结果；调试重跑不覆盖 LastTest.log。
    for name in ("LastTest.log", "LastTestsFailed.log"):
        source = build / "Testing" / "Temporary" / name
        if source.is_file():
            shutil.copy2(source, output / name)

    capture(["sw_vers"], output / "system.log")
    capture(["uname", "-a"], output / "architecture.log")
    os.environ["QT_QPA_PLATFORM"] = "cocoa"
    # 一个真实应用、一个最小测试：区分公共 Cocoa 崩溃与测试装配问题。
    targets = (
        (build / "apps/gui_qt/ZzRkDevTool.app/Contents/MacOS/ZzRkDevTool",
         ["--smoke-test"], "gui-smoke-lldb.log"),
        (build / "tests/gui_settings_restore_test", [], "settings-restore-lldb.log"),
    )
    for executable, arguments, name in targets:
        if not executable.is_file():
            print(f"未生成程序，无法收集回溯：{executable}", flush=True)
            continue
        log = output / name
        capture([
            "xcrun", "lldb", "--batch",
            "--one-line", "settings set target.disable-aslr false",
            "--one-line", "run",
            "--one-line-on-crash", "thread backtrace all",
            "--one-line-on-crash", "register read",
            "--", str(executable), *arguments,
        ], log)
        print(log.read_text(encoding="utf-8", errors="replace"), flush=True)

    # macOS 的系统报告可补充原始运行的崩溃线程、动态库与系统版本。
    reports = Path.home() / "Library/Logs/DiagnosticReports"
    for pattern in ("ZzRkDevTool*.ips", "ZzRkDevTool*.crash",
                    "gui_*_test*.ips", "gui_*_test*.crash"):
        for report in reports.glob(pattern):
            if report.is_file():
                shutil.copy2(report, output / report.name)


if __name__ == "__main__":
    main()
