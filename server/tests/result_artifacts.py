"""측정 시각(KST)을 버전으로 사용하는 결과 경로 공통 함수."""
from datetime import datetime, timedelta, timezone
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
