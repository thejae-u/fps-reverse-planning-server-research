# 서버 지연 테스트 종합 결과

작성일: 2026-10-08 (KST). 추가 OS 진단은 여기서 중단하고 실제 서버 Python 테스트만 유지합니다. 본 문서는 지금까지의 결과와 남은 과제를 정리하며, macOS 지연의 원인을 확정하거나 수정 완료를 선언하는 문서는 아닙니다.

## 결론

Release는 평균 입력 처리와 tick 실행이 빠르지만, 실제 송수신 지연은 큐·tick 위상·OS/실행 대기·Python 생성기 영향을 함께 받습니다. 따라서 Release의 계산이 빠르다는 사실만으로 RTT가 항상 낮아야 하는 것은 아닙니다.

Windows에서는 일반 IOCP waitable timer의 해상도 문제가 확인되어 high-resolution waitable timer로 변경한 커밋 4dfe2d2가 적용됐습니다. 지원되지 않는 Windows에는 fallback을 유지합니다. 이 Windows 결론은 해당 커밋과 기존 Windows 검증 기록에 근거하며, macOS에서 Windows를 다시 실행한 결과는 아닙니다.

macOS에서는 지연 대부분이 timer 완료 handler 진입 전에 발생했습니다. 긴 Room 작업이나 strand 대기가 주원인이라는 근거는 약합니다. 독립 Asio와 raw kqueue에서도 지연이 발생했고, 유휴 조건에 CPU 활동을 추가하면 긴 지연이 줄어드는 경향이 있었습니다. 이는 유휴 깨우기·전력 정책 영향과 일관되지만 특정 CPU 절전 상태나 timer coalescing을 직접 입증하지는 않습니다.

## 측정 방식

실제 서버에서 TCP handshake, UDP 인증, heartbeat와 가상 플레이어 Move 송수신을 수행했습니다. 고정 전송률 테스트는 ACK를 기다리지 않고 입력을 생성하며, 송신과 수신 Python 프로세스를 분리하고 플레이어별 전송 시점을 분산했습니다. Debug/Release 순서를 반복별로 교대하고 초기 지연을 기록했습니다.

서버 benchmark protocol v3은 다음 분해를 제공합니다.

`tick_start_lateness ≈ timer_handler_lateness + strand_dispatch + tick_entry`

실제 서버에서 마이크로초 정수 변환에 따른 최대 2µs 오차 내 일치를 검증했습니다. Timer handler 지연에는 OS 깨우기뿐 아니라 Asio reactor/game io_context 실행 대기도 포함됩니다. game worker가 하나이므로 strand 대기가 작다고 전체 executor 대기가 작다는 뜻은 아닙니다.

아래 표는 별도 명시가 없으면 실행별 통계의 평균이며, 전체 표본을 합쳐 계산한 percentile이 아닙니다. 단위는 ms입니다.

## 실제 서버: 22분 protocol v3 결과

[버전 20261007_140214_207061_KST](latency_results/20261007_140214_207061_KST/comparison_report_20261007_140214_207061_KST.html). 플레이어 12명 × 60 pps, warmup 10초, 측정 120초 × 빌드별 5회입니다.

| 지표 | Debug | Release |
|---|---:|---:|
| Timer handler 지연 p99 | 0.221 | 4.079 |
| Strand dispatch p99 | 0.0038 | 0.0302 |
| Tick 시작 지연 p99 | 0.237 | 4.115 |
| 입력 처리 평균 | 0.00315 | 0.00147 |
| Tick 실행 평균 | 0.158 | 0.094 |
| 실제 송신 → ACK 평균 | 13.645 | 14.392 |
| 예정 입력 생성 누락 | 294개 | 724개 |
| ACK 미수신 | 0개 | 0개 |

10회 모두 현재 비교 유효 조건을 통과했습니다. Release에서 1ms 이상 늦어진 1,410개 tick 중 1,397개는 timer handler 지연도 1ms 이상이었습니다. Debug 일부 실행에는 큰 RTT 이상 구간이 있어 RTT p99의 빌드 우열은 안정적으로 확정하지 못했습니다.

## 실제 서버: 4분 Room 작업 추적

[버전 20261008_004816_162355_KST](latency_results/20261008_004816_162355_KST/executor_report_20261008_004816_162355_KST.html). warmup 5초, 측정 30초 × 빌드별 3회입니다.

| 지표 | Debug | Release |
|---|---:|---:|
| Timer handler 지연 p99 | 0.330 | 4.667 |
| Strand dispatch p99 | 0.003 | 0.034 |
| 입력 처리 평균 | 0.00286 | 0.00164 |
| 실제 송신 → ACK 평균 | 13.341 | 14.513 |
| 생성 누락 | 32개 | 99개 |
| ACK 미수신 | 0개 | 0개 |
| 1ms 이상 지연 tick (추적 전체) | 465회 | 796회 |
| 긴 Room 작업과 시간 구간이 겹친 tick | 1회 | 3회 |

추적 기록은 warmup/종료를 포함하고, 1ms 이상 대기하거나 실행된 Room 작업/tick만 기록합니다. 기록 한도 초과는 없었습니다. Release 입력 작업이 37.486ms 기다린 뒤 0.051ms에 실행된 사례가 있었습니다. 긴 작업과 시간 구간이 겹친 사례가 적어 단일 긴 Room 작업을 주원인으로 보기 어렵지만, Asio 내부 callback과 많은 짧은 작업의 누적은 측정 범위 밖입니다. wall-clock 실행 시간에는 OS에서 실행이 중단된 시간도 포함됩니다.

## 독립 진단: OS 스케줄링·Asio 비교

[버전 20261008_151002_771883_KST](latency_results/20261008_151002_771883_KST/timer_report_20261008_151002_771883_KST.html). Asio / raw kqueue / 별도 producer→post 경로, default / user_initiated QoS, 실제 패킷 부하 유무, Debug/Release를 비교했습니다. 조건별 3초 × 2회, 총 48개 조건입니다.

높은 QoS가 지연을 일관되게 낮추지는 않았으며 raw kqueue에서도 긴 지연이 발생했습니다. 부하 중 handoff의 post 전달 p99는 실행별 평균 약 0.054ms였고 생산자 자체의 깨우기 지연은 더 컸습니다. Asio에만 발생하는 문제라는 근거는 약합니다. QoS는 스케줄링 외에 전력·timer latency도 바꿀 수 있어 결과를 순수 스케줄러 효과로 해석하지 않습니다. 서버/Python의 QoS는 변경하지 않았습니다.

24개 패킷 부하 실행은 모두 유효했고 ACK 미수신은 0개였습니다. 독립 probe의 결과로 실제 서버 game worker의 스케줄링 원인을 직접 확정할 수는 없습니다.

## 독립 진단: CPU 활동 대조

[버전 20261008_152400_703175_KST](latency_results/20261008_152400_703175_KST/timer_report_20261008_152400_703175_KST.html). 동일한 default QoS에서 CPU 활동 없는 조건과 별도 스레드가 계속 CPU를 사용하는 조건을 비교했습니다. 조건별 3초 × 2회, 총 32개 조건입니다. 아래는 Debug/Release를 합친 실행별 p99 평균입니다.

| 조건 | CPU 활동 없음 | CPU 활동 유지 |
|---|---:|---:|
| 부하 없음 — Asio | 3.172 | 1.597 |
| 부하 없음 — raw kqueue | 4.249 | 1.052 |
| 패킷 부하 중 — Asio | 1.214 | 1.056 |
| 패킷 부하 중 — raw kqueue | 1.043 | 1.054 |

유휴 조건의 긴 지연은 줄었고, 이미 패킷 부하가 있으면 추가 CPU 활동 효과는 작았습니다. 최종 16개 부하 실행 모두 유효하고 ACK 미수신은 없었습니다. 최초 시도에서는 CPU 활동 조건의 생성 누락이 1%를 넘어서 중단됐으며, 그 부분 기록도 별도 버전 20261008_152127_285003_KST에 보존했습니다.

지속 CPU 활동은 코어 배치·주파수·열·자원 경쟁도 바꿉니다. 실제 CPU 절전 상태를 측정하지 않았으므로 깨우기/전력 정책의 영향이라는 가설을 지지하는 수준입니다. busy-spin을 운영 서버 개선책으로 채택하지 않았습니다.

## 한계와 남은 과제

1. **macOS 원인 확정:** OS 스케줄링 추적으로 game worker가 실행 가능한 상태였는지, CPU 깨우기나 선행 작업 때문에 늦었는지 구분해야 합니다. 현재는 timer 완료 handler 이전 대기가 섞여 있습니다. 독립 probe만으로 대체할 수 없습니다.
2. **게임 executor의 미측정 작업:** Room 외 callback과 짧은 작업의 누적을 조사할 필요가 있습니다. 현재 추적 범위만으로 배제하지 못합니다.
3. **환경 통제와 재현성:** 전원·저전력 모드·화면 상태·열·백그라운드 부하를 통제한 실제 서버 반복 측정이 필요합니다. 3초 독립 진단은 탐색용입니다.
4. **생성기 영향 분리:** 같은 호스트의 Python 송수신 부하와 서버의 경쟁이 남아 있습니다. 별도 호스트 생성기로 재측정하고 누락/전송 지연을 함께 확인해야 합니다.
5. **Windows 재검증:** 수정 적용 바이너리와 이전 조건을 동일한 protocol·트래픽·환경으로 장시간 비교하고, Windows 원본 데이터와 환경을 함께 보관해야 합니다.
6. **성능 지표 정리:** 실제 RTT, 예정 시각 기준 latency, 서버 처리, 큐 대기, 미응답과 생성 누락을 분리해서 평가해야 합니다. 서로 다른 workload/protocol 결과를 섞지 않습니다.
7. **개선안 선택:** 원인 확인 후에만 QoS, timer 경로, executor 구조 변경 등을 검토합니다. 현재 macOS 수정안을 확정하지 않았습니다.

추가 테스트는 자동으로 진행하지 않습니다. 필요한 과제와 비교 조건을 선택한 뒤 범위를 정해 수행합니다.

## 코드 정리와 보존

실제 서버 Python 테스트와 Windows용 실행 파일 및 overlay를 유지합니다. 테스트에 필수인 수신/프로토콜/결과 경로/환경 표 보조 모듈도 유지합니다. test_fixed_rate_benchmark.py는 실제 서버 통합 검증만 남겼습니다.

독립 timer_probe.cpp / timer_diagnostics.py, 추적 후처리 analyze_executor_trace.py, 단위 테스트, 마이그레이션 및 그래프 생성 스크립트는 제거했습니다. 저장된 결과와 HTML/PNG는 삭제하지 않았습니다. 선택적 서버 계측 코드는 남아 있으며 일반 실행에서는 비활성화됩니다. 유지 파일과 실행법은 [BENCHMARK.md](BENCHMARK.md)를 참고하세요.

후속 정리에서 성능 실행기를 fixed_rate_benchmark.py로 통합했습니다. latency_percentiles.py를 제거하고 환경 수집만 benchmark_environment.py로 분리했습니다. 비교용 thread 수신과 trace-executor/sample-resources 옵션을 제거했습니다. 기능 smoke 및 실제 서버 ACK 통합 검증은 유지하며, 과거 결과는 변경하지 않았습니다. Windows PowerShell의 ReceiverMode는 process만 허용하도록 맞췄습니다.
