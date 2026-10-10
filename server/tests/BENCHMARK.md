# 실제 서버 테스트 안내

2026-10-08 기준으로 독립 OS/Asio 진단과 단위 테스트를 정리했습니다. 종합 관측과 남은 과제는 [TEST_RESULTS.md](TEST_RESULTS.md)에 있습니다. 이전 결과 폴더와 저장된 HTML/PNG/CSV/JSON은 보존합니다.

## 유지하는 파일

| 파일 | 역할 |
|---|---|
| network_game_smoke.py | 실제 서버 TCP/UDP 연결과 게임 패킷 smoke 테스트 및 프로토콜 보조 함수 |
| heartbeat_smoke.py | 실제 서버 heartbeat 검증 |
| fixed_rate_benchmark.py | 실제 서버 고정 전송률 입력 → ACK 측정, Debug/Release 교차 비교 |
| test_fixed_rate_benchmark.py | 실제 서버 ACK 활성/비활성 및 정상/잘못된 입력 통합 검증 |
| run_fixed_rate_windows.ps1 | Windows 빌드/벤치마크 실행 보조 |
| benchmark_receiver.py | 고정 전송률 테스트의 수신 프로세스와 기록 병합 모듈 |
| result_artifacts.py | 결과 버전·경로와 HTML 환경 표 생성 모듈 |
| benchmark_environment.py | 빌드·시스템 환경 수집과 입력 검증 모듈 |

보조 모듈은 남은 실제 서버 테스트의 실행에 필요합니다. 독립 timer probe/러너, 추적 후처리 스크립트, 단위 테스트, 마이그레이션과 그래프 생성 스크립트는 제거했습니다. 고정 전송률 테스트 자체가 생성하는 HTML 비교 표는 유지합니다.

## macOS 실행

서버를 먼저 빌드하세요. benchmark protocol v3 및 실제 Debug/Release configuration을 실행 파일의 `--build-info`로 검증합니다.

```bash
cmake --build server/build/macos-debug -j 4
cmake --build server/build/macos-release -j 4

caffeinate -di python3 server/tests/fixed_rate_benchmark.py \
  --debug server/build/macos-debug/main \
  --release server/build/macos-release/main \
  --players 12 --rates 60 \
  --warmup 5 --duration 30 --repetitions 3 \
  --receiver-mode process --player-timing spread \
  --phase-offsets-ms 0 4 8
```

약 4분입니다. `--warmup 10 --duration 120 --repetitions 5`는 약 22분입니다. 서버 연결/종료와 결과 저장 시간은 추가됩니다. 성능 벤치마크는 fixed_rate_benchmark.py로 통합했습니다. 비교용 thread 수신, executor 추적, Unix 자원 샘플링 옵션은 제거했고 수신은 항상 별도 프로세스를 사용합니다. Windows 인자 호환을 위해 --receiver-mode process는 계속 허용합니다.

```bash
BENCHMARK_SERVER=server/build/macos-release/main \
  python3 -m unittest discover -s server/tests -p test_fixed_rate_benchmark.py
```

이 통합 검증은 실제 서버를 실행합니다. BENCHMARK_SERVER가 없으면 건너뜁니다. 다른 실제 서버 테스트의 옵션은 각 파일의 `--help`를 참고하세요.

## Windows

run_fixed_rate_windows.ps1 및 server/vcpkg-overlay-ports/asio를 유지합니다. PowerShell에서 다음과 같이 실행할 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File server/tests/run_fixed_rate_windows.ps1
```

세부 빌드·측정 옵션은 해당 스크립트의 param 블록을 확인하세요. Windows preset의 Asio overlay는 지원되는 Windows에서 IOCP high-resolution waitable timer를 사용하고, 구형 Windows에서는 일반 타이머로 fallback합니다. [overlay 설명](../vcpkg-overlay-ports/asio/README.md)을 참고하세요.

## 결과와 해석

결과는 latency_results/YYYYMMDD_HHMMSS_ffffff_KST/ 아래에 저장합니다. 각 문서 이름에도 같은 버전이 포함됩니다. 기존 버전을 덮어쓰지 않으며 측정 데이터는 Git에서 제외합니다. 고정 전송률 테스트는 suite_summary JSON, comparison_report HTML, 실행별 samples CSV / summary JSON / server log / process 모드 receiver_events CSV를 생성합니다.

- rtt_ms: 실제 송신 → 입력 처리 ACK 수신
- scheduled_latency_ms: 예정 송신 → ACK 수신. 생성기의 전송 지연 포함
- send_lag_ms: 예정 송신 → 실제 송신
- input_queue_ms: World 큐 등록 → 입력 처리 시작
- input_processing_ms: 입력 처리 함수 실행 시간
- tick_execution_ms: tick 시작 → 상태 방송 처리 완료. benchmark ACK 제출 비용 제외
- timer_handler_lateness_ms: tick 예정 → 타이머 완료 handler 진입. OS 깨우기와 reactor/io_context 대기 포함
- strand_dispatch_ms: timer 완료 handler → strand 진입
- tick_entry_ms: strand 진입 → Update 시작
- unacked: 실제 송신 후 유예 시간까지 ACK 미수신. 원인 자체는 특정하지 못함
- missed_schedule: 생성기가 한 전송 주기 이상 늦어 보내지 못한 입력. 서버 패킷 손실과 구별

성공 ACK만 percentile에 포함하므로 미응답·전송 누락을 함께 검토해야 합니다. 현재 비교 유효 조건은 예정 입력 누락 1% 이하, send lag p95가 플레이어 전송 주기 미만, 성공 ACK 존재입니다. 통과해도 배경 부하·열·전력·스케줄링 변동이 제거되는 것은 아닙니다.

계측은 테스트가 시작한 서버에서만 SERVER_BENCHMARK=1 / SERVER_METRICS=0으로 활성화합니다. 일반 실행은 benchmark ACK를 생성하지 않습니다. protocol v3은 timer 완료 handler를 먼저 관측하고 기존 strand에 전달하므로 이전 v2와 계측 경로가 다릅니다. 동일한 protocol과 옵션끼리 비교하세요. 이전 closed-loop 결과는 보존하지만 closed-loop 실행 스크립트는 제거했습니다. 이전 상태 응답 지연과 현재 open-loop ACK 지연은 서로 다른 지표입니다.
