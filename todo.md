# Internal Connection & Server Core Core Implementation Status

현재 C# AuthServer와 C++ Logic Server 간의 내부 통신 연동 완료 후, 고성능 FPS 게임 서버 코어(World 및 물리, 지연 보상)를 완성하기 위한 잔여 작업 목록입니다.

## 1. 향후 잔여 작업 (Remaining To-Do)

### A. World 및 물리 틱(Physics Tick) 연산 보완
- [ ] **[물리 버그] 위치 업데이트 공식 수정 (dt 누락 보완)**:
  - `World::UpdateState()` 내에서 `player->position += player->velocity;`로 연산되어 속도가 60배 뻥튀기되는 문제를 `player->position += player->velocity * dt;`로 수정.
- [ ] **[물리 및 지면 충돌] 점프 물리 가속도 유도 및 지면 착지(Ground Check, y <= 0) 구현**:
  - `Jump()` 시 `velocity.y = JUMP_SPEED; player->isGrounded = false;`로 수직 속도를 인가하고, `UpdateState()`에서 중력 감속을 적용하며, `position.y <= 0.0f`가 되면 지면에 착지하도록 `y = 0.0f`, `velocity.y = 0.0f`, `isGrounded = true`로 복원.
- [ ] **[인풋 연동] `ProcessQueue`에서의 Jump 및 Shoot 실구현 연동**:
  - 클라이언트 입력 패킷 수신 시, 단순히 로그만 찍던 분기문을 확장하여 실제 `Jump()`, `Shoot()` 함수를 정상 호출하도록 매핑.

### B. 지연 보상을 고려한 서버 사이드 되감기 (Server-side Rewind / Lag Compensation) 시스템
- [ ] **[스냅샷 히스토리 버퍼] 플레이어 과거 위치 히스토리 수집**:
  - `Player` 구조체 내에 최근 N틱(예: 60틱 = 1초) 분량의 위치 및 시간 정보를 저장하는 `std::deque<PlayerPositionSnapshot>` 버퍼 구현.
- [ ] **[발사 패킷 롤백] 과거 시점으로 플레이어 되감기(Rewind) 처리**:
  - `Shoot` 패킷에 클라이언트가 격발한 시점의 `clientTick` 정보를 포함하여 서버로 전송.
  - 서버는 발사자 외의 타 플레이어들의 위치를 해당 `clientTick` 당시의 과거 위치로 일시적으로 변경.
- [ ] **[레이캐스트 판정 및 복원] 사격 충돌 판정 및 현재 위치 복구(Restore)**:
  - 되감긴 상태에서 발사자 위치와 사격 방향 벡터(Ray)가 다른 플레이어들의 히트박스(AABB 또는 Sphere)와 교차하는지 충돌 수학 연산 수행.
  - 판정 완료 후, 임시 백업해두었던 현재 위치로 플레이어 위치를 원복.
- [ ] **[게임 룰] 체력(HP) 차감 및 리스폰(Respawn) 로직 구현**:
  - 피격 성공 시 대상의 `Player::hp` 차감.
  - 체력이 `0` 이하가 되면 사망 처리(`death++`, 발사자 `kill++`) 및 지면(y=0)에 리스폰 및 무적시간 부여 등 기초 게임 룰 구현.

### C. 매칭 및 기타 플랫폼 통신 연동
- [ ] **실제 게임 룸 생성 로직 연동 (C++ WebServerClient)**:
  - C++ 서버가 [MatchCreateRequest](file:///C:/Dev/reverse-planning-project/proto/Internal.proto#L21) 패킷을 수신했을 때, `HandleMatchCreateRequest` 내부에서 실제 게임 룸(Room) 클래스의 생성 및 초기화 로직 연동.
- [ ] **MatchCreateResponse 응답 전송**:
  - 생성된 룸의 동적 포트 번호 등을 담아 [MatchCreateResponse](file:///C:/Dev/reverse-planning-project/proto/Internal.proto#L27) 패킷을 C# 서버로 전송하는 응답 시나리오 완료.
- [ ] **JWT 토큰 검증 핸드오버 (Security)**:
  - 매칭 성공 유저들이 C++ 로직 서버로 직접 접속할 때, AuthServer 측에서 발행한 JWT 세션 토큰 정보를 대조 및 검증하는 세션 매칭 구현.
- [ ] **게임 결과 피드백 웹훅 (Webhook)**:
  - 게임 종료 시 C++ 서버에서 AuthServer에 승리팀 및 매치 스코어 통계 정보를 쏘아주는 웹훅 엔드포인트 구현 (Phase 4 진입을 위한 사전 준비).
- [ ] **성능 프로파일링 및 텔레메트리(Telemetry) 수집 시스템 구축**:
  - 인게임 틱 지연 시간 및 사격 성능 데이터(되감기/충돌/복구 소요 시간)를 런타임에 성능 분석 로그 파일로 지속 기록하거나, 게임 통계 분석을 위해 로깅 DB 및 웹훅으로 수집 및 전송하는 파이프라인 개발.


## 2. 아키텍처 확장 및 개선 논의 (Future Scalability & Architecture)

### D. 분산 데디케이티드 룸 서버 (Distributed Dedicated Room Server) 아키텍처 전환
- [ ] **1룸 1프로세스(Room-per-Process) 데디케이티드 서버 모델 전환**:
  - 현재 1개의 C++ 프로세스 내에서 여러 개의 Room 객체들을 스레드(Thread)로 실행하는 구조에서 발생할 수 있는 장애 전파(한 방의 오류로 서버 전체 다운) 및 메모리 누수 문제를 극복하기 위해, 하나의 프로세스가 단 하나의 방만 처리하도록 구조 전환.
  - 방 생성 요청 시 로직 서버 프로세스 자체를 독립적으로 실행하고 배정하는 프로세스 기반 격리 설계 구축.
- [ ] **오케스트레이션(Orchestration) 도입 구상**:
  - AWS GameLift 또는 Agones(Kubernetes) 같은 오케스트레이터를 도입하여 빌드된 C++ 서버를 Docker 컨테이너 이미지화하고, 실시간 매칭 수요에 따라 동적으로 로직 서버 프로세스를 늘리고 줄이는(Scale-Out) 클라우드 인프라 설계 연구.

### E. Unity 연동 지연 보상(Lag Compensation) 시각화 클라이언트 개발 (플레이어블 데모)
- [ ] **Protobuf C# 빌드 파이프라인 구축**:
  - `Packet.proto`를 C#용 코드로 컴파일하여 유니티 프로젝트에 통합하고 `Google.Protobuf` 패키지 설정.
- [ ] **C++ 서버 디버그 패킷 전송 로직 구현**:
  - `World::Shoot` 시 사격선 궤적 및 되감기 전/후 좌표(Present/Rewound Ghost Mesh 용) 정보를 담은 `DebugLagComp` 패킷 정의 및 클라이언트 전송 구현.
- [ ] **유니티 플레이어블 시각화 씬 구축**:
  - 실시간 TCP/UDP 통신 클라이언트(NetworkManager) 구현.
  - 사용자가 직접 조작(이동, 사격)하여 가상의 움직이는 적 봇들을 맞출 때, 서버로부터 수신한 되감기 정보를 바탕으로 빨간색(현재 위치) 및 초록색(되감긴 과거 위치) 고스트 메쉬와 사격선(LineRenderer)을 화면에 동적으로 렌더링.
  - 핑(Ping / RTT) 지연을 의도적으로 조절할 수 있는 시뮬레이션 슬라이더 및 스코어보드 UI 탑재.
