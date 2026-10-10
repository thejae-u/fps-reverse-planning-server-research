"""실제 서버의 benchmark ACK 및 일반 상태 응답 통합 검증."""
import unittest
import os
from pathlib import Path
import socket
import tempfile
import time
from fixed_rate_benchmark import TestServer
from network_game_smoke import blob, decode, frame, integer, number


@unittest.skipUnless(os.environ.get('BENCHMARK_SERVER'), 'BENCHMARK_SERVER를 지정하면 실제 서버 검증 수행')
class ProtocolIntegrationTests(unittest.TestCase):
    def start_server(self, enabled):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        server = TestServer(Path(os.environ['BENCHMARK_SERVER']).resolve(), 12, 5, Path(temporary.name), benchmark=enabled)
        self.addCleanup(server.close)
        server.connect()
        server.udp[0].setblocking(True)
        return server

    def send_move(self, server, sequence, x):
        movement = blob(1, server.ids[0]) + number(2, x)
        payload = (blob(1, server.ids[0]) + blob(2, server.match) + integer(3, 1)
                   + blob(4, movement) + integer(6, sequence))
        server.udp[0].send(frame(200, payload))

    def receive_ack(self, server, sequence):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            server.udp[0].settimeout(max(0.001, deadline - time.monotonic()))
            packet = decode(server.udp[0].recv(65535)[2:])
            if packet[1][0] == 202:
                ack = decode(packet[2][0])
                if ack[2][0] == sequence:
                    return ack
        self.fail('처리 확인 ACK 없음')

    def test_ack_for_accepted_and_rejected_input(self):
        server = self.start_server(True)
        self.send_move(server, 1, server.initial[0].get(4, [0.0])[0])
        accepted = self.receive_ack(server, 1)
        self.assertEqual(accepted.get(7, [0])[0], 1)
        self.assertGreaterEqual(accepted.get(8, [0])[0], 1)
        queue_us = accepted.get(4, [0])[0]
        wait_us = accepted.get(13, [0])[0]
        within_us = accepted.get(15, [0])[0]
        self.assertLessEqual(abs(queue_us - wait_us - within_us), 1)
        components = [accepted.get(field, [0])[0] for field in (17, 18, 19)]
        self.assertLessEqual(abs(accepted.get(14, [0])[0] - sum(components)), 2)
        remaining_us, overdue_us, lateness_us = (accepted.get(field, [0])[0] for field in (11, 12, 14))
        self.assertLessEqual(abs(wait_us - remaining_us - lateness_us + overdue_us), 2)
        self.send_move(server, 2, float('nan'))
        rejected = self.receive_ack(server, 2)
        self.assertEqual(rejected.get(7, [0])[0], 0)

    def test_normal_server_emits_state_without_benchmark_ack(self):
        server = self.start_server(False)
        self.send_move(server, 1, server.initial[0].get(4, [0.0])[0])
        deadline = time.monotonic() + 0.3
        state = False
        while time.monotonic() < deadline:
            server.udp[0].settimeout(max(0.001, deadline - time.monotonic()))
            try:
                packet = decode(server.udp[0].recv(65535)[2:])
            except socket.timeout:
                break
            self.assertNotEqual(packet[1][0], 202)
            if packet[1][0] == 200:
                game = decode(packet[2][0])
                if game[1][0].decode() == server.ids[0] and game.get(3, [0])[0] == 1:
                    self.assertEqual(game.get(6, [0])[0], 0)
                    state = True
        self.assertTrue(state)


if __name__ == '__main__':
    unittest.main()
