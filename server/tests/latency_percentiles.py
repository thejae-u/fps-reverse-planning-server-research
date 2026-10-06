"""Measure UDP Move input -> authoritative state response latency on loopback.

Run: python server/tests/latency_percentiles.py --samples 1000 --players 12
Optional: --server /path/to/main --output /tmp/server-latency
No third-party Python packages required. Launches and stops its own server.
Each round sends one input per player concurrently, then waits for responses.
This closed-loop workload measures application round trips, including tick wait;
it does not measure one-way latency, raw echo RTT, or fixed-rate saturation.
Percentiles use nearest rank over successful samples only; losses are separate.
"""
import argparse
from collections import deque
import csv
from datetime import datetime, timezone
import hashlib
import os
import platform
import json
import math
from pathlib import Path
import select
import socket
import struct
import subprocess
import threading
import time
import uuid

from result_artifacts import artifact_path, create_result_directory
from network_game_smoke import blob, decode, frame, integer, number, port, read_tcp


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


def default_server():
    root = Path(__file__).resolve().parents[1]
    for preset, name in (("macos-debug", "main"), ("x64-debug", "main.exe"),
                         ("linux-debug", "main")):
        candidate = root / "build" / preset / name
        if candidate.is_file():
            return candidate
    return root / "build/x64-debug/main.exe"


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


def summarize(rows):
    values = sorted(row["latency_ms"] for row in rows if row["status"] == "ok")
    def percentile(percent):
        return values[math.ceil(len(values) * percent / 100) - 1] if values else None
    return {"sent": len(rows), "received": len(values),
            "timeouts": len(rows) - len(values),
            "timeout_percent": 100 * (len(rows) - len(values)) / len(rows) if rows else 0,
            "min_ms": min(values) if values else None,
            "mean_ms": sum(values) / len(values) if values else None,
            "p50_ms": percentile(50), "p95_ms": percentile(95),
            "p99_ms": percentile(99), "max_ms": max(values) if values else None}


def run(args):
    executable = args.server.resolve()
    if not executable.is_file():
        raise RuntimeError(f"server executable not found: {executable}; use --server")
    environment = collect_environment(executable)
    directory, version = create_result_directory(args.output)
    tcp_port, udp_port = port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
    match = str(uuid.uuid4())
    ids = [str(uuid.uuid4()) for _ in range(args.players)]
    command = [str(executable), "--match-id", match, "--auth-token", "latency-test",
               "--tcp-port", str(tcp_port), "--udp-port", str(udp_port),
               "--players", ",".join(ids)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                               errors="replace")
    logs = deque(maxlen=100)
    log_thread = threading.Thread(target=lambda: logs.extend(process.stdout), daemon=True)
    log_thread.start()
    tcp, udp, readers, initial, rows = [], [], [], [], []
    stop = threading.Event()
    tcp_errors = []

    def keep_alive(client):
        try:
            while not stop.is_set():
                read_tcp(client)  # Answers heartbeat probes; ignores RTT reports.
        except (OSError, AssertionError) as error:
            if not stop.is_set():
                tcp_errors.append(str(error))

    try:
        for index, player in enumerate(ids):
            deadline = time.monotonic() + args.timeout
            while True:
                try:
                    client = socket.create_connection(("127.0.0.1", tcp_port), timeout=args.timeout)
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("server startup failed")
                    time.sleep(0.02)
            tcp.append(client)
            client.sendall(frame(101, blob(1, player) + integer(2, index + 1)))
            packet = read_tcp(client)
            if packet[1][0] != 101 or decode(packet[2][0])[1][0].decode() != player:
                raise RuntimeError("unexpected TCP handshake")
            datagram = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp.append(datagram)
            datagram.bind(("127.0.0.1", 0))
            datagram.connect(("127.0.0.1", udp_port))
            datagram.settimeout(args.timeout)
            datagram.send(frame(201, blob(1, player) + blob(2, match) + integer(3, 1)))
            deadline = time.monotonic() + args.timeout
            while True:
                datagram.settimeout(max(0.001, deadline - time.monotonic()))
                packet = decode(datagram.recv(65535)[2:])
                if packet[1][0] == 201 and decode(packet[2][0])[1][0].decode() == player:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("UDP authentication timed out")
        for client in tcp:
            packet = read_tcp(client)
            info = decode(packet[2][0])
            if packet[1][0] != 101 or len(info.get(14, [])) != args.players:
                raise RuntimeError("unexpected game initialization")
            initial.append(info)
            client.settimeout(None)
            reader = threading.Thread(target=keep_alive, args=(client,), daemon=True)
            readers.append(reader)
            reader.start()
        for datagram in udp:
            datagram.setblocking(False)

        print(f"UDP Move round trips: {args.players} players, {args.samples} rounds, "
              f"{args.warmup} warmup rounds", flush=True)
        started = time.perf_counter()
        for round_index in range(args.warmup + args.samples):
            if process.poll() is not None or tcp_errors:
                raise RuntimeError(f"server/client disconnected: {tcp_errors}")
            pending = {}
            for index, datagram in enumerate(udp):
                # Unique float32 coordinate identifies this input without changing the protocol.
                # Small increments stay within the server movement validation tolerance.
                x = struct.unpack("<f", struct.pack("<f", initial[index].get(4, [0.0])[0]
                                                   + (round_index + 1) * 0.001))[0]
                movement = (blob(1, ids[index]) + number(2, x)
                            + number(4, initial[index].get(6, [0.0])[0]))
                payload = blob(1, ids[index]) + blob(2, match) + integer(3, 1) + blob(4, movement)
                wire = frame(200, payload)
                sent_ns = time.perf_counter_ns()
                datagram.send(wire)
                pending[datagram] = (index, x, sent_ns, len(wire))
            deadline = time.monotonic() + args.timeout
            while pending and time.monotonic() < deadline:
                ready, _, _ = select.select(list(pending), [], [], max(0, deadline - time.monotonic()))
                for datagram in ready:
                    wire = datagram.recv(65535)
                    received_ns = time.perf_counter_ns()
                    if len(wire) < 2 or struct.unpack("!H", wire[:2])[0] != len(wire) - 2:
                        raise RuntimeError("invalid UDP frame length")
                    packet = decode(wire[2:])
                    if packet[1][0] != 200:
                        continue
                    game = decode(packet[2][0])
                    index, x, sent_ns, sent_bytes = pending[datagram]
                    if (game.get(3, [0])[0] != 1 or game[1][0].decode() != ids[index]
                            or game[2][0].decode() != match):
                        continue
                    movement = decode(game[4][0])
                    if movement[1][0].decode() != ids[index] or movement.get(2, [0.0])[0] != x:
                        continue  # Another player or an older state broadcast.
                    if round_index >= args.warmup:
                        rows.append({"sample": round_index - args.warmup + 1, "player": index,
                                     "status": "ok", "latency_ms": (received_ns - sent_ns) / 1e6,
                                     "sent_bytes": sent_bytes, "received_bytes": len(wire)})
                    del pending[datagram]
            for index, _, _, sent_bytes in pending.values():
                if round_index >= args.warmup:
                    rows.append({"sample": round_index - args.warmup + 1, "player": index,
                                 "status": "timeout", "latency_ms": None,
                                 "sent_bytes": sent_bytes, "received_bytes": 0})
            measured = round_index - args.warmup + 1
            if measured > 0 and measured % 100 == 0:
                print(f"Completed {measured}/{args.samples} rounds", flush=True)
        elapsed = time.perf_counter() - started
        summary = {"version": version, "test_kind": "closed_loop", "metric": "UDP Move input to matching authoritative state round trip",
                   "environment": environment,
                   "host": "127.0.0.1", "server": str(executable), "players": args.players,
                   "rounds": args.samples, "warmup_rounds": args.warmup,
                   "timeout_seconds": args.timeout, "percentile_method": "nearest_rank",
                   "workload": "closed_loop_one_outstanding_input_per_player",
                   "elapsed_seconds_including_warmup": elapsed,
                   "overall": summarize(rows),
                   "per_player": {str(i): summarize([row for row in rows if row["player"] == i])
                                  for i in range(args.players)}}
        csv_path, json_path = artifact_path(directory, "samples.csv"), artifact_path(directory, "summary.json")
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=("sample", "player", "status", "latency_ms",
                                                       "sent_bytes", "received_bytes"))
            writer.writeheader()
            writer.writerows(sorted(rows, key=lambda row: (row["sample"], row["player"])))
        json_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(summary["overall"], indent=2))
        print(f"CSV: {csv_path.resolve()}\nJSON: {json_path.resolve()}")
        stop.set()
        process.stdin.write("quit\n")
        process.stdin.flush()
        if process.wait(timeout=8) != 0:
            raise RuntimeError("server shutdown failed")
        return 1 if summary["overall"]["timeouts"] else 0
    except Exception:
        print("".join(logs))
        raise
    finally:
        stop.set()
        for client in tcp:
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        for client in tcp + udp:
            client.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        for reader in readers + [log_thread]:
            reader.join(timeout=2)
        for stream in (process.stdin, process.stdout):
            stream.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server", type=Path, default=default_server())
    parser.add_argument("--players", type=int, choices=(10, 12), default=12)
    parser.add_argument("--samples", type=positive_int, default=1000, help="measured rounds per player")
    parser.add_argument("--warmup", type=int, default=50, help="unmeasured rounds")
    parser.add_argument("--timeout", type=positive_seconds, default=2.0)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "latency_results")
    args = parser.parse_args()
    if args.warmup < 0:
        parser.error("--warmup must be nonnegative")
    raise SystemExit(run(args))
