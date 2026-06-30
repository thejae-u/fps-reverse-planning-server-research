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
