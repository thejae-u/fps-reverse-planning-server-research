"""송신 프로세스와 독립된 UDP 수신기. 수신 시각을 기록하고 측정 종료 후 병합합니다."""
import csv
import select
import struct
import time

from network_game_smoke import decode

BASE_EVENT_FIELDS = ('player', 'sequence', 'kind', 'received_ns', 'accepted',
                     'state_superseded', 'queue_depth', 'tick', 'tick_anchor_reset')


def receive_worker(sockets, ids, match, path, stage_fields, stop, connection):
    """spawn 진입점. 패킷마다 IPC하지 않고 버퍼링한 CSV에 수신 이벤트를 기록합니다."""
    counters = {'received_datagrams': 0, 'recorded_events': 0}
    try:
        indices = {sock: index for index, sock in enumerate(sockets)}
        encoded_ids = [value.encode() for value in ids]
        encoded_match = match.encode()
        fields = BASE_EVENT_FIELDS + tuple(f'stage_{field}_us' for field in stage_fields)
        with open(path, 'w', newline='', encoding='utf-8', buffering=1024 * 1024) as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            connection.send({'ready': True})
            while not stop.is_set():
                ready, _, _ = select.select(sockets, [], [], 0.05)
                for sock in ready:
                    wire = sock.recv(65535)
                    received_ns = time.perf_counter_ns()
                    counters['received_datagrams'] += 1
                    if len(wire) < 2 or struct.unpack('!H', wire[:2])[0] != len(wire) - 2:
                        raise RuntimeError('UDP frame length 오류')
                    packet = decode(wire[2:])
                    kind = packet.get(1, [0])[0]
                    if kind not in (200, 202):
                        continue
                    payload = decode(packet[2][0])
                    player = indices[sock]
                    if payload.get(1, [b''])[0] != encoded_ids[player]:
                        continue
                    if kind == 200 and (payload.get(3, [0])[0] != 1 or payload.get(2, [b''])[0] != encoded_match):
                        continue
                    sequence = payload.get(2 if kind == 202 else 6, [0])[0]
                    row = dict(player=player, sequence=sequence, kind=kind, received_ns=received_ns)
                    if kind == 202:
                        for name, field in (('accepted', 7), ('state_superseded', 10), ('queue_depth', 8),
                                            ('tick', 9), ('tick_anchor_reset', 16)):
                            row[name] = payload.get(field, [0])[0]
                        row.update({f'stage_{field}_us': payload.get(field, [0])[0] for field in stage_fields})
                    writer.writerow(row)
                    counters['recorded_events'] += 1
        connection.send({'finished': True, 'counters': counters})
    except Exception as error:
        connection.send({'error': f'UDP receiver: {error}'})
    finally:
        for sock in sockets:
            sock.close()
        connection.close()


def merge_event(row, event, epoch_ns, stages, counters):
    """별도 프로세스의 monotonic 수신 시각을 실제 송신 기록에 결합합니다."""
    if row is None or row['sent_ms'] is None or row['status'] == 'send_error':
        counters['unmatched'] += 1
        return
    elapsed_ms = (int(event['received_ns']) - epoch_ns) / 1e6
    rtt = elapsed_ms - row['sent_ms']
    if rtt < 0:
        raise RuntimeError('송수신 프로세스의 monotonic 시각 순서가 맞지 않습니다.')
    if int(event['kind']) == 200:
        if row['state_rtt_ms'] is None:
            row['state_rtt_ms'] = rtt
        return
    if row['status'] in ('accepted', 'rejected'):
        counters['duplicate_acks'] += 1
        return
    row['status'] = 'accepted' if int(event['accepted']) else 'rejected'
    row['rtt_ms'] = rtt
    row['scheduled_latency_ms'] = elapsed_ms - row['scheduled_ms']
    for field, name in stages.items():
        row[name] = int(event[f'stage_{field}_us']) / 1000
    row['queue_depth'] = int(event['queue_depth'])
    row['tick'] = int(event['tick'])
    row['state_superseded'] = bool(int(event['state_superseded']))
    row['tick_anchor_reset'] = bool(int(event['tick_anchor_reset']))


def merge_events(path, records, epoch_ns, stages, counters):
    with open(path, newline='', encoding='utf-8') as stream:
        for event in csv.DictReader(stream):
            key = int(event['player']), int(event['sequence'])
            merge_event(records.get(key), event, epoch_ns, stages, counters)
