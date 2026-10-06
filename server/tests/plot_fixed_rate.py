"""고정 전송률 벤치마크의 비교 보고서에 그래프를 추가합니다.

설치: python3 -m pip install matplotlib
실행: python3 server/tests/plot_fixed_rate.py --input <suite_summary.json이 있는 폴더>
comparison_report_<버전>.html에 그래프를 포함하고 comparison_charts_<버전>.png를 저장합니다.
결과 루트를 지정하면 최신 fixed-rate 측정 버전을 선택합니다.
"""
import argparse
import base64
import json
from pathlib import Path
import statistics
from result_artifacts import artifact_path, find_artifact, resolve_result_directory


def render(directory):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        raise SystemExit('먼저 matplotlib을 설치하세요: python3 -m pip install matplotlib')
    directory = resolve_result_directory(directory, 'suite_summary.json')
    suite = json.loads(find_artifact(directory, 'suite_summary.json').read_text(encoding='utf-8'))
    runs = suite['runs']
    valid = [run for run in runs if run['summary']['valid_for_comparison']]
    labels = list(dict.fromkeys(run['label'] for run in runs))
    rates = sorted({run['rate'] for run in runs})
    fig, axes = plt.subplots(3, 2, figsize=(15, 15), layout='constrained')
    settings = suite.get("settings", {})
    conditions = f"Receiver: {settings.get('receiver_mode', 'thread (legacy)')} | Timing: {settings.get('player_timing', 'burst (legacy)')} "
    fig.suptitle(f'Fixed-rate UDP benchmark\nVersion: {suite.get("version") or "Legacy"}\n{conditions}', fontsize=16, fontweight='bold')
    colors = ['#2563eb', '#ea580c', '#16a34a']
    ax = axes[0, 0]
    for index, label in enumerate(labels):
        for metric, style in [('p95', '--'), ('p99', '-')]:
            xs, ys, low, high = [], [], [], []
            for rate in rates:
                values = [run['summary']['overall']['scheduled_latency_ms'][metric] for run in valid
                          if run['label'] == label and run['rate'] == rate]
                values = [value for value in values if value is not None]
                if values:
                    middle = statistics.median(values)
                    xs.append(rate)
                    ys.append(middle)
                    low.append(middle - min(values))
                    high.append(max(values) - middle)
            ax.errorbar(xs, ys, yerr=[low, high], marker='o', linestyle=style,
                        color=colors[index], label=f'{label} {metric}')
    ax.set(title='Run median and min/max of scheduled latency', xlabel='Input packets/s/player', ylabel='Latency (ms)')
    ax.legend()
    ax = axes[0, 1]
    groups = [(label, rate) for rate in rates for label in labels]
    width = 0.25
    for offset, metric, color in [(-width, 'input_queue_ms', '#64748b'),
                                  (0, 'input_processing_ms', '#0891b2'),
                                  (width, 'tick_execution_ms', '#ea580c')]:
        values = []
        for label, rate in groups:
            samples = [run['summary']['overall'][metric]['p99'] for run in valid
                       if run['label'] == label and run['rate'] == rate]
            samples = [value for value in samples if value is not None]
            values.append(statistics.median(samples) if samples else float('nan'))
        ax.bar([i + offset for i in range(len(groups))], values, width=width, color=color,
               label=metric.replace('_ms', ''))
    ax.set_xticks(range(len(groups)), [f'{label}\n{rate} pps' for label, rate in groups])
    ax.set(title='Median of server-stage p99 by run', ylabel='Duration (ms)')
    ax.legend()
    ax = axes[1, 0]
    rate = max(rates)
    for index, label in enumerate(labels):
        selected = next((run for run in reversed(valid) if run['label'] == label and run['rate'] == rate), None)
        if selected:
            seconds = selected['summary']['overall']['per_second']
            ax.plot([row['second'] for row in seconds],
                    [row['p99'] if row['p99'] is not None else float('nan') for row in seconds],
                    color=colors[index], marker='.', label=f'{label} run {selected["repetition"]}')
    ax.set(title=f'Scheduled latency p99 per 1-second window ({rate} pps/player)',
           xlabel='Measurement second (latest valid run per build)', ylabel='Latency (ms)')
    if ax.lines:
        ax.legend()
    ax = axes[1, 1]
    for offset, metric, color in [(-0.18, 'unacked', '#ea580c'), (0.18, 'missed_schedule', '#64748b')]:
        values = []
        for label, rate in groups:
            samples = []
            for run in runs:
                if run['label'] == label and run['rate'] == rate:
                    stats = run['summary']['overall']
                    denominator = stats['sent'] if metric == 'unacked' else stats['scheduled']
                    samples.append(100 * stats[metric] / denominator if denominator else 0)
            values.append(statistics.median(samples) if samples else 0)
        ax.bar([i + offset for i in range(len(groups))], values, width=0.36, color=color, label=metric)
    ax.set_xticks(range(len(groups)), [f'{label}\n{rate} pps' for label, rate in groups])
    ax.set(title='Median missing ACK / missed schedule percentages (all runs)', ylabel='Percent (%)')
    ax.legend()
    ax = axes[2, 0]
    names = ('next_tick_remaining_ms', 'queue_to_tick_start_ms', 'within_tick_wait_ms')
    for offset, metric, color in ((-0.25, names[0], '#64748b'), (0, names[1], '#0891b2'), (0.25, names[2], '#ea580c')):
        values = []
        for label, rate in groups:
            samples = [run['summary']['overall'].get(metric, {}).get('mean') for run in valid
                       if run['label'] == label and run['rate'] == rate]
            samples = [value for value in samples if value is not None]
            values.append(statistics.median(samples) if samples else float('nan'))
        ax.bar([i + offset for i in range(len(groups))], values, width=0.25, color=color,
               label=metric.replace('_ms', ''))
    ax.set_xticks(range(len(groups)), [f'{label}\n{rate} pps' for label, rate in groups])
    ax.set(title='Tick phase and queue components (median of run means)', ylabel='Duration (ms)')
    ax.legend()
    ax = axes[2, 1]
    buckets = ['0-4', '4-8', '8-12', '12+']
    for index, label in enumerate(labels):
        ys = []
        for bucket_index in range(4):
            samples = []
            for run in valid:
                if run['label'] != label or run['rate'] != rate:
                    continue
                phases = run['summary']['overall'].get('phase_buckets', [])
                if len(phases) == 4 and phases[bucket_index]['rtt_ms']['mean'] is not None:
                    samples.append(phases[bucket_index]['rtt_ms']['mean'])
            ys.append(statistics.median(samples) if samples else float('nan'))
        ax.plot(range(4), ys, marker='o', color=colors[index], label=label)
    ax.set_xticks(range(4), buckets)
    ax.set(title=f'RTT by enqueue phase ({rate} pps/player; run median means)',
           xlabel='Next tick remaining at enqueue (ms)', ylabel='Actual-send RTT (ms)')
    ax.legend()
    for ax in axes.flat:
        ax.grid(axis='y', alpha=0.2)
        ax.set_axisbelow(True)
    png = artifact_path(directory, 'comparison_charts.png')
    fig.savefig(png, dpi=150)
    plt.close(fig)
    encoded = base64.b64encode(png.read_bytes()).decode('ascii')
    marker_start, marker_end = '<!-- charts:start -->', '<!-- charts:end -->'
    report = find_artifact(directory, 'comparison_report.html')
    page = report.read_text(encoding='utf-8')
    if marker_start in page:
        start = page.index(marker_start)
        end = page.index(marker_end, start) + len(marker_end)
        page = page[:start] + page[end:]
    charts = (marker_start + '<h2>반복·부하별 비교 그래프</h2><p>오차 막대는 실행별 최소~최대이며 신뢰구간이 아닙니다. '
              '지연 그래프는 생성 부하가 유효한 실행만 사용하고, 미응답/전송 누락 그래프는 모든 실행을 포함합니다. tick phase별 RTT는 같은 도착 구간끼리 비교하는 탐색적 지표이며 각 구간 표본 수는 JSON에 있습니다. 구버전 결과에는 phase 데이터가 없습니다.</p>'
              f'<img style="width:100%" alt="부하별 지연, 서버 단계별 시간, 구간별 p99, 미응답 비율" src="data:image/png;base64,{encoded}">' + marker_end)
    page = page.replace('<h2>실행별 결과 (ms)</h2>', charts + '<h2>실행별 결과 (ms)</h2>')
    report.write_text(page, encoding='utf-8')
    print(f'그래프: {png.resolve()}\n비교 보고서: {report.resolve()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--input', type=Path, required=True, help='버전 폴더 또는 결과 루트 (최신 fixed-rate 버전 선택)')
    render(parser.parse_args().input.resolve())
