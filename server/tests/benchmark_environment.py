"""실제 서버 벤치마크의 입력 검증과 빌드·시스템 환경 수집."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess


def positive_int(value):
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def positive_seconds(value):
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return result


def command_output(command):
    try:
        return subprocess.check_output(command, text=True, encoding="utf-8",
                                       errors="replace", timeout=5, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def collect_environment(executable):
    # 측정 시점의 환경을 저장하고 보고서 생성 시에는 다시 수집하지 않음.
    system = platform.system()
    cpu = platform.processor() or None
    memory = None
    if system == "Darwin":
        cpu = command_output(["sysctl", "-n", "machdep.cpu.brand_string"]) or cpu
        memory = command_output(["sysctl", "-n", "hw.memsize"])
    elif system == "Linux":
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
                if line.startswith("model name"):
                    cpu = line.split(":", 1)[1].strip()
                    break
            for line in Path("/proc/meminfo").read_text().splitlines():
                if line.startswith("MemTotal:"):
                    memory = int(line.split()[1]) * 1024
                    break
        except OSError:
            pass
    elif system == "Windows":
        raw = command_output(["powershell", "-NoProfile", "-Command",
                              "@{cpu=(Get-CimInstance Win32_Processor | Select-Object -First 1).Name; "
                              "memory=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory} | ConvertTo-Json"])
        if raw:
            try:
                windows = json.loads(raw)
                cpu, memory = windows.get("cpu"), windows.get("memory")
            except ValueError:
                pass
    digest = hashlib.sha256()
    with executable.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    raw_build = command_output([str(executable), "--build-info"])
    build = {"configuration": None, "source": "unavailable"}
    if raw_build:
        try:
            candidate = json.loads(raw_build)
            if isinstance(candidate, dict) and "configuration" in candidate:
                build = dict(candidate, source="executable --build-info")
        except ValueError:
            pass
    build["executable_sha256"] = digest.hexdigest()
    return {
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        "build": build,
        "system": {"os": platform.platform(), "architecture": platform.machine(),
                   "cpu_model": cpu, "logical_cpus": os.cpu_count(),
                   "total_memory_bytes": int(memory) if memory is not None else None,
                   "python_version": platform.python_version(),
                   "python_implementation": platform.python_implementation(),
                   "load_average_at_start": list(os.getloadavg()) if hasattr(os, "getloadavg") else None},
        "runtime": {"network": "loopback; server and clients on the same host",
                    "server_metrics_env": os.environ.get("SERVER_METRICS", "unset")}
    }

