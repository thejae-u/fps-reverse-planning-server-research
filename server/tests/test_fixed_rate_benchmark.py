"""벤치마크 집계의 유실·tick 중복·실행 쌍 비교 검증."""
import unittest
import os
from pathlib import Path
import socket
import tempfile
import time
from fixed_rate_benchmark import TestServer
from network_game_smoke import blob, decode, frame, integer, number
from fixed_rate_benchmark import FIELDS, STAGES, paired_comparison, summarize_records, scheduled_events
from benchmark_receiver import merge_event
from collections import defaultdict


def sample(sequence, status='accepted', latency=20, tick=1):
    row = dict.fromkeys(FIELDS)
    row.update(sequence=sequence, player=0, phase='measurement', scheduled_ms=sequence * 10,
               sent_ms=sequence * 10 + 1, send_lag_ms=1, status=status,
               rtt_ms=latency - 1, scheduled_latency_ms=latency,
               game_dispatch_ms=0.1, input_queue_ms=15, input_processing_ms=0.2,
               tick_execution_ms=0.4, queue_depth=4, tick=tick, state_superseded=False,
               next_tick_remaining_ms=10, tick_overdue_at_enqueue_ms=0, queue_to_tick_start_ms=14,
               tick_start_lateness_ms=4, within_tick_wait_ms=1, tick_anchor_reset=False)
    return row


class TimingTests(unittest.TestCase):
    def test_spread_keeps_per_player_rate(self):
        events = list(scheduled_events(12, 2, 16_666_667, 'spread'))
        self.assertEqual(len(events), 24)
        self.assertEqual(events[0][2], 0)
        self.assertLess(events[11][2], 16_666_667)
        for player in range(12):
            self.assertEqual(events[player + 12][2] - events[player][2], 16_666_667)
        self.assertEqual({e[2] for e in scheduled_events(12, 1, 100, 'burst')}, {0})

    def test_receive_timestamp_merge_and_duplicate(self):
        row = sample(1, 'unacked')
        event = dict(kind='202', received_ns='31000000', accepted='1',
                     state_superseded='0', queue_depth='2', tick='3', tick_anchor_reset='0')
        event.update({f'stage_{field}_us': '100' for field in STAGES})
        counters = defaultdict(int)
        merge_event(row, event, 0, STAGES, counters)
        self.assertEqual(row['rtt_ms'], 20)
        self.assertEqual(row['scheduled_latency_ms'], 21)
        self.assertEqual(row['input_processing_ms'], 0.1)
        merge_event(row, event, 0, STAGES, counters)
        self.assertEqual(counters['duplicate_acks'], 1)
        event['kind'] = '200'
        merge_event(row, event, 0, STAGES, counters)
        self.assertEqual(row['state_rtt_ms'], 20)
        row['sent_ms'] = None
        merge_event(row, event, 0, STAGES, counters)
        self.assertEqual(counters['unmatched'], 1)


class AggregationTests(unittest.TestCase):
    def test_losses_and_warmup_do_not_become_fast_samples(self):
        rows = [sample(1, latency=20), sample(2, latency=60), sample(3, 'unacked'),
                sample(4, 'missed_schedule'), sample(5, 'rejected'), sample(6, 'send_error')]
        rows[3]['sent_ms'] = None
        warmup = sample(0, latency=900)
        warmup['phase'] = 'warmup'
        result = summarize_records(rows + [warmup], 1)
        self.assertEqual(result['scheduled'], 6)
        self.assertEqual(result['sent'], 4)
        self.assertEqual(result['accepted'], 2)
        self.assertEqual(result['unacked_percent'], 25)
        self.assertEqual(result['scheduled_latency_ms']['mean'], 40)
        self.assertEqual(result['thresholds']['50']['percent_of_accepted'], 50)
        self.assertEqual(result['tick_execution_ms']['count'], 1)

    def test_superseded_state_is_not_an_unacked_input(self):
        row = sample(1)
        row['state_superseded'] = True
        result = summarize_records([row], 1)
        self.assertEqual(result['state_superseded'], 1)
        self.assertEqual(result['unacked'], 0)
        self.assertEqual(result['state_missing_not_superseded'], 0)

    def test_tick_phase_decomposes_queue_wait(self):
        rows = [sample(1), sample(2, tick=2)]
        rows[1].update(next_tick_remaining_ms=0, tick_overdue_at_enqueue_ms=2,
                       tick_start_lateness_ms=16, tick_anchor_reset=True)
        result = summarize_records(rows, 1)
        self.assertLess(result['queue_decomposition_error_ms']['max'], 0.000001)
        self.assertLess(result['phase_relation_error_ms']['max'], 0.000001)
        self.assertEqual(result['tick_anchor_resets'], 1)
        self.assertEqual(result['tick_start_lateness_ms']['count'], 2)
        self.assertEqual(result['phase_buckets'][0]['rtt_ms']['count'], 1)

    def test_all_unacked_produces_no_percentile(self):
        result = summarize_records([sample(1, 'unacked')], 1)
        self.assertIsNone(result['rtt_ms']['p99'])
        self.assertEqual(result['unacked_percent'], 100)

    def test_paired_bootstrap_uses_runs(self):
        runs = []
        for repetition in range(5):
            for label, latency in [('Debug', 20), ('Release', 15)]:
                stats = summarize_records([sample(1, latency=latency)], 1)
                runs.append({'rate': 60, 'repetition': repetition, 'label': label,
                             'summary': {'valid_for_comparison': True, 'overall': stats}})
        result = next(c for c in paired_comparison(runs) if c['metric'] == 'scheduled_latency_ms' and c['statistic'] == 'p99')
        self.assertEqual(result['pairs'], 5)
        self.assertEqual(result['mean_release_minus_debug_ms'], -5)
        self.assertEqual(result['bootstrap_95_ci_ms'], [-5, -5])


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
