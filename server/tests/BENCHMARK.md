# 고정 전송률 벤치마크

`fixed_rate_benchmark.py`는 응답을 기다리지 않고 가상 플레이어의 UDP Move 입력을 정해진 주기로 전송합니다. TCP handshake, UDP 인증, heartbeat 응답, 서버 종료까지 실제 서버를 사용합니다. Python 표준 라이브러리만 필요하며 그래프 생성에는 matplotlib이 필요합니다.

## 빌드와 실행

먼저 Debug와 Release 서버를 빌드합니다. `main --build-info`의 실제 configuration 및 benchmark protocol v2을 확인하며, 잘못 지정한 빌드나 이전 서버 바이너리는 테스트가 거부합니다. 실행 파일의 경로는 플랫폼별 빌드 폴더에 맞게 지정하세요.

```bash
cmake --build server/build/macos-debug -j 4
cmake --build server/build/macos-release -j 4

python3 server/tests/fixed_rate_benchmark.py \
  --debug server/build/macos-debug/main \
  --release server/build/macos-release/main \
  --players 12 --rates 30 60 120 \
  --warmup 10 --duration 120 --repetitions 5
```

위 설정은 서버 30회 실행으로 약 66분이 필요합니다. 60 pps만 비교하면 약 22분입니다. 빠른 동작 확인은 `--rates 60 --warmup 1 --duration 3 --repetitions 1`로 수행할 수 있으나 성능 결론을 위한 측정으로 사용하지 않습니다.

단일 서버도 `--server <실행 파일>`로 실행할 수 있습니다. 선택 사항인 `--sample-resources`는 Unix에서 서버 RSS와 `ps`의 CPU 값을 1초 간격으로 수집합니다. CPU 값은 프로세스 수명 평균이며 순간 사용률이 아닙니다. Windows의 프로세스 자원 수집은 미지원입니다. 자원 수집 및 계측 비용도 있으므로 비교하는 실행에서는 동일한 옵션을 사용하세요.

### Windows (PowerShell)

Visual Studio의 **x64 Native Tools Command Prompt**에서 프로젝트 루트로 이동한 뒤 두 구성을 빌드합니다. `CMakePresets.json`의 Windows 설정은 `C:/vcpkg`를 사용합니다.

Windows preset은 `vcpkg-overlay-ports/asio`의 패치된 Asio를 설치합니다. Windows 10 1803 이상에서는 IOCP 타이머를 고해상도 waitable timer로 생성하며, 플래그가 지원되지 않는 구형 Windows에서는 일반 타이머를 사용합니다. 기존 `steady_timer`와 strand 구조를 유지하며 heartbeat 등 같은 context의 다른 타이머에도 적용됩니다. 자세한 적용 범위와 버전 관리 방법은 [overlay 설명](../vcpkg-overlay-ports/asio/README.md)을 참고하세요.

```bat
cd server
cmake --preset x64-debug
cmake --build build/x64-debug --parallel 4
cmake --preset x64-release
cmake --build build/x64-release --parallel 4
cd ..
```

PowerShell용 `run_fixed_rate_windows.ps1`은 스크립트 위치를 기준으로 두 `main.exe`를 찾아 기존 `fixed_rate_benchmark.py`를 호출합니다. Python 표준 라이브러리만 필요하며 기본 수신 모드는 Windows에서도 사용할 수 있는 `spawn` 프로세스입니다. 실행 위치에 관계없이 기본 결과는 `server/tests/latency_results`에 저장됩니다.

프로젝트 루트에서 짧은 동작 확인:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File server/tests/run_fixed_rate_windows.ps1 -Quick
```

`-Quick`은 전송률 60 pps, warmup 1초, 측정 3초, 반복 1회로 설정합니다. Debug/Release 각 1회를 실행하며 성능 결론에 사용할 설정은 아닙니다. 정식 측정은 기본값(30/60/120 pps, warmup 10초, 측정 120초, 반복 5회, 약 66분)을 사용합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File server/tests/run_fixed_rate_windows.ps1
```

PowerShell 세션에서 60 pps만 비교하거나 매개변수를 조절하려면:

```powershell
& ./server/tests/run_fixed_rate_windows.ps1 -Rates 60 -Warmup 10 -Duration 120 -Repetitions 5
# 여러 전송률은 PowerShell 배열로 지정합니다.
& ./server/tests/run_fixed_rate_windows.ps1 -Rates 30,60,120 -ReceiverMode process -PlayerTiming spread
```

필요하면 현재 세션에서만 `Set-ExecutionPolicy -Scope Process Bypass`를 적용한 뒤 직접 실행할 수 있습니다. `-Output`, `-Players`, `-Timeout`, `-PhaseOffsetsMs`, `-PythonExecutable`도 지정할 수 있습니다. Python 실행 파일 경로에 공백이 있으면 따옴표로 감싸세요. 실행 파일의 실제 Debug/Release 설정과 benchmark protocol v2 확인 및 종료 코드는 Python 벤치마크가 관리합니다. 종료 코드 1이면 오류 메시지와 결과의 `valid_for_comparison`을 확인하세요. Windows CPU/RSS 수집은 지원하지 않아 실행 파일에는 자원 수집 옵션을 제공하지 않습니다.

Windows에서 그래프를 만들려면:

```powershell
python -m pip install matplotlib
python server/tests/plot_fixed_rate.py --input server/tests/latency_results/<측정 버전 폴더>
```

## 전송 위상과 생성기 간섭 제어

기본 설정은 `--receiver-mode process --player-timing spread --phase-offsets-ms 0 4 8 12`입니다. 송신과 UDP 수신을 별도 Python 프로세스로 실행해 두 작업 사이의 GIL 경쟁을 줄입니다. 수신기는 디코딩 전에 `perf_counter_ns` 시각을 기록하고 버퍼링한 CSV에 저장합니다. 측정 종료 후 송신 기록과 병합하므로 입력마다 프로세스 간 메시지를 주고받지 않습니다. 같은 호스트의 monotonic clock을 사용합니다.

`spread`는 각 플레이어의 전송률을 유지하면서 한 주기 안에 입력을 균등하게 배치합니다. 12명 × 60 pps이면 플레이어별 주기는 약 16.667ms, 플레이어 사이 예정 간격은 약 1.389ms입니다. `burst`는 12명의 예정 시각을 동일하게 지정해 동시 입력 부하를 재현합니다. 두 부하는 서로 다른 실험이므로 같은 설정끼리 비교하세요. 분산 전송에서는 Python sleep의 정확도가 더 중요하므로 send lag와 missed schedule도 확인해야 합니다.

초기 지연은 반복마다 목록을 순환하며 같은 반복의 Debug/Release에 같은 값을 적용합니다. 기본 5회는 0, 4, 8, 12, 0ms입니다. 서버 tick에 직접 동기화하는 값은 아니므로 실제 도착 위상은 `next_tick_remaining_ms` 분포로 확인해야 합니다. 수신 프로세스 준비 시간과 OS 스케줄링 때문에 정확한 tick 위상을 보장하지 않습니다. 이 옵션은 반복 수나 측정 시간을 늘리지 않습니다.

이전 방식과 비교하는 대조 실험은 `--receiver-mode thread --player-timing burst --phase-offsets-ms 0`으로 실행합니다. 원인을 분리하려면 동일한 시간·전송률로 process/burst와 process/spread를 각각 실행하고, 초기 지연 설정도 고정해 비교하세요. 서버 tick 주기와 처리 로직은 변경하지 않습니다.

## 결과

`server/tests/latency_results/<측정 버전>/`에 저장합니다. `--output`으로 상위 경로를 바꿀 수 있습니다. 한국 시간(KST) 기준 `YYYYMMDD_HHMMSS_ffffff_KST` 버전 폴더를 새로 만들어 기존 결과를 덮어쓰지 않습니다.

- 실행별 `samples_<버전>.csv`: warmup을 포함한 입력별 예정/실제 전송 시각, ACK 및 상태 응답 지연, 서버 단계별 구간, queue depth, 상태 병합 여부
- 실행별 `summary_<버전>.json`: warmup 제외 통계, 실제 전송률, 33.3/50/100ms 초과 지표, 1초 구간별 p99, 플레이어별 통계, 빌드/시스템 환경
- 실행별 `receiver_events_<버전>.csv`: process 모드의 원본 수신 시각과 ACK 계측 값
- 실행별 `server_<버전>.log`: 서버 로그
- `suite_summary_<버전>.json`, `comparison_report_<버전>.html`: 모든 실행과 Debug/Release 실행 쌍 비교

```bash
python3 -m pip install matplotlib
python3 server/tests/plot_fixed_rate.py --input <측정 버전 폴더>
```

`comparison_charts_<버전>.png`를 생성하고 비교 HTML에 그래프를 추가합니다. 기본 `plot_latency.py`는 이전 closed-loop CSV 형식용이며 이 테스트 결과에는 `plot_fixed_rate.py`를 사용합니다.

## 측정 구간과 해석

테스트가 시작하는 서버 프로세스에만 `SERVER_BENCHMARK=1`, `SERVER_METRICS=0`을 설정합니다. 일반 실행에서는 ACK를 생성하지 않습니다. `BenchmarkSupport`, `ProcessInputPacket`, `FinishBenchmarkTick`에 벤치마크 로직을 분리했으며 기존 처리 흐름을 사용합니다. ACK는 tick 종료 후 입력별로 보내고, 상태 방송은 기존처럼 최신 상태를 보냅니다. Protobuf의 `benchmarkSequence`와 ACK 메시지는 추가 필드/타입입니다. 일반 게임 클라이언트를 갱신하지 않아도 테스트 모드가 꺼진 서버를 사용할 수 있습니다.

- `rtt_ms`: 실제 입력 송신 → 해당 입력의 처리 확인 ACK 수신
- `scheduled_latency_ms`: 예정 송신 → ACK 수신. 생성기의 전송 지연까지 포함하며 보고서의 기본 지연 지표입니다.
- `send_lag_ms`: 예정 송신 → 실제 송신
- `game_dispatch_ms`: Room에 입력을 제출 → game executor 실행, Protobuf 파싱 및 검증 후 World 큐에 등록
- `input_queue_ms`: World 큐 등록 → 해당 입력 처리 시작. tick 대기와 앞선 입력 처리 대기를 포함합니다.
- `input_processing_ms`: 입력 처리 함수 실행 시간
- `tick_execution_ms`: tick 시작 → 물리/상태 방송 처리 완료. 벤치마크 ACK 생성·송신 제출 비용은 제외합니다. tick 통계는 동일 tick의 ACK를 중복 집계하지 않습니다.
- `state_rtt_ms`: 실제 송신 → 자신의 해당 sequence를 가진 상태 응답 첫 수신. ACK 지연과 구별합니다.
- `state_superseded`: 같은 tick에 더 최신 입력이 처리되어 중간 입력 상태가 방송되지 않음. 입력 유실이 아닙니다.
- `state_missing_not_superseded`: ACK로 확인된 입력 중 서버 tick 병합으로 설명되지 않는 상태 미수신. 이후 outbound 병합 또는 UDP 유실 등은 이 값만으로 구분할 수 없습니다.
- `unacked`: 측정 종료 후 유예 시간까지 ACK가 오지 않은 입력. 입력 유실, 서버 rate limit/큐 제한, ACK 유실을 단정할 수 없습니다.
- `missed_schedule`: 생성기가 한 전송 주기 이상 늦어 보낼 수 없었던 입력. 밀린 입력을 한꺼번에 보내지 않고 누락된 부하를 명시합니다.

ACK 지연과 상태 응답 지연은 서로 다른 지표이며 기존 `latency_percentiles.py`의 결과와 직접 비교하면 안 됩니다. ACK가 추가하는 트래픽과 계측 비용을 포함하는 실험입니다. 거부된 입력과 미응답은 성공 응답 percentile에서 제외되므로 함께 확인해야 합니다. 1초 구간의 p99는 해당 구간 표본 수에 따라 불안정할 수 있습니다.

전송 주기를 놓친 입력이 1%를 초과하거나 send lag p95가 전송 간격 이상인 실행은 생성 부하를 확인하도록 표시하고 실행 쌍 통계에서 제외합니다. 종료 코드는 1입니다. 유효한 실행도 고정 전송률을 완벽하게 달성했다는 의미는 아닙니다.

현재 입력 제한은 `--build-info`와 각 summary에 기록합니다. `ConnectionInputRate=240`, `GlobalUdpRate=4096`을 넘긴 경우 rate limit 영향을 포함한 실험이 됩니다. 테스트는 이 제한을 수정하지 않습니다.

Debug→Release, Release→Debug 순서를 번갈아 실행합니다. 각 실행 쌍의 통계 차이(Release − Debug)와 실행 쌍 단위의 탐색적 bootstrap 95% 구간을 제공합니다. 5쌍 미만이면 구간을 제공하지 않습니다. CPU 주파수, 온도, 백그라운드 작업, 실행 순서 영향이 제거되는 것은 아닙니다. 반복별 통계와 원본을 함께 검토하세요.

빌드 및 다른 테스트를 측정과 동시에 실행하지 마세요. 현재 서버와 Python 클라이언트가 동일 호스트를 사용하므로 Python 생성기의 CPU 사용도 서버에 영향을 줍니다. 이 제한을 넘어 실제 네트워크 조건을 평가하려면 별도 호스트의 부하 생성기와 서버를 사용하는 테스트가 필요합니다.

## 집계 로직 검증

```bash
python3 -m unittest discover -s server/tests -p test_fixed_rate_benchmark.py
```

실제 서버의 ACK 활성/비활성, 정상 입력 및 잘못된 입력도 검증하려면:

```bash
BENCHMARK_SERVER=server/build/macos-release/main \
  python3 -m unittest discover -s server/tests -p test_fixed_rate_benchmark.py
```

## 버전 구조

모든 결과는 측정을 시작한 시각의 버전을 폴더·파일명·JSON의 `version` 필드에 함께 저장합니다. 반복 실행은 동일한 suite 버전을 사용하고 하위 폴더에서 회차·빌드·전송률을 구분합니다.

```text
latency_results/
  20261006_155835_302983_KST/
    suite_summary_20261006_155835_302983_KST.json
    comparison_report_20261006_155835_302983_KST.html
    comparison_charts_20261006_155835_302983_KST.png
    run01_debug_60pps/
      samples_20261006_155835_302983_KST.csv
      summary_20261006_155835_302983_KST.json
      server_20261006_155835_302983_KST.log
```

`latency_percentiles.py`도 같은 결과 루트 아래 버전 폴더를 만듭니다. `--output`은 파일을 직접 저장하는 폴더가 아니라 버전 폴더를 생성할 상위 경로입니다. 그래프 스크립트는 버전 폴더 또는 결과 루트를 받으며, 루트라면 해당 테스트 종류의 최신 버전을 선택합니다. 이전의 버전 없는 파일도 읽을 수 있습니다.

기존 데이터 정리에는 `migrate_latency_results.py`를 사용합니다. 기존 suite 폴더의 시각이나 환경에 저장된 측정 시각을 사용하고, 그 정보가 없으면 summary 파일의 수정 시각을 사용합니다. 버전 시각의 출처는 `migration_<버전>.json`에 기록합니다. CSV 측정 행을 변경하거나 기존 결과를 덮어쓰지 않습니다.

## Tick phase 계측 (benchmark protocol v2)

서버를 다시 빌드한 뒤 같은 명령으로 실행합니다. 구버전 서버는 새 측정 필드를 제공하지 않으므로 테스트가 거부합니다. 기존 결과에는 새 값을 역산해 채우지 않습니다.

- `next_tick_remaining_ms`: World 큐에 등록될 때 다음 tick 예정 시각까지 남은 시간. 이미 예정 시각이 지났으면 0.
- `tick_overdue_at_enqueue_ms`: 큐 등록 때 이미 tick 예정 시각을 초과한 시간. 예정 시각 전이면 0.
- `queue_to_tick_start_ms`: 큐 등록부터 실제 처리 tick 시작까지의 시간.
- `tick_start_lateness_ms`: 처리 tick의 원래 예정 시각보다 실제 시작이 늦어진 시간. 동일 tick은 중복 집계하지 않습니다. timer anchor가 리셋되어도 원래 예정 시각으로 계산합니다.
- `within_tick_wait_ms`: 실제 tick 시작부터 해당 입력 처리 시작까지의 시간. tick 준비 및 앞선 입력 처리 비용이 포함됩니다.
- `tick_anchor_reset`: 과도한 지연으로 timer anchor가 리셋된 tick 여부. summary의 `tick_anchor_resets`는 tick 단위 횟수입니다.

큐 대기 = `queue_to_tick_start_ms` + `within_tick_wait_ms`입니다.
실제 tick 시작까지 대기 = `next_tick_remaining_ms` + `tick_start_lateness_ms` − `tick_overdue_at_enqueue_ms`입니다.
마이크로초 정수 변환에 따른 작은 오차는 `queue_decomposition_error_ms`, `phase_relation_error_ms`에 기록합니다.

보고서에는 큐 대기 분해 표와 enqueue phase별 RTT 그래프를 추가합니다. JSON의 `phase_buckets`에는 남은 시간 0–4 / 4–8 / 8–12 / 12ms 이상 구간의 표본 수와 지연 통계가 있습니다. 이 비교는 도착 시점 차이를 살펴보기 위한 것이며 인과관계를 확정하는 지표는 아닙니다. 0–4ms 구간에는 이미 예정 시각을 넘긴 입력도 포함됩니다.
