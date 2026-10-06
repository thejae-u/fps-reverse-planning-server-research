"""버전 결과 저장 및 이전 형식 호환성 검증."""
from datetime import datetime, timezone
from pathlib import Path
import tempfile
import unittest

from result_artifacts import artifact_path, create_result_directory, find_artifact, resolve_result_directory, result_version


class VersionTests(unittest.TestCase):
    def test_kst_timestamp_and_nested_run_names(self):
        version = result_version(datetime(2026, 10, 6, 6, 58, 35, 302983, tzinfo=timezone.utc))
        self.assertEqual(version, '20261006_155835_302983_KST')
        path = artifact_path(Path('/tmp') / version / 'run01_debug_60pps', 'samples.csv')
        self.assertEqual(path.name, f'samples_{version}.csv')

    def test_new_versions_preserve_previous_data(self):
        with tempfile.TemporaryDirectory() as root:
            first, version = create_result_directory(root)
            artifact_path(first, 'samples.csv').write_text('original')
            second, _ = create_result_directory(root)
            self.assertNotEqual(first, second)
            self.assertEqual(find_artifact(first, 'samples.csv').read_text(), 'original')

    def test_latest_selection_filters_test_kind_and_legacy_reading(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            old = root / '20261006_150000_000000_KST'
            latest = root / '20261006_160000_000000_KST'
            old.mkdir()
            latest.mkdir()
            artifact_path(old, 'summary.json').write_text('{}')
            artifact_path(latest, 'suite_summary.json').write_text('{}')
            self.assertEqual(resolve_result_directory(root, 'summary.json'), old.resolve())
            self.assertEqual(resolve_result_directory(root, 'suite_summary.json'), latest.resolve())
            legacy = root / 'legacy'
            legacy.mkdir()
            (legacy / 'summary.json').write_text('{}')
            self.assertEqual(find_artifact(legacy, 'summary.json'), legacy / 'summary.json')


if __name__ == '__main__':
    unittest.main()
