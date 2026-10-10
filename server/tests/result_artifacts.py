"""측정 시각(KST)을 버전으로 사용하는 결과 경로 공통 함수."""
from datetime import datetime, timedelta, timezone
import html
from pathlib import Path
import re

KST = timezone(timedelta(hours=9))
VERSION_PATTERN = re.compile(r'^\d{8}_\d{6}_\d{6}_KST$')


def result_version(moment=None):
    moment = moment or datetime.now(timezone.utc)
    return moment.astimezone(KST).strftime('%Y%m%d_%H%M%S_%f_KST')


def version_for(directory):
    directory = Path(directory)
    for candidate in (directory, *directory.parents):
        if VERSION_PATTERN.fullmatch(candidate.name):
            return candidate.name
    return None


def create_result_directory(root):
    version = result_version()
    directory = Path(root).resolve() / version
    directory.mkdir(parents=True, exist_ok=False)
    return directory, version


def artifact_path(directory, filename):
    directory = Path(directory)
    version = version_for(directory)
    name = Path(filename)
    return directory / (f'{name.stem}_{version}{name.suffix}' if version else filename)


def find_artifact(directory, filename):
    directory = Path(directory)
    expected = artifact_path(directory, filename)
    if expected.is_file():
        return expected
    legacy = directory / filename
    if legacy.is_file():
        return legacy
    raise FileNotFoundError(f'결과 파일 없음: {expected}')


def resolve_result_directory(directory, filename):
    """버전 폴더 또는 결과 루트 허용. 루트 입력이면 해당 테스트의 최신 버전 선택."""
    directory = Path(directory).resolve()
    try:
        find_artifact(directory, filename)
        return directory
    except FileNotFoundError:
        pass
    candidates = sorted((p for p in directory.iterdir() if p.is_dir() and VERSION_PATTERN.fullmatch(p.name)), reverse=True)
    for candidate in candidates:
        try:
            find_artifact(candidate, filename)
            return candidate
        except FileNotFoundError:
            continue
    raise FileNotFoundError(f'{directory}에 {filename}을 포함하는 측정 버전이 없습니다.')


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
