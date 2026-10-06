"""기존 latency_results를 측정 시각 버전 폴더/파일명으로 정리합니다.

실행: python3 server/tests/migrate_latency_results.py
이미 버전이 지정된 결과는 건너뛰며 원본 데이터 행은 변경하지 않습니다.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil

from result_artifacts import VERSION_PATTERN, artifact_path, result_version

NAMES = ('samples.csv', 'summary.json', 'server.log', 'latency_charts.png', 'latency_report.html',
         'suite_summary.json', 'comparison_charts.png', 'comparison_report.html')


def timestamp(source, metadata):
    if metadata.get('runs') and source.name.endswith('Z'):
        try:
            return datetime.strptime(source.name, '%Y%m%dT%H%M%S%fZ').replace(tzinfo=timezone.utc), 'original_suite_directory'
        except ValueError:
            pass
    recorded = metadata.get('environment', {}).get('recorded_at_utc')
    if recorded:
        return datetime.fromisoformat(recorded), 'environment.recorded_at_utc'
    summary = source / ('suite_summary.json' if 'runs' in metadata else 'summary.json')
    return datetime.fromtimestamp(summary.stat().st_mtime, timezone.utc), 'summary_file_mtime'


def migrate(root):
    root = root.resolve()
    suites = list(root.rglob('suite_summary.json'))
    leaves = [p for p in root.rglob('summary.json') if not any(s.parent in p.parents for s in suites)]
    sources = sorted(suites + leaves, key=lambda p: len(p.parts), reverse=True)
    for summary in sources:
        source = summary.parent
        if any(VERSION_PATTERN.fullmatch(p.name) for p in (source, *source.parents)):
            continue
        metadata = json.loads(summary.read_text(encoding='utf-8'))
        moment, method = timestamp(source, metadata)
        version = result_version(moment)
        target = root / version
        if target.exists():
            raise FileExistsError(f'동일한 버전이 이미 존재합니다: {target}')
        original_path = str(source)
        if source == root:
            target.mkdir()
            for name in NAMES:
                path = root / name
                if path.is_file():
                    shutil.move(str(path), str(target / name))
        else:
            shutil.move(str(source), str(target))
        for path in list(target.rglob('*')):
            if path.is_file() and path.name in NAMES:
                path.rename(artifact_path(path.parent, path.name))
        kind = 'fixed_rate_suite' if 'runs' in metadata else 'closed_loop'
        for path in target.rglob(f'summary_{version}.json'):
            data = json.loads(path.read_text(encoding='utf-8'))
            data.update(version=version, test_kind='fixed_rate_run' if kind == 'fixed_rate_suite' else kind)
            path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        if kind == 'fixed_rate_suite':
            metadata.update(version=version, test_kind=kind)
            for run in metadata['runs']:
                run['summary'] = json.loads(artifact_path(target / run['directory'], 'summary.json').read_text(encoding='utf-8'))
            artifact_path(target, 'suite_summary.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        for path in target.rglob('*.html'):
            page = path.read_text(encoding='utf-8')
            for name in NAMES:
                page = page.replace(name, artifact_path(target, name).name)
            page = page.replace('</h1>', f'</h1><p>측정 버전: {version}</p>', 1)
            path.write_text(page, encoding='utf-8')
        artifact_path(target, 'migration.json').write_text(json.dumps({
            'version': version, 'original_directory': original_path, 'version_timestamp_source': method,
            'note': 'summary_file_mtime이면 원래 측정 시각이 없어 파일 수정 시각을 사용함.'
        }, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(f'{original_path} -> {target}')
    # 옮긴 후 비어 있는 이전 그룹 폴더만 정리합니다.
    for path in sorted(root.rglob('*'), key=lambda p: len(p.parts), reverse=True):
        if path.is_dir() and not VERSION_PATTERN.fullmatch(path.name) and not any(path.iterdir()):
            path.rmdir()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path(__file__).resolve().parent / 'latency_results')
    migrate(parser.parse_args().input)
