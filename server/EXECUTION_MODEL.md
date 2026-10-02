# 서버 실행 모델

서버는 독립적인 두 ExecutionContext와 별도의 BlockingExecutor를 사용한다.
Game용 context는 Network worker와 스레드를 공유하지 않는다.

| 실행 도메인 | 담당 작업 |
| --- | --- |
| Network / Listener strand | accept, UDP 프레이밍·NetworkPacket 파싱, endpoint 등록, UDP 송신 |
| Network / 각 Session strand | TCP 읽기·쓰기, 연결 인증, 연결 종료 |
| Game / Room·World 공유 strand, worker 1개 | 참가자 상태, IngamePacket 파싱, RPC 생성, 물리·전투·tick |
| BlockingExecutor | HTTP 결과 보고. 기본 deadline 3초 |

strand는 스레드가 아니라 handler 간 직렬 실행을 보장하는 executor이다.
Game과 Network의 context 및 worker를 분리해 서로의 handler가 직접 실행 시간을 점유하지 않도록 한다.
다만 CPU 포화, OS 스케줄링, 메모리 경합까지 격리하는 실시간 보장은 아니다.

## 도메인 경계와 패킷

Room과 World는 Session 참조를 보관하지 않는다. Game의 참가자 목록은 UUID,
preset과 generation을 가진 Participant이고, Network의 연결 목록은 endpoint와 Session을 가진다.
generation은 송신 시각이 아니라 Session 생성 때 부여하는 연결 인스턴스 번호이다.
동일 UUID가 재접속해도 이전 연결의 지연 입력·송신 요청·퇴장 명령을 적용하지 않도록 비교한다.

Game은 내부 RPC 메시지와 IngamePacket을 직렬화해 immutable payload를 제출한다.
Network는 NetworkPacket 외부 envelope와 uint16 길이 헤더를 만든다.
protobuf schema와 기존 클라이언트 프레이밍은 변경하지 않는다.

Listener가 NetworkGateway를 구현한다. UDP broadcast는 요청 하나를 제출하고
Network에서 sendBuffer 하나를 생성한다. 모든 수신자가 같은 immutable buffer를 공유하지만,
실제 송신은 각 endpoint에 대한 개별 unicast datagram이다. IP broadcast/multicast가 아니다.
이 경로는 각 Session strand를 거치지 않는다. TCP는 각 Session의 write queue로 전달한다.

제출 큐는 drain 예약을 공유하며 기본 8개씩 처리 후 다음 handler에 실행 기회를 준다.
이동 상태는 StateKey{StateKind::Movement, 플레이어 UUID}로 식별한다.
정기 점수판은 StateKey{StateKind::Scoreboard, 방 UUID}로 식별한다.
전체 점수판 snapshot이므로 Network 제출 큐에서 이전 정기 갱신을 최신 값으로 교체한다.
점수판은 기존 TCP 전송을 유지하며 Session 송신 큐에 들어간 데이터는 교체하지 않는다.
매치 종료의 최종 점수판은 키 없이 제출해 병합 및 교체 가능한 상태 제거 대상에서 제외한다.
optional 키가 없으면 병합하지 않는다. 이는 UDP 전달 보장을 의미하지 않는다.
동일 키의 미처리 요청과 동일 endpoint·키의 대기 datagram은 최신 상태로 교체할 수 있다.
이미 송신 중인 datagram은 교체하지 않는다.

UDP는 한 번의 참가자 fan-out만큼 동시 송신을 허용한다. 정원 10/12명에서는 최대 10/12개이다.
소켓 작업 시작과 완료 처리는 Listener strand에 속한다.
동시 송신 상한은 재전송 횟수가 아니며 애플리케이션 재전송 기능을 추가하지 않는다.
UDP 대기 큐 초과 시 교체 가능한 이동 상태를 먼저 제거하고, 없으면 새 요청을 버린다.
TCP 송신 큐 초과 시 느린 연결을 종료한다.

## 제한값과 기본 설정

운영 제한·timeout·worker 수·측정 설정은 src/header/ServerPolicy.hpp의 inline constexpr로 관리한다.
네트워크 프레이밍 규격은 같은 파일의 NetworkFraming, 게임 기본값은 GameRules.hpp에서 관리한다.
0 초기화, 인덱스 증가 등 일반적인 수학·제어 연산은 상수화 대상에서 제외한다.
기본값은 안전 상한이지 성능 측정으로 산정한 처리 용량은 아니다.

- 최대 참가자 12명, 미인증 TCP 연결 24개, handshake 제한 10초.
- 입력 payload 최대 8 KiB, 전체 UDP 초당 4096개, 연결별 게임 입력 초당 240개.
- Network 제출 큐·Game 제출 큐·World 입력 큐 각각 1024개.
- Session TCP 처리 큐 64개, 송신 큐 256개, UDP 대기 큐 2048개.

정상 참가자가 10/12명이어도 입력 폭주, 미인증 연결, 느린 수신자로 자원이 소진될 수 있어
제한은 별도로 필요하다. endpoint·UUID·방 검사는 비인증 게임 입력과 다른 플레이어 조작을 거른다.
기존 allowlist handshake는 암호학적 인증을 대신하지 않으며 네트워크 대역폭 DoS까지 막지 못한다.

기존 값은 보존했다. 특히 handshake의 공격력 10과 실제 Player 공격력 50의 차이,
스폰 중심 인덱스 4.5는 기존 게임 규칙이므로 이번 상수화에서 임의로 변경하지 않았다.

## 객체 생성과 Pool

Room은 private SecretKey를 받는 생성자와 Create의 make_shared를 사용한다.
호출자는 key를 만들 수 없어 Create 경로가 강제되고 객체·제어 블록의 결합 할당을 활용한다.
이전 private 생성자와 shared_ptr(new Room)도 생성 경로는 제한했지만 결합 할당은 활용하지 못했다.

ObjectPool은 mutex로 컨테이너 접근을 보호한다. 기존 Treiber stack의 즉시 Node 삭제는
다른 Rent가 head->next를 읽는 동안 노드 수명을 끝내 UAF/ABA 문제를 만들 수 있다.
CAS 성공 여부만으로 메모리 회수 안전성이 확보되지는 않는다.
mutex 비용은 존재하지만 생성·Clear는 잠금 밖에서 수행하고 컨테이너 변경만 잠근다.
최대 보관 슬롯을 미리 reserve해 반환 시 잠금 안의 vector 재할당을 피한다.
lock-free 복원에는 검증된 hazard pointer/epoch 회수 등 별도 설계와 contention 측정이 필요하다.
현재 Pool이 기존 구현보다 빠르다는 벤치마크 결과는 없다.

## TCP heartbeat

TCP 인증 이후 Session의 Network strand에서 2초 주기로 ping 상태를 확인한다.
기존 PacketType::Ping의 data에 요청 식별값을 넣고, 클라이언트는 같은 타입·data로 pong을 응답한다.
동시에 하나의 요청만 대기하며 일치하는 응답만 인정한다. 이전·중복·잘못된 응답은 무시한다.
응답이 10초 동안 없으면 해당 Session을 Stop한다. 재접속 유예는 아직 구현하지 않는다.
timer 검사 주기와 executor 지연 때문에 실제 종료 시각은 deadline보다 늦을 수 있다.
TCP EOF·read/write 실패는 heartbeat와 별개로 즉시 기존 종료 경로를 따른다.
이 방식은 TCP 연결의 생존 확인이며 UDP 경로 정상 여부나 게임 main thread의 진행을 보장하지 않는다.
tcp_ping_rtt 측정에는 송신 큐 대기·서버/클라이언트 처리 지연도 포함된다.
유효 pong 수신 시 Network 제어 메시지 PingRtt(104)로 sequence와 관측 RTT(마이크로초)를 전달한다.
클라이언트 HUD는 이를 밀리초로 변환해 표시하며 10초 이상 새 측정이 없으면 미측정 상태로 표시한다.
Unity 클라이언트는 TCP receive loop에서 pong을 응답하며 모든 TCP write를 semaphore로 직렬화한다.
Session 종료 시 heartbeat timer도 취소하며 현재 Room의 기존 이탈 처리를 사용한다.

## 종료와 future의 의미

종료는 main lifecycle thread에서 다음 순서로 수행한다.

1. accept와 입력 수용을 중단한다.
2. Game 제출을 닫고 barrier에서 timer를 취소한 뒤 Game을 drain한다.
3. HTTP worker를 join한다.
4. Network 송신을 기본 2초까지 기다린다.
5. 소켓을 닫고 Network 취소 handler까지 drain한 뒤 Pool을 해제한다.

StopInput과 Stop은 strand에 작업을 제출하고 promise의 set_value를 future.get으로 기다린다.
StopInput의 반환은 입력 차단·acceptor close 호출 완료를 의미한다.
Stop의 반환은 소켓 close와 Session Stop 요청 완료이지 모든 취소 handler 완료가 아니다.
후속 context drain이 남은 handler를 마무리한다.
IsDrained는 strand에서 조회한 순간의 송신 큐 상태를 반환한다. true가 될 때까지 기다리는 함수가 아니다.
종료 코드가 반복 조회하며 제한 시간까지 기다린다.

같은 Listener strand에서 호출하면 직접 실행해 자기 handler를 기다리는 교착을 피한다.
다른 worker에서 동기 대기하는 일반 API로 사용해서는 안 된다.
worker가 없거나 context가 이미 정지한 뒤 post하고 기다려도 끝나지 않으므로
Stop/Drain 등 동기 lifecycle API는 context가 살아 있는 동안 main에서 호출한다.

## 측정과 검증

SERVER_METRICS=1이면 기본 256개 표본 단위로 마이크로초 p50/p95/p99와 최대 표본 큐 깊이를 기록한다.
기본은 비활성화이며 패킷별 로그는 남기지 않는다.
표본 수로 percentile 인덱스를 계산하므로 고정 배열 인덱스를 따로 유지하지 않는다.
대상은 game_to_network, network_serialization, game_to_udp_start, tick이다.
UDP 완료는 로컬 송신 완료이며 상대 수신 확인이 아니다.

Visual Studio x64 개발 환경에서 실행한다.

    cmake --build server/build/x64-debug --config Debug --clean-first
    python server/tests/network_game_smoke.py
    python server/tests/network_game_smoke.py --players 10 --finish match
    python server/tests/network_game_smoke.py --players 12 --finish stall
    python server/tests/heartbeat_smoke.py

테스트는 Python 외부 패키지 없이 loopback 클라이언트를 사용한다.
TCP 초기화, UDP hole punching, 팀 균형, 동일 broadcast datagram,
다른 플레이어 입력 거부, 측정 로그, 활성 연결 상태에서의 종료를 확인한다.
match/stall은 실제 전투 승리 후 최종 TCP EndGame과 결과 보고도 확인한다.
로컬 mock HTTP 서버는 18080 포트를 사용하며 다른 서비스가 점유하면 테스트를 거부한다.
Debug loopback 결과는 검증 자료이지 운영 latency 목표나 Pool 성능 벤치마크가 아니다.
