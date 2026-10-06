"""고정 전송률 UDP 벤치마크와 Debug/Release 교차 반복 실행.

단일 실행:
  python3 server/tests/fixed_rate_benchmark.py --server server/build/macos-release/main --rates 60 --duration 120
교차 반복:
  python3 server/tests/fixed_rate_benchmark.py --debug server/build/macos-debug/main --release server/build/macos-release/main --rates 30 60 120 --duration 120 --repetitions 5
서버를 먼저 빌드하세요. 테스트는 SERVER_BENCHMARK=1로 전용 ACK 측정을 활성화합니다.
Python 표준 라이브러리만 사용하며, 실행별 CSV/JSON/log와 비교 HTML을 저장합니다.
"""
import argparse
from collections import defaultdict
import csv
from datetime import datetime, timezone
import html
import json
import math
import multiprocessing
import os
from pathlib import Path
import random
import select
import socket
import struct
import subprocess
import threading
import time
import uuid

from benchmark_receiver import receive_worker, merge_events
from latency_percentiles import collect_environment, positive_int, positive_seconds
from result_artifacts import artifact_path, create_result_directory, version_for
from network_game_smoke import blob, decode, frame, integer, number, port, read_tcp


STAGES = {3: 'game_dispatch_ms', 4: 'input_queue_ms', 5: 'input_processing_ms', 6: 'tick_execution_ms',
          11: 'next_tick_remaining_ms', 12: 'tick_overdue_at_enqueue_ms',
          13: 'queue_to_tick_start_ms', 14: 'tick_start_lateness_ms', 15: 'within_tick_wait_ms'}
FIELDS = ('sequence', 'player', 'phase', 'scheduled_ms', 'sent_ms', 'send_lag_ms', 'status',
          'rtt_ms', 'scheduled_latency_ms', 'game_dispatch_ms', 'input_queue_ms',
          'input_processing_ms', 'tick_execution_ms', 'queue_depth', 'tick',
          'state_superseded', 'state_rtt_ms', 'next_tick_remaining_ms',
          'tick_overdue_at_enqueue_ms', 'queue_to_tick_start_ms', 'tick_start_lateness_ms',
          'within_tick_wait_ms', 'tick_anchor_reset', 'player_offset_ms')


def distribution(values):
    values = sorted(values)
    def percentile(p):
        return values[math.ceil(len(values) * p / 100) - 1] if values else None
    return {'count': len(values), 'mean': sum(values) / len(values) if values else None,
            'p50': percentile(50), 'p95': percentile(95), 'p99': percentile(99),
            'max': max(values) if values else None}


def summarize_records(records, duration):
    rows = [row for row in records if row['phase'] == 'measurement']
    sent = [row for row in rows if row['sent_ms'] is not None and row['status'] != 'send_error']
    success = [row for row in rows if row['status'] == 'accepted']
    stats = {'scheduled': len(rows), 'sent': len(sent), 'accepted': len(success),
             'rejected': sum(row['status'] == 'rejected' for row in rows),
             'unacked': sum(row['status'] == 'unacked' for row in rows),
             'missed_schedule': sum(row['status'] == 'missed_schedule' for row in rows),
             'send_errors': sum(row['status'] == 'send_error' for row in rows),
             'actual_packets_per_second': len(sent) / duration,
             'unacked_percent': 100 * sum(row['status'] == 'unacked' for row in rows) / len(sent) if sent else 0,
             'state_superseded': sum(row['state_superseded'] is True for row in success),
             'state_responses': sum(row['state_rtt_ms'] is not None for row in success),
             'state_missing_not_superseded': sum(row['state_rtt_ms'] is None and not row['state_superseded'] for row in success)}
    for name in ('rtt_ms', 'scheduled_latency_ms', 'send_lag_ms', *STAGES.values(), 'state_rtt_ms'):
        source = sent if name == 'send_lag_ms' else success
        stats[name] = distribution([row[name] for row in source if row[name] is not None])
    stats['queue_depth'] = distribution([row['queue_depth'] for row in success])
    stats['thresholds'] = {str(threshold): {
        'count': sum(row['scheduled_latency_ms'] > threshold for row in success),
        'percent_of_accepted': 100 * sum(row['scheduled_latency_ms'] > threshold for row in success) / len(success) if success else None}
        for threshold in (33.3, 50, 100)}
    # Tick duration repeats in each input ACK. Deduplicate by server tick before aggregation.
    ticks = {row['tick']: row for row in success}
    for metric in ('tick_execution_ms', 'tick_start_lateness_ms'):
        stats[metric] = distribution([row[metric] for row in ticks.values() if row[metric] is not None])
    stats['tick_anchor_resets'] = sum(row.get('tick_anchor_reset') is True for row in ticks.values())
    stats['phase_buckets'] = []
    for lower, upper in ((0, 4), (4, 8), (8, 12), (12, float('inf'))):
        group = [row for row in success if row.get('next_tick_remaining_ms') is not None
                 and lower <= row['next_tick_remaining_ms'] < upper]
        stats['phase_buckets'].append({'remaining_min_ms': lower,
                                      'remaining_max_ms': upper if math.isfinite(upper) else None,
                                      'rtt_ms': distribution([row['rtt_ms'] for row in group]),
                                      'input_queue_ms': distribution([row['input_queue_ms'] for row in group])})
    stats['queue_decomposition_error_ms'] = distribution([
        abs(row['input_queue_ms'] - row['queue_to_tick_start_ms'] - row['within_tick_wait_ms'])
        for row in success if row.get('queue_to_tick_start_ms') is not None and row.get('within_tick_wait_ms') is not None])
    stats['phase_relation_error_ms'] = distribution([
        abs(row['queue_to_tick_start_ms'] - row['next_tick_remaining_ms']
            - row['tick_start_lateness_ms'] + row['tick_overdue_at_enqueue_ms'])
        for row in success if all(row.get(key) is not None for key in
                                 ('queue_to_tick_start_ms', 'next_tick_remaining_ms', 'tick_start_lateness_ms', 'tick_overdue_at_enqueue_ms'))])
    stats['per_second'] = []
    for second in range(math.ceil(duration)):
        group = [row for row in rows if second <= row['scheduled_ms'] / 1000 < second + 1]
        ok = [row['scheduled_latency_ms'] for row in group if row['status'] == 'accepted']
        stats['per_second'].append({'second': second, 'scheduled': len(group),
                                    'unacked': sum(row['status'] == 'unacked' for row in group),
                                    **distribution(ok)})
    return stats


class TestServer:
    def __init__(self, executable, players, timeout, directory, benchmark=True):
        self.executable, self.players, self.timeout = executable, players, timeout
        self.tcp, self.udp, self.initial, self.readers = [], [], [], []
        self.errors, self.stop = [], threading.Event()
        self.match = str(uuid.uuid4())
        self.ids = [str(uuid.uuid4()) for _ in range(players)]
        self.tcp_port, self.udp_port = port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
        self.log = artifact_path(directory, 'server.log').open('w', encoding='utf-8')
        env = dict(os.environ, SERVER_BENCHMARK='1' if benchmark else '0', SERVER_METRICS='0')
        self.process = subprocess.Popen(
            [str(executable), '--match-id', self.match, '--auth-token', 'fixed-rate-local',
             '--tcp-port', str(self.tcp_port), '--udp-port', str(self.udp_port),
             '--players', ','.join(self.ids)], stdin=subprocess.PIPE, stdout=self.log,
            stderr=subprocess.STDOUT, text=True, env=env)

    def connect(self):
        for index, player in enumerate(self.ids):
            deadline = time.monotonic() + self.timeout
            while True:
                try:
                    client = socket.create_connection(('127.0.0.1', self.tcp_port), timeout=self.timeout)
                    break
                except OSError:
                    if self.process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f'서버 시작 실패: {self.log.name}를 확인하세요.')
                    time.sleep(0.02)
            self.tcp.append(client)
            client.sendall(frame(101, blob(1, player) + integer(2, index + 1)))
            packet = read_tcp(client)
            if packet[1][0] != 101 or decode(packet[2][0])[1][0].decode() != player:
                raise RuntimeError('TCP handshake 응답 오류')
            datagram = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp.append(datagram)
            datagram.bind(('127.0.0.1', 0))
            datagram.connect(('127.0.0.1', self.udp_port))
            datagram.send(frame(201, blob(1, player) + blob(2, self.match) + integer(3, 1)))
            deadline = time.monotonic() + self.timeout
            while True:
                datagram.settimeout(max(0.001, deadline - time.monotonic()))
                packet = decode(datagram.recv(65535)[2:])
                if packet[1][0] == 201 and decode(packet[2][0])[1][0].decode() == player:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError('UDP 인증 실패')
        for client in self.tcp:
            packet = read_tcp(client)
            info = decode(packet[2][0])
            if packet[1][0] != 101 or len(info.get(14, [])) != self.players:
                raise RuntimeError('게임 초기화 응답 오류')
            self.initial.append(info)
            client.settimeout(None)
            reader = threading.Thread(target=self.keep_alive, args=(client,), daemon=True)
            self.readers.append(reader)
            reader.start()
        for datagram in self.udp:
            datagram.setblocking(False)

    def keep_alive(self, client):
        try:
            while not self.stop.is_set():
                read_tcp(client)
        except (OSError, AssertionError) as error:
            if not self.stop.is_set():
                self.errors.append(str(error))

    def close(self):
        self.stop.set()
        try:
            if self.process.poll() is None:
                self.process.stdin.write('quit\n')
                self.process.stdin.flush()
                if self.process.wait(timeout=8) != 0:
                    raise RuntimeError('서버 종료 실패')
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            for client in self.tcp:
                try:
                    client.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            for client in self.tcp + self.udp:
                client.close()
            for reader in self.readers:
                reader.join(timeout=2)
            self.process.stdin.close()
            self.log.close()


def receive_packets(server, records, lock, stop, start_ns, counters):
    indices = {sock: index for index, sock in enumerate(server.udp)}
    try:
        while not stop.is_set():
            ready, _, _ = select.select(server.udp, [], [], 0.05)
            for sock in ready:
                wire = sock.recv(65535)
                received_ns = time.perf_counter_ns()
                if len(wire) < 2 or struct.unpack('!H', wire[:2])[0] != len(wire) - 2:
                    raise RuntimeError('UDP frame length 오류')
                packet = decode(wire[2:])
                kind = packet.get(1, [0])[0]
                if kind not in (200, 202):
                    continue
                payload = decode(packet[2][0])
                player = indices[sock]
                if payload.get(1, [b''])[0].decode() != server.ids[player]:
                    continue
                if kind == 200 and (payload.get(3, [0])[0] != 1 or payload.get(2, [b''])[0].decode() != server.match):
                    continue
                sequence = payload.get(2 if kind == 202 else 6, [0])[0]
                with lock:
                    row = records.get((player, sequence))
                    if row is None or row['sent_ms'] is None:
                        counters['unmatched'] += 1
                        continue
                    elapsed = (received_ns - start_ns) / 1e6
                    if kind == 200:
                        if row['state_rtt_ms'] is None:
                            row['state_rtt_ms'] = elapsed - row['sent_ms']
                        continue
                    if row['status'] in ('accepted', 'rejected'):
                        counters['duplicate_acks'] += 1
                        continue
                    row['status'] = 'accepted' if payload.get(7, [0])[0] else 'rejected'
                    row['rtt_ms'] = elapsed - row['sent_ms']
                    row['scheduled_latency_ms'] = elapsed - row['scheduled_ms']
                    for field, name in STAGES.items():
                        row[name] = payload.get(field, [0])[0] / 1000
                    row['queue_depth'] = payload.get(8, [0])[0]
                    row['tick'] = payload.get(9, [0])[0]
                    row['state_superseded'] = bool(payload.get(10, [0])[0])
                    row['tick_anchor_reset'] = bool(payload.get(16, [0])[0])
    except Exception as error:
        server.errors.append(f'UDP receiver: {error}')


def sample_resources(server, stop, samples):
    # Unix ps의 CPU 값은 프로세스 수명 평균이며 순간 CPU 사용률로 해석하지 않음.
    if os.name == 'nt':
        return
    while not stop.is_set():
        try:
            result = subprocess.check_output(['ps', '-p', str(server.process.pid), '-o', '%cpu=', '-o', 'rss='],
                                             text=True, timeout=2, stderr=subprocess.DEVNULL).split()
            if len(result) == 2:
                samples.append({'recorded_at_utc': datetime.now(timezone.utc).isoformat(),
                                'ps_lifetime_cpu_percent': float(result[0]), 'rss_bytes': int(result[1]) * 1024})
        except (OSError, subprocess.SubprocessError, ValueError):
            pass
        stop.wait(1)


def scheduled_events(players, rounds, interval_ns, player_timing):
    """각 플레이어의 전송률은 유지하며 한 주기 안에서 전송 시점만 분산합니다."""
    for round_index in range(rounds):
        for player in range(players):
            offset = round(interval_ns * player / players) if player_timing == 'spread' else 0
            yield round_index, player, round_index * interval_ns + offset, offset


def nonnegative_ms(value):
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise argparse.ArgumentTypeError('0 이상의 유한한 밀리초 값을 지정하세요.')
    return result


def run_once(executable, args, rate, directory, phase_offset_ms=0):
    directory.mkdir(parents=True)
    environment = collect_environment(executable)
    build = environment['build']
    if build.get('benchmark_protocol_version') != 2:
        raise RuntimeError(f'{executable}: benchmark protocol v2이 없습니다. 서버를 다시 빌드하세요.')
    environment['runtime'].update(server_benchmark='1', server_metrics_env='0')
    server = TestServer(executable, args.players, args.timeout, directory)
    records, lock, receiver_stop = {}, threading.Lock(), threading.Event()
    counters = defaultdict(int)
    reader = None
    resource_reader = None
    resource_samples = []
    resource_stop = threading.Event()
    receiver_connection = None
    try:
        server.connect()
        if args.sample_resources:
            resource_reader = threading.Thread(target=sample_resources, args=(server, resource_stop, resource_samples), daemon=True)
            resource_reader.start()
        if args.receiver_mode == 'process':
            context = multiprocessing.get_context('spawn')
            receiver_stop = context.Event()
            receiver_connection, child_connection = context.Pipe(duplex=True)
            events_path = artifact_path(directory, 'receiver_events.csv')
            reader = context.Process(target=receive_worker,
                                     args=(server.udp, server.ids, server.match, str(events_path),
                                           tuple(STAGES), receiver_stop, child_connection), name='UDP receiver')
            reader.start()
            child_connection.close()
            if not receiver_connection.poll(10):
                raise RuntimeError('수신 프로세스 준비 시간 초과')
            ready = receiver_connection.recv()
            if not ready.get('ready'):
                raise RuntimeError(f'수신 프로세스 준비 실패: {ready}')
        start_ns = time.perf_counter_ns() + 100_000_000 + round(phase_offset_ms * 1e6)
        interval_ns = round(1e9 / rate)
        warmup_rounds = math.ceil(args.warmup * rate)
        measured_rounds = math.ceil(args.duration * rate)
        measurement_start_ns = start_ns + warmup_rounds * interval_ns
        measured_duration = measured_rounds * interval_ns / 1e9
        if args.receiver_mode == 'thread':
            reader = threading.Thread(target=receive_packets,
                                      args=(server, records, lock, receiver_stop, measurement_start_ns, counters), daemon=True)
            reader.start()
        for round_index, player, relative_ns, player_offset in scheduled_events(
                args.players, warmup_rounds + measured_rounds, interval_ns, args.player_timing):
            if (server.process.poll() is not None or server.errors
                    or (args.receiver_mode == 'process' and not reader.is_alive())):
                raise RuntimeError(f'서버/수신 연결 오류: {server.errors}')
            scheduled_ns = start_ns + relative_ns
            phase = 'warmup' if round_index < warmup_rounds else 'measurement'
            sequence = round_index + 1
            x = server.initial[player].get(4, [0.0])[0] + (sequence % 100) * 0.001
            movement = (blob(1, server.ids[player]) + number(2, x)
                        + number(4, server.initial[player].get(6, [0.0])[0])
                        + number(5, 1.0 if sequence % 2 else -1.0))
            payload = (blob(1, server.ids[player]) + blob(2, server.match) + integer(3, 1)
                       + blob(4, movement) + integer(6, sequence))
            wire = frame(200, payload)
            remaining = (scheduled_ns - time.perf_counter_ns()) / 1e9
            if remaining > 0:
                time.sleep(remaining)
            missed = time.perf_counter_ns() - scheduled_ns >= interval_ns
            row = dict.fromkeys(FIELDS)
            row.update(sequence=sequence, player=player, phase=phase,
                       scheduled_ms=(scheduled_ns - measurement_start_ns) / 1e6,
                       player_offset_ms=player_offset / 1e6,
                       status='missed_schedule' if missed else 'unacked')
            # process 모드에서는 기록·lock을 수신기와 공유하지 않음. thread 모드는 비교용.
            with lock:
                if not missed:
                    sent_ns = time.perf_counter_ns()
                    row['sent_ms'] = (sent_ns - measurement_start_ns) / 1e6
                    row['send_lag_ms'] = (sent_ns - scheduled_ns) / 1e6
                records[player, sequence] = row
                if not missed:
                    try:
                        server.udp[player].send(wire)
                    except OSError:
                        row['status'] = 'send_error'
            if (player == args.players - 1 and phase == 'measurement'
                    and (round_index - warmup_rounds + 1) % rate == 0):
                seconds = (round_index - warmup_rounds + 1) / rate
                if int(seconds) % 10 == 0:
                    print(f'  {seconds:.0f}/{args.duration:g}초', flush=True)
        # 고정된 응답 수집 유예 시간. 종료 시 미확인 입력은 unacked로 남김.
        end_ns = measurement_start_ns + measured_rounds * interval_ns
        until = end_ns / 1e9 + args.timeout
        while time.perf_counter() < until:
            if (server.errors or server.process.poll() is not None
                    or (args.receiver_mode == 'process' and not reader.is_alive())):
                raise RuntimeError(f'수신 오류: {server.errors}')
            time.sleep(min(0.05, max(0, until - time.perf_counter())))
        receiver_stop.set()
        reader.join(timeout=10)
        if reader.is_alive() or server.errors:
            raise RuntimeError(f'수신기 종료 오류: {server.errors}')
        if args.receiver_mode == 'process':
            if reader.exitcode != 0 or not receiver_connection.poll(2):
                raise RuntimeError(f'수신 프로세스 실패: exitcode={reader.exitcode}')
            result = receiver_connection.recv()
            if not result.get('finished'):
                raise RuntimeError(f'수신 프로세스 실패: {result}')
            counters.update(result['counters'])
            merge_events(events_path, records, measurement_start_ns, STAGES, counters)
        resource_stop.set()
        if resource_reader:
            resource_reader.join(timeout=3)
        rows = list(records.values())
        stats = summarize_records(rows, measured_duration)
        validity = []
        missed_fraction = stats['missed_schedule'] / stats['scheduled'] if stats['scheduled'] else 1
        if missed_fraction > 0.01:
            validity.append('예정 입력의 1% 이상을 생성하지 못함')
        if stats['send_lag_ms']['p95'] is not None and stats['send_lag_ms']['p95'] >= 1000 / rate:
            validity.append('send lag p95가 입력 전송 간격 이상임')
        if not stats['accepted']:
            validity.append('처리 확인 응답 없음')
        limits = {'per_player_pps': build.get('connection_input_rate_limit'),
                  'global_pps': build.get('global_udp_rate_limit')}
        summary = {'version': version_for(directory), 'test_kind': 'fixed_rate_run', 'schema_version': 2, 'server': str(executable), 'environment': environment,
                   'players': args.players, 'rate_per_player': rate, 'offered_pps': rate * args.players,
                   'duration_seconds': measured_duration, 'warmup_seconds': warmup_rounds / rate,
                   'response_grace_seconds': args.timeout, 'workload': f'open_loop_{args.player_timing}_players',
                   'receiver_mode': args.receiver_mode, 'player_timing': args.player_timing,
                   'phase_offset_ms': phase_offset_ms,
                   'planned_player_spacing_ms': 1000 / rate / args.players if args.player_timing == 'spread' else 0,
                   'clock': 'same-host perf_counter_ns; receive timestamp captured before decode',
                   'percentile_method': 'nearest_rank', 'rate_limits': limits,
                   'valid_for_comparison': not validity, 'validity_notes': validity,
                   'overall': stats, 'per_player': {str(p): summarize_records([r for r in rows if r['player'] == p], measured_duration)
                                                  for p in range(args.players)},
                   'receiver_counters': dict(counters),
                   'resource_sampling': {'enabled': args.sample_resources,
                                         'note': 'Unix ps CPU는 수명 평균; warmup/응답 수집 포함. Windows는 미지원.',
                                         'samples': resource_samples}}
        with artifact_path(directory, 'samples.csv').open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        artifact_path(directory, 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        print(f"  accepted={stats['accepted']}/{stats['sent']}, unacked={stats['unacked']}, "
              f"p95={stats['scheduled_latency_ms']['p95']}, p99={stats['scheduled_latency_ms']['p99']}", flush=True)
        return summary
    finally:
        receiver_stop.set()
        if reader:
            reader.join(timeout=3)
            if args.receiver_mode == 'process' and reader.is_alive():
                reader.terminate()
                reader.join(timeout=3)
        if receiver_connection:
            receiver_connection.close()
        resource_stop.set()
        if resource_reader:
            resource_reader.join(timeout=3)
        server.close()


def paired_comparison(runs):
    # 실행을 통계 단위로 사용. 동일 tick의 여러 ACK를 독립 표본처럼 취급하지 않음.
    result = []
    for rate in sorted({run['rate'] for run in runs}):
        pairs = defaultdict(dict)
        for run in runs:
            if run['rate'] == rate and run['summary']['valid_for_comparison']:
                pairs[run['repetition']][run['label']] = run['summary']['overall']
        for metric in ('rtt_ms', 'scheduled_latency_ms', 'input_processing_ms', 'tick_execution_ms',
                       'next_tick_remaining_ms', 'queue_to_tick_start_ms', 'tick_start_lateness_ms', 'within_tick_wait_ms'):
            for percentile in ('mean', 'p95', 'p99'):
                differences = [pair['Release'][metric][percentile] - pair['Debug'][metric][percentile]
                               for pair in pairs.values() if 'Debug' in pair and 'Release' in pair
                               and pair['Release'][metric][percentile] is not None
                               and pair['Debug'][metric][percentile] is not None]
                if not differences:
                    continue
                low = high = None
                if len(differences) >= 5:
                    rng = random.Random(12345)
                    boot = sorted(sum(rng.choices(differences, k=len(differences))) / len(differences)
                                  for _ in range(5000))
                    low, high = boot[124], boot[4874]
                result.append({'rate': rate, 'metric': metric, 'statistic': percentile,
                               'pairs': len(differences), 'mean_release_minus_debug_ms': sum(differences) / len(differences),
                               'bootstrap_95_ci_ms': [low, high],
                               'note': '실행 쌍 단위의 탐색적 bootstrap; 5쌍 미만이면 CI 미제공'})
    return result


def save_suite(directory, runs, args):
    comparison = paired_comparison(runs)
    suite = {'version': version_for(directory), 'test_kind': 'fixed_rate_suite', 'runs': runs, 'comparisons': comparison,
             'settings': {'rates': args.rates, 'players': args.players, 'repetitions': args.repetitions,
                          'duration_seconds': args.duration, 'warmup_seconds': args.warmup,
                          'receiver_mode': args.receiver_mode, 'player_timing': args.player_timing,
                          'phase_offsets_ms': args.phase_offsets_ms}}
    artifact_path(directory, 'suite_summary.json').write_text(json.dumps(suite, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    def fmt(value):
        return 'N/A' if value is None else f'{value:.3f}'
    body = []
    for run in runs:
        s = run['summary']['overall']
        url = html.escape(run['directory'] + '/' + artifact_path(directory / run['directory'], 'summary.json').name, quote=True)
        body.append(f"<tr><td><a href='{url}'>{html.escape(run['label'])}</a></td><td>{run['rate']}</td>"
                    f"<td>{run['repetition']}</td><td>{fmt(run['summary'].get('phase_offset_ms'))}</td><td>{fmt(s['actual_packets_per_second'])}</td>"
                    + ''.join(f'<td>{fmt(s["scheduled_latency_ms"][key])}</td>' for key in ('mean', 'p95', 'p99', 'max'))
                    + f"<td>{s['unacked']}</td><td>{s['missed_schedule']}</td>"
                    + f"<td>{fmt(s['thresholds']['33.3']['percent_of_accepted'])}</td>"
                    + f"<td>{fmt(s['thresholds']['50']['percent_of_accepted'])}</td>"
                    + f"<td>{s['thresholds']['100']['count']}</td>"
                    + f"<td>{s['state_superseded']}</td>"
                    + f"<td>{fmt(s['input_processing_ms']['p99'])}</td><td>{fmt(s['tick_execution_ms']['p99'])}</td>"
                    + f"<td>{'유효' if run['summary']['valid_for_comparison'] else '생성 부하 확인 필요'}</td></tr>")
    compared = ''.join(f"<tr><td>{c['rate']}</td><td>{c['metric']}</td><td>{c['statistic']}</td><td>{c['pairs']}</td>"
                       f"<td>{fmt(c['mean_release_minus_debug_ms'])}</td>"
                       f"<td>{fmt(c['bootstrap_95_ci_ms'][0])} ~ {fmt(c['bootstrap_95_ci_ms'][1])}</td></tr>" for c in comparison)
    phase_rows = []
    for run in runs:
        stats = run['summary']['overall']
        phase_rows.append(f"<tr><td>{html.escape(run['label'])}</td><td>{run['repetition']}</td>"
                          + ''.join(f'<td>{fmt(stats[key]["mean"])}</td>' for key in
                                    ('next_tick_remaining_ms', 'tick_overdue_at_enqueue_ms', 'queue_to_tick_start_ms',
                                     'tick_start_lateness_ms', 'within_tick_wait_ms'))
                          + f"<td>{stats['tick_anchor_resets']}</td></tr>")
    from plot_latency import environment_html
    environments = ''.join(f"<h3>{html.escape(label)}</h3>{environment_html(next(r['summary'] for r in runs if r['label'] == label))}"
                           for label in dict.fromkeys(r['label'] for r in runs))
    report = artifact_path(directory, 'comparison_report.html')
    report.write_text(f'''<!doctype html><html lang="ko"><meta charset="utf-8"><title>고정 전송률 비교</title>
<style>body{{font:15px system-ui;padding:28px;background:#f1f5f9;color:#0f172a}}main{{max-width:1500px;margin:auto}}
table{{border-collapse:collapse;background:white;width:100%}}td,th{{padding:10px;border-bottom:1px solid #ddd;text-align:right}}
.scroll{{overflow:auto}}p{{line-height:1.7}}.environment td{{text-align:left;overflow-wrap:anywhere}}</style><main>
<h1>고정 전송률 UDP 벤치마크</h1><p>측정 버전: {html.escape(version_for(directory) or "미기록")}</p><p>응답을 기다리지 않고 입력을 전송합니다. 아래 지연은 예정 전송 시각부터 tick 종료 후 처리 확인 ACK 수신까지입니다.
상태 갱신 응답은 CSV의 state_rtt_ms에 별도로 기록합니다. ACK 미수신은 입력 유실·rate limit·큐 제한·ACK 유실 중 무엇인지 단정하지 않습니다.
ACK는 입력마다 추가되는 벤치마크 트래픽이므로 일반 게임 트래픽과 처리량이 다릅니다. 측정 기능을 동일하게 켠 실행끼리 비교하세요.</p>
<p>수신기: {args.receiver_mode} · 입력 전송: {args.player_timing} · 초기 지연 순환: {html.escape(str(args.phase_offsets_ms))} ms</p>
<p>플레이어 {args.players}명 · 워밍업 {args.warmup:g}초 · 측정 {args.duration:g}초 · 반복 {args.repetitions}회.
실행 순서는 Debug→Release / Release→Debug를 번갈아 사용합니다. 각 실행의 환경과 원본 데이터는 해당 하위 폴더의 버전이 포함된 summary JSON / samples CSV / server log에 있습니다.</p>
<h2>실행별 결과 (ms)</h2><div class="scroll"><table><tr><th>Build</th><th>Player pps</th><th>Run</th><th>Initial delay (ms)</th><th>Actual pps</th><th>Mean</th><th>p95</th><th>p99</th><th>Max</th><th>Unacked</th><th>Missed schedule</th><th>&gt;33.3ms %</th><th>&gt;50ms %</th><th>&gt;100ms count</th><th>State superseded</th><th>Process p99</th><th>Tick p99</th><th>Validity</th></tr>{''.join(body)}</table></div>
<h2>tick 타이밍과 큐 대기 분해 (평균 ms)</h2>
<p>큐 대기 = 큐 등록부터 tick 시작까지의 대기 + tick 시작 후 해당 입력 처리까지의 대기입니다.
예정 시각까지 남은 시간은 tick의 입력 도착 phase를 보여줍니다. 이미 예정 시각을 지난 경우 Remaining은 0이고 Overdue에 초과 시간을 기록합니다.
Tick lateness는 처리 tick의 원래 예정 시각 대비 실제 시작 지연이며 동일 tick은 중복 집계하지 않습니다.</p>
<div class="scroll"><table><tr><th>Build</th><th>Run</th><th>Next tick remaining</th><th>Overdue at enqueue</th><th>Queue to tick start</th><th>Tick lateness</th><th>Within tick wait</th><th>Anchor resets</th></tr>{''.join(phase_rows)}</table></div>
<h2>실행 쌍 비교</h2><p>차이는 Release − Debug이며 음수면 Release 지연이 낮습니다.
CI는 실행 쌍을 재표집한 탐색적 95% bootstrap 구간이며, 5쌍 미만이면 표시하지 않습니다.
미응답을 percentile 계산에서 제외하므로 Unacked와 생성하지 못한 부하도 함께 확인해야 합니다.</p>
<div class="scroll"><table><tr><th>Player pps</th><th>Metric</th><th>Statistic</th><th>Pairs</th><th>Difference ms</th><th>95% CI ms</th></tr>{compared}</table></div>
{environments}</main></html>''', encoding='utf-8')
    print(f'비교 보고서: {report.resolve()}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--server', type=Path, help='단일 서버 실행 파일')
    parser.add_argument('--debug', type=Path)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--players', type=int, choices=(10, 12), default=12)
    parser.add_argument('--rates', type=positive_int, nargs='+', default=[30, 60, 120])
    parser.add_argument('--duration', type=positive_seconds, default=120)
    parser.add_argument('--warmup', type=positive_seconds, default=10)
    parser.add_argument('--repetitions', type=positive_int, default=5)
    parser.add_argument('--timeout', type=positive_seconds, default=2)
    parser.add_argument('--receiver-mode', choices=('process', 'thread'), default='process',
                        help='기본 process: 송신/수신 GIL 분리; thread: 이전 방식 비교')
    parser.add_argument('--player-timing', choices=('spread', 'burst'), default='spread',
                        help='기본 spread: 플레이어별 전송 시점 분산; burst: 동시 전송')
    parser.add_argument('--phase-offsets-ms', type=nonnegative_ms, nargs='+', default=[0, 4, 8, 12],
                        help='반복마다 순환할 초기 전송 지연(ms). 각 Debug/Release 쌍에 동일 값 적용')
    parser.add_argument('--sample-resources', action='store_true', help='Unix 서버 CPU(수명 평균)/RSS를 1초 간격으로 수집')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent / 'latency_results')
    args = parser.parse_args()
    if bool(args.server) == bool(args.debug or args.release) or (not args.server and not (args.debug and args.release)):
        parser.error('--server 또는 --debug와 --release 쌍을 지정하세요.')
    if len(set(args.rates)) != len(args.rates):
        parser.error('--rates에 같은 전송률을 중복 지정할 수 없습니다.')
    binaries = [('Server', args.server)] if args.server else [('Debug', args.debug), ('Release', args.release)]
    binaries = [(label, path.resolve()) for label, path in binaries]
    for label, path in binaries:
        if not path.is_file():
            parser.error(f'실행 파일 없음: {path}')
        build = collect_environment(path)['build']
        if build.get('benchmark_protocol_version') != 2:
            parser.error(f'{path}를 다시 빌드하세요: benchmark protocol v2 필요')
        if label in ('Debug', 'Release') and build.get('configuration') != label:
            parser.error(f'{label}로 지정한 실행 파일의 실제 설정이 {build.get("configuration")}입니다.')
    directory, version = create_result_directory(args.output)
    runs = []
    for label, path in binaries:
        build = collect_environment(path)['build']
        for rate in args.rates:
            if rate > build.get('connection_input_rate_limit', math.inf) or rate * args.players > build.get('global_udp_rate_limit', math.inf):
                print(f'{label}: {rate} pps/player는 서버 rate limit을 초과합니다. 결과에 제한 정책의 영향이 포함됩니다.', flush=True)
    for repetition in range(1, args.repetitions + 1):
        for rate in args.rates:
            order = binaries if repetition % 2 else list(reversed(binaries))
            for label, executable in order:
                name = f'run{repetition:02d}_{label.lower()}_{rate}pps'
                print(f'{label}, {rate} pps/player, 반복 {repetition}/{args.repetitions}', flush=True)
                phase_offset_ms = args.phase_offsets_ms[(repetition - 1) % len(args.phase_offsets_ms)]
                summary = run_once(executable, args, rate, directory / name, phase_offset_ms)
                runs.append({'label': label, 'rate': rate, 'repetition': repetition,
                             'directory': name, 'summary': summary})
                save_suite(directory, runs, args)
    return 0 if all(r['summary']['valid_for_comparison'] for r in runs) else 1


if __name__ == '__main__':
    raise SystemExit(main())
