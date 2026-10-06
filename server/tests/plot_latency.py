"""지연 시간 테스트 데이터로 PNG 그래프와 외부 파일 없이 열 수 있는 HTML 보고서를 생성합니다.

필요 패키지 설치: python3 -m pip install matplotlib
실행: python3 server/tests/plot_latency.py --input server/tests/latency_results
입력 버전 폴더에 latency_charts_<버전>.png와 latency_report_<버전>.html을 저장합니다.
결과 루트를 지정하면 최신 closed-loop 측정 버전을 선택합니다.
"""
import argparse
import base64
import csv
import html
import json
import math
from pathlib import Path

from latency_percentiles import summarize
from result_artifacts import artifact_path, find_artifact, resolve_result_directory


def load_results(directory):
    metadata = json.loads(find_artifact(directory, 'summary.json').read_text(encoding='utf-8'))
    rows = []
    with find_artifact(directory, 'samples.csv').open(newline='', encoding='utf-8') as stream:
        for raw in csv.DictReader(stream):
            row = {'sample': int(raw['sample']), 'player': int(raw['player']),
                   'status': raw['status'], 'latency_ms': None}
            if row['status'] not in ('ok', 'timeout'):
                raise ValueError(f"알 수 없는 측정 상태: {row['status']}")
            if row['status'] == 'ok':
                value = float(raw['latency_ms'])
                if not math.isfinite(value) or value < 0:
                    raise ValueError('지연 시간은 0 이상의 유한한 값이어야 합니다.')
                row['latency_ms'] = value
            rows.append(row)
    if not rows:
        raise ValueError('samples.csv에 측정 데이터가 없습니다.')
    overall = summarize(rows)
    for key, value in overall.items():
        expected = metadata['overall'][key]
        if value is None or expected is None:
            matches = value is expected
        else:
            matches = math.isclose(value, expected, rel_tol=1e-9, abs_tol=1e-6)
        if not matches:
            raise ValueError(f'CSV와 JSON의 {key} 값이 다릅니다. 같은 실행에서 생성한 결과 파일을 사용하세요.')
    groups = {}
    for row in rows:
        groups.setdefault(row['player'], []).append(row)
    return metadata, rows, overall, {player: summarize(group) for player, group in sorted(groups.items())}


def format_value(value):
    return 'N/A' if value is None else f'{value:.3f}'


def environment_html(metadata):
    environment = metadata.get('environment')
    if not environment:
        return '<h2>측정 환경</h2><p>이 결과에는 측정 당시 환경 정보가 없습니다. 새 테스트를 실행하면 자동으로 기록됩니다.</p>'
    build = environment.get('build', {})
    system = environment.get('system', {})
    runtime = environment.get('runtime', {})
    memory = system.get('total_memory_bytes')
    entries = [
        ('측정 시작 (UTC)', environment.get('recorded_at_utc')),
        ('Build configuration', build.get('configuration')),
        ('빌드 정보 출처', build.get('source')),
        ('Compiler', ' '.join(str(build.get(key) or '') for key in ('compiler', 'compiler_version')).strip()),
        ('Compiler path', build.get('compiler_path')),
        ('CMake / Generator', ' / '.join(str(build.get(key) or 'Unknown') for key in ('cmake_version', 'generator'))),
        ('CMake CXX flags', build.get('cmake_cxx_flags')),
        ('C++ standard', build.get('cxx_standard')),
        ('Assertions enabled', build.get('assertions_enabled')),
        ('Build target OS / Architecture', ' / '.join(str(build.get(key) or 'Unknown') for key in ('target_os', 'target_arch'))),
        ('Executable SHA-256', build.get('executable_sha256')),
        ('OS', system.get('os')),
        ('Architecture', system.get('architecture')),
        ('CPU model', system.get('cpu_model')),
        ('Logical CPUs', system.get('logical_cpus')),
        ('Total RAM', f'{memory / 1024 ** 3:.2f} GiB' if memory is not None else None),
        ('Python', ' '.join(str(system.get(key) or '') for key in ('python_implementation', 'python_version')).strip()),
        ('측정 시작 시 Load average (1 / 5 / 15 min)', system.get('load_average_at_start')),
        ('Network workers / Game workers / Blocking workers',
         ' / '.join(str(build.get(key) if build.get(key) is not None else 'Unknown')
                    for key in ('network_workers', 'game_workers', 'blocking_workers'))),
        ('Network', runtime.get('network')),
        ('SERVER_METRICS', runtime.get('server_metrics_env')),
    ]
    rows = ''.join('<tr><th>' + html.escape(label) + '</th><td>'
                   + html.escape('Unknown (미기록)' if value is None or value == '' else str(value))
                   + '</td></tr>' for label, value in entries)
    return ('<h2>측정 환경</h2><p>테스트 시작 시 수집한 환경입니다. Build configuration은 실행 파일의 '
            '--build-info 응답으로 확인하며 폴더 이름으로 추정하지 않습니다. '
            'CMake CXX flags는 기본·구성별 설정이며 모든 target 옵션을 나열한 값은 아닙니다. '
            '측정 시 다른 프로세스의 부하나 전원 상태도 성능에 영향을 줄 수 있습니다.</p>'
            '<div class="scroll"><table class="environment"><tbody>' + rows + '</tbody></table></div>')


def render(directory):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        raise SystemExit('먼저 matplotlib을 설치하세요: python3 -m pip install matplotlib')
    directory = resolve_result_directory(directory, 'summary.json')
    metadata = json.loads(find_artifact(directory, 'summary.json').read_text(encoding='utf-8'))
    if metadata.get('test_kind') == 'fixed_rate_run':
        raise SystemExit('고정 전송률 결과는 plot_fixed_rate.py로 suite 폴더를 지정하세요.')
    metadata, rows, overall, players = load_results(directory)
    values = sorted(row['latency_ms'] for row in rows if row['status'] == 'ok')
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10})
    fig, axes = plt.subplots(2, 2, figsize=(14, 9), layout='constrained')
    fig.suptitle(f'UDP Move round-trip latency\nVersion: {metadata.get("version") or "Legacy"}', fontsize=20, fontweight='bold')
    colors = {'p50_ms': '#64748b', 'p95_ms': '#0891b2', 'p99_ms': '#ea580c'}
    ax = axes[0, 0]
    if values:
        ax.hist(values, bins=60, color='#2563eb', alpha=0.8)
        for metric in ('p95_ms', 'p99_ms'):
            ax.axvline(overall[metric], color=colors[metric], linestyle='--',
                       label=f'{metric[:3]}: {overall[metric]:.3f} ms')
        ax.legend()
    ax.set(title='Latency distribution (full range)', xlabel='Round-trip latency (ms)', ylabel='Successful samples')
    ax = axes[0, 1]
    if values:
        ax.plot(values, [100 * (i + 1) / len(values) for i in range(len(values))], color='#2563eb')
        for metric, percent in (('p95_ms', 95), ('p99_ms', 99)):
            ax.axhline(percent, color=colors[metric], linestyle=':', alpha=0.7)
            ax.scatter([overall[metric]], [percent], color=colors[metric],
                       label=f'{percent}% <= {overall[metric]:.3f} ms')
        ax.legend(loc='lower right')
    ax.set(title='Cumulative distribution (successful samples)', xlabel='Round-trip latency (ms)', ylabel='Cumulative samples (%)', ylim=(0, 101))
    ax = axes[1, 0]
    for player in players:
        group = sorted((row for row in rows if row['player'] == player and row['status'] == 'ok'),
                       key=lambda row: row['sample'])
        ax.plot([row['sample'] for row in group], [row['latency_ms'] for row in group],
                linewidth=0.6, alpha=0.55, label=f'P{player}')
    for metric in ('p95_ms', 'p99_ms'):
        if overall[metric] is not None:
            ax.axhline(overall[metric], color=colors[metric], linestyle='--', linewidth=1.5)
    ax.set(title='Latency by measurement round (full range)', xlabel='Round index (not elapsed time)', ylabel='Round-trip latency (ms)')
    ax = axes[1, 1]
    positions = list(range(len(players)))
    for offset, metric in ((-0.25, 'p50_ms'), (0, 'p95_ms'), (0.25, 'p99_ms')):
        ax.bar([x + offset for x in positions],
               [float('nan') if stats[metric] is None else stats[metric] for stats in players.values()],
               width=0.25, color=colors[metric], label=metric.split('_')[0])
    ax.set_xticks(positions, [f'P{player}' for player in players])
    ax.set(title='Percentiles by player', xlabel='Virtual player', ylabel='Round-trip latency (ms)')
    ax.legend()
    for ax in axes.flat:
        ax.grid(axis='y', alpha=0.2)
        ax.set_axisbelow(True)
    png = artifact_path(directory, 'latency_charts.png')
    fig.savefig(png, dpi=150)
    plt.close(fig)
    columns = [('sent', 'Sent'), ('received', 'Received'), ('timeouts', 'Timeouts'),
               ('timeout_percent', 'Timeout %'), ('mean_ms', 'Mean ms'), ('p50_ms', 'p50 ms'),
               ('p95_ms', 'p95 ms'), ('p99_ms', 'p99 ms'), ('max_ms', 'Max ms')]
    table_rows = []
    for label, stats in [('Overall', overall)] + [(f'Player {p}', s) for p, s in players.items()]:
        cells = [str(stats[key]) if key in ('sent', 'received', 'timeouts') else format_value(stats[key])
                 for key, _ in columns]
        table_rows.append('<tr><th>' + label + '</th>' + ''.join(f'<td>{cell}</td>' for cell in cells) + '</tr>')
    cards = ''.join(f'<div class="card"><span>{label}</span><strong>{format_value(overall[key])} ms</strong></div>'
                    for key, label in [('mean_ms', 'Mean'), ('p95_ms', 'p95'), ('p99_ms', 'p99')])
    encoded = base64.b64encode(png.read_bytes()).decode('ascii')
    report = artifact_path(directory, 'latency_report.html')
    environment_section = environment_html(metadata)
    report.write_text(f'''<!doctype html><html lang="ko"><meta charset="utf-8">
<title>UDP 지연 시간 보고서</title><style>
body{{font:15px system-ui,sans-serif;background:#f1f5f9;color:#0f172a;margin:0;padding:32px}}
main{{max-width:1200px;margin:auto}}h1{{margin-bottom:8px}}p{{line-height:1.6}}
.cards{{display:flex;gap:16px;flex-wrap:wrap}}.card{{background:white;padding:20px;border-radius:12px;min-width:150px}}
.card span{{display:block;color:#64748b}}.card strong{{font-size:28px}}img{{width:100%;margin-top:24px;border-radius:12px}}
.scroll{{overflow:auto}}table{{width:100%;border-collapse:collapse;background:white;font-variant-numeric:tabular-nums}}
th,td{{padding:12px;text-align:right;border-bottom:1px solid #e2e8f0}}th:first-child{{text-align:left}}
.environment td{{text-align:left;overflow-wrap:anywhere}}.environment th{{width:32%}}
</style><main><h1>UDP Move 왕복 지연 시간</h1><p>측정 버전: {html.escape(metadata.get("version") or "미기록")}</p>
<p>가상 플레이어 {metadata['players']}명 · 플레이어별 {metadata['rounds']}회 측정 ·
워밍업 {metadata['warmup_rounds']}회 제외 · 응답 {overall['received']}/{overall['sent']}건 ·
타임아웃 {overall['timeouts']}건 ({overall['timeout_percent']:.3f}%)</p>
<div class="cards">{cards}</div>
<p>Loopback에서 입력을 보낸 시점부터 해당 입력이 반영된 서버 상태 응답을 받는 시점까지 측정합니다.
측정값에는 network I/O, 서버 처리 시간, tick 대기가 포함됩니다.
Closed-loop 방식으로 플레이어마다 응답을 기다리는 입력을 하나씩 유지합니다.
Percentile은 성공한 응답만 대상으로 nearest rank 방식으로 계산하며, 타임아웃은 별도로 집계합니다.
이 결과는 one-way latency나 고정 전송률의 포화 부하를 나타내지 않습니다.
Debug/Release 빌드 설정에 따라 결과가 달라질 수 있습니다.</p>
<p>서버 실행 파일: {html.escape(metadata['server'])}<br>타임아웃: {metadata['timeout_seconds']}초</p>
{environment_section}
<img alt="지연 시간 분포, 누적 분포, 측정 회차별 지연 시간, 플레이어별 percentile 그래프" src="data:image/png;base64,{encoded}">
<h2>플레이어별 요약</h2><div class="scroll"><table><thead><tr><th>Scope</th>
{''.join(f'<th>{label}</th>' for _, label in columns)}</tr></thead><tbody>
{''.join(table_rows)}</tbody></table></div></main></html>''', encoding='utf-8')
    print(f"{'Scope':<12} {'Received':>10} {'Timeouts':>9} {'Mean ms':>10} {'p95 ms':>10} {'p99 ms':>10} {'Max ms':>10}")
    for label, stats in [('Overall', overall)] + [(f'Player {p}', s) for p, s in players.items()]:
        print(f"{label:<12} {stats['received']:>10} {stats['timeouts']:>9} "
              f"{format_value(stats['mean_ms']):>10} {format_value(stats['p95_ms']):>10} "
              f"{format_value(stats['p99_ms']):>10} {format_value(stats['max_ms']):>10}")
    print(f'PNG 그래프 저장 경로: {png.resolve()}\nHTML 보고서 저장 경로: {report.resolve()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                     add_help=False)
    parser.add_argument('-h', '--help', action='help', help='사용 안내를 출력하고 종료합니다.')
    parser.add_argument('--input', type=Path, default=Path(__file__).resolve().parent / 'latency_results',
                        help='버전 폴더 또는 결과 루트 (기본값: server/tests/latency_results, 최신 버전 선택)')
    render(parser.parse_args().input.resolve())
