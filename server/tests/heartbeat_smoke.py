"""TCP heartbeat: valid pong survives; stale/wrong pong cannot prevent timeout."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import uuid
from network_game_smoke import blob, decode, exact, frame, integer, port, read_tcp


def raw_packet(client):
    size = struct.unpack("!H", exact(client, 2))[0]
    return decode(exact(client, size))


def run():
    executable = Path(__file__).resolve().parents[1] / "build/x64-debug/main.exe"
    tcp_port, udp_port = port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
    match = str(uuid.uuid4())
    ids = [str(uuid.uuid4()) for _ in range(2)]
    # Use the same CLI convention as network_game_smoke.
    args = [str(executable), "--match-id", match, "--auth-token", "heartbeat-test",
            "--tcp-port", str(tcp_port), "--udp-port", str(udp_port),
            "--players", ",".join(ids)]
    process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, env=os.environ.copy())
    logs = []
    log_reader = threading.Thread(target=lambda: logs.extend(process.stdout), daemon=True)
    log_reader.start()
    clients, datagrams = [], []
    try:
        for player in ids:
            client = socket.socket()
            until = time.monotonic() + 5
            while True:
                try:
                    client.connect(("127.0.0.1", tcp_port))
                    break
                except OSError:
                    assert process.poll() is None and time.monotonic() < until
                    time.sleep(0.02)
            client.settimeout(18)
            clients.append(client)
            client.sendall(frame(101, blob(1, player) + integer(2, 1)))
            assert read_tcp(client)[1][0] == 101
            datagram = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            datagram.bind(("127.0.0.1", 0))
            datagram.settimeout(5)
            datagrams.append(datagram)
            datagram.sendto(frame(201, blob(1, player) + blob(2, match) + integer(3, 1)),
                            ("127.0.0.1", udp_port))
            while decode(datagram.recv(65535)[2:])[1][0] != 201:
                pass
        for client in clients:
            assert read_tcp(client)[1][0] == 101

        valid_pongs = []
        rtt_reports = []
        def healthy_client():
            try:
                while True:
                    packet = raw_packet(clients[0])
                    if packet[1][0] == 102:
                        clients[0].sendall(frame(102, packet[2][0]))
                        valid_pongs.append(packet[2][0])
                    elif packet[1][0] == 104:
                        report = decode(packet[2][0])
                        assert report[1][0] > 0
                        assert 0 <= report.get(2, [0])[0] < 10_000_000
                        rtt_reports.append(report)
            except (OSError, AssertionError):
                return
        healthy = threading.Thread(target=healthy_client, daemon=True)
        healthy.start()

        # A first valid pong must not make old replies valid for subsequent probes.
        first = None
        until = time.monotonic() + 18
        disconnected = False
        while time.monotonic() < until:
            try:
                packet = raw_packet(clients[1])
                if packet[1][0] == 102:
                    if first is None:
                        first = packet[2][0]
                        clients[1].sendall(frame(102, first))
                    else:
                        assert packet[2][0] != first
                        clients[1].sendall(frame(102, first))
                        clients[1].sendall(frame(102, b"invalid-sequence"))
            except AssertionError as error:
                if str(error) != "server disconnected":
                    raise
                disconnected = True
                break
            except ConnectionError:
                disconnected = True
                break
        assert disconnected, "stale/wrong pong prevented heartbeat timeout"
        assert len(valid_pongs) >= 3, "healthy peer did not keep answering"
        assert len(rtt_reports) >= 2, "server did not deliver RTT reports"
        assert process.poll() is None, "timeout disconnected the healthy peer too"
        process.stdin.write("quit\n")
        process.stdin.flush()
        assert process.wait(timeout=8) == 0
        log_reader.join(timeout=2)
        healthy.join(timeout=2)
        assert any(ids[1] in line and "TCP heartbeat timeout" in line for line in logs)
        assert not any(ids[0] in line and "TCP heartbeat timeout" in line for line in logs)
        print("PASS: valid pong keeps TCP session alive beyond heartbeat timeout")
        print("PASS: server RTT reports contain sequence and microsecond measurements")
        print("PASS: stale/wrong pong is ignored and nonresponsive session is disconnected")
        print("PASS: heartbeat timers cancel and server shuts down cleanly")
    finally:
        for client in clients + datagrams:
            client.close()
        if process.poll() is None:
            process.kill()
            process.wait()


if __name__ == "__main__":
    run()
