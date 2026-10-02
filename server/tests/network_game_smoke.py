"""Loopback integration test without protobuf/Python package dependencies.

Run: python server/tests/network_game_smoke.py
Exercises the existing protobuf encoding format against the compiled Debug server.
"""
import os
import argparse
import json
import queue
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import uuid


def varint(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def integer(field, value):
    return varint(field << 3) + varint(value)


def blob(field, value):
    if isinstance(value, str):
        value = value.encode()
    return varint((field << 3) | 2) + varint(len(value)) + value


def number(field, value):
    return varint((field << 3) | 5) + struct.pack("<f", value)


def decode(data):
    position = 0
    result = {}

    def read_int():
        nonlocal position
        value = shift = 0
        while True:
            byte = data[position]
            position += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
            shift += 7

    while position < len(data):
        tag = read_int()
        field, kind = tag >> 3, tag & 7
        if kind == 0:
            value = read_int()
        elif kind == 2:
            length = read_int()
            value = data[position:position + length]
            position += length
        elif kind == 5:
            value = struct.unpack_from("<f", data, position)[0]
            position += 4
        else:
            raise AssertionError(f"unexpected protobuf encoding type {kind}")
        result.setdefault(field, []).append(value)
    return result


def frame(kind, payload):
    body = integer(1, kind) + blob(2, payload)
    return struct.pack("!H", len(body)) + body


def exact(sock, length):
    data = b""
    while len(data) < length:
        part = sock.recv(length - len(data))
        assert part, "server disconnected"
        data += part
    return data


def read_tcp(sock):
    while True:
        length = struct.unpack("!H", exact(sock, 2))[0]
        packet = decode(exact(sock, length))
        if packet[1][0] == 102:
            sock.sendall(frame(102, packet.get(2, [b""])[0]))
            continue
        if packet[1][0] == 104:
            continue
        return packet


def port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def run(count=12, finish="quit"):
    root = Path(__file__).resolve().parents[1]
    executable = root / "build/x64-debug/main.exe"
    tcp_port, udp_port = port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
    match = str(uuid.uuid4())
    ids = [str(uuid.uuid4()) for _ in range(count)]
    reports = []
    report_thread = None
    report_socket = None
    if finish != "quit":
        # Refuse to run this scenario if a real auth service occupies this port.
        report_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        report_socket.bind(("127.0.0.1", 18080))
        report_socket.listen(1)
        report_socket.settimeout(15)

        def report_server():
            with report_socket:
                client, _ = report_socket.accept()
                with client:
                    data = b""
                    while b"\r\n\r\n" not in data:
                        data += client.recv(4096)
                    headers, body = data.split(b"\r\n\r\n", 1)
                    length = next(int(line.split(b":", 1)[1]) for line in headers.split(b"\r\n")
                                  if line.lower().startswith(b"content-length:"))
                    while len(body) < length:
                        body += client.recv(4096)
                    reports.append(json.loads(body[:length]))
                    if finish == "stall":
                        time.sleep(4)
                    else:
                        client.sendall(b"HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n")

        report_thread = threading.Thread(target=report_server, daemon=True)
        report_thread.start()
    env = dict(os.environ, SERVER_METRICS="1")
    process = subprocess.Popen(
        [str(executable), "--match-id", match, "--auth-token", "local-smoke",
         "--tcp-port", str(tcp_port), "--udp-port", str(udp_port),
         "--players", ",".join(ids)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", env=env)
    logs = []
    reader = threading.Thread(target=lambda: logs.extend(process.stdout), daemon=True)
    reader.start()
    tcp, udp = [], []
    try:
        for index, player in enumerate(ids):
            deadline = time.monotonic() + 5
            while True:
                try:
                    client = socket.create_connection(("127.0.0.1", tcp_port), timeout=2)
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.02)
            tcp.append(client)
            client.settimeout(5)
            client.sendall(frame(101, blob(1, player) + integer(2, index + 1)))
            response = read_tcp(client)
            assert response[1][0] == 101
            info = decode(response[2][0])
            assert info[1][0].decode() == player
            assert info[2][0] == index + 1
            datagram = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            datagram.bind(("127.0.0.1", 0))
            datagram.settimeout(5)
            udp.append(datagram)
            datagram.sendto(frame(201, blob(1, player) + blob(2, match) + integer(3, 1)),
                            ("127.0.0.1", udp_port))
            while True:
                packet = datagram.recv(65535)
                parsed = decode(packet[2:])
                if parsed[1][0] == 201:
                    assert decode(parsed[2][0])[1][0].decode() == player
                    break

        initial = []
        for client in tcp:
            response = read_tcp(client)
            assert response[1][0] == 101
            info = decode(response[2][0])
            assert len(info[14]) == count
            initial.append(info)
        assert sum(info[3][0] == 1 for info in initial) == count // 2
        assert sum(info[3][0] == 2 for info in initial) == count // 2
        print(f"PASS: {count} handshakes, UDP authentication, balanced initialization")

        # Read continuously like the real client, including pong responses while idle.
        tcp_packets = [queue.Queue() for _ in tcp]
        def receive_packets(client, packets):
            try:
                while True:
                    packets.put(read_tcp(client))
            except (OSError, AssertionError):
                return
        for client, packets in zip(tcp, tcp_packets):
            client.settimeout(20)
            threading.Thread(target=receive_packets, args=(client, packets), daemon=True).start()

        for datagram in udp:
            datagram.settimeout(0.03)
            try:
                while True:
                    datagram.recv(65535)
            except socket.timeout:
                pass
        spawn = initial[0].get(4, [0.0])[0]
        x = spawn + 0.1
        move = blob(1, ids[0]) + number(2, x) + number(5, 1.0)
        input_packet = blob(1, ids[0]) + blob(2, match) + integer(3, 1) + blob(4, move)
        udp[0].sendto(frame(200, input_packet), ("127.0.0.1", udp_port))
        received = []
        for datagram in udp:
            datagram.settimeout(5)
            while True:
                sendBuffer = datagram.recv(65535)
                packet = decode(sendBuffer[2:])
                if packet[1][0] != 200:
                    continue
                ingame = decode(packet[2][0])
                if ingame.get(3, [0])[0] == 1 and ingame[1][0].decode() == ids[0]:
                    movement = decode(ingame[4][0])
                    if abs(movement[2][0] - x) >= 0.001:
                        continue  # A delayed initial-state datagram can still be queued.
                    received.append(sendBuffer)
                    break
        assert all(sendBuffer == received[0] for sendBuffer in received)
        print(f"PASS: Game input -> identical UDP sendBuffer delivered to all {count} peers")

        # Unknown endpoint and another user's ID cannot inject game commands.
        outsider = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        forged_move = blob(1, ids[0]) + number(2, x + 1)
        forged = blob(1, ids[0]) + blob(2, match) + integer(3, 1) + blob(4, forged_move)
        outsider.sendto(frame(200, forged), ("127.0.0.1", udp_port))
        outsider.close()
        forged_id = blob(1, ids[1]) + blob(2, match) + integer(3, 2)
        udp[0].sendto(frame(200, forged_id), ("127.0.0.1", udp_port))
        # Observe victim state after forged Jump: no positive Y may be broadcast.
        udp[0].settimeout(0.1)
        until = time.monotonic() + 0.3
        while time.monotonic() < until:
            try:
                packet = decode(udp[0].recv(65535)[2:])
            except socket.timeout:
                continue
            if packet[1][0] == 200:
                game = decode(packet[2][0])
                if game.get(3, [0])[0] == 1 and game[1][0].decode() == ids[1]:
                    assert decode(game[4][0]).get(3, [0.0])[0] == 0.0
        print("PASS: forged sender cannot trigger another player's Jump")

        # Enough active ticks to exercise bounded metric windows.
        time.sleep(4.5)
        assert process.poll() is None
        if finish == "quit":
            process.stdin.write("quit\n")
            process.stdin.flush()
        else:
            # Generate a real win through game input. Other stationary players remain
            # aligned on the initial spawn line; a horizontal shot crosses their capsules.
            shooter = min(range(1, count), key=lambda index: initial[index].get(4, [0.0])[0])
            udp[shooter].settimeout(0.1)
            tick = 0
            until = time.monotonic() + 0.2
            while time.monotonic() < until:
                try:
                    packet = decode(udp[shooter].recv(65535)[2:])
                    if packet[1][0] == 200:
                        tick = max(tick, decode(packet[2][0]).get(5, [0])[0])
                except socket.timeout:
                    break
            shot = (blob(1, ids[shooter]) + blob(2, match) + integer(3, 3) +
                    blob(4, struct.pack("<fff", 1.0, 0.0, 0.0)) + integer(5, tick))
            for _ in range(80):
                udp[shooter].sendto(frame(200, shot), ("127.0.0.1", udp_port))
            for packets in tcp_packets:
                while packets.get(timeout=8)[1][0] != 103:
                    pass
            print("PASS: real combat win delivers final TCP EndGame to every peer")
        assert process.wait(timeout=8) == 0
        reader.join(timeout=2)
        assert any("Server shutdown complete" in line for line in logs)
        assert any("read finished during shutdown" in line for line in logs)
        assert not any("[error]" in line and "TCP" in line and "read" in line for line in logs)
        assert not any("TCP write failed" in line for line in logs)
        print("PASS: shutdown reads are classified normally and TCP writes completed without failure")
        assert any("[metrics] tick" in line for line in logs)
        print(f"PASS: tick metrics, drain and shutdown with {count} live connections")
        if finish != "quit":
            assert reports and len(reports[0]["playerStats"]) == count
            assert max(reports[0]["TeamAScore"], reports[0]["TeamBScore"]) >= 100
            if finish == "stall":
                assert any("match result reporting failed" in line for line in logs)
            print(f"PASS: HTTP result reporting ({finish}) and bounded shutdown")
        for line in logs:
            if "[metrics]" in line:
                print(line.strip())
    finally:
        for client in tcp + udp:
            client.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        reader.join(timeout=2)
        if report_thread:
            report_thread.join(timeout=5)
        if process.returncode:
            print("".join(logs[-20:]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--players", type=int, choices=(10, 12), default=12)
    parser.add_argument("--finish", choices=("quit", "match", "stall"), default="quit")
    arguments = parser.parse_args()
    run(arguments.players, arguments.finish)
