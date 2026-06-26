# Internal Connection Implementation & Integration Status

현재 C# AuthServer와 C++ Logic Server 간의 내부 통신 연동 및 환경 구성 완료 상태와 향후 잔여 작업 목록입니다.

## 1. 완료된 작업 (Completed)
- [x] **양방향 포트 충돌 우회 및 연동 설정**:
  - Windows 가상 포트 예약 제외 범위(`8955 - 9054`)와의 충돌을 피하기 위해 통신 포트를 조정 완료 (C++ 리슨: `9100` / C# 리슨: `9102`).
  - [appsettings.json](file:///C:/Dev/reverse-planning-project/AuthServer/appsettings.json) 및 [ConnectionPool.hpp](file:///C:/Dev/reverse-planning-project/server/src/header/ConnectionPool.hpp) 포트 설정 동기화 완료.
- [x] **인프라 환경 호스트 포트 노출**:
  - Docker Compose 파일([docker-compose.yml](file:///C:/Dev/reverse-planning-project/AuthServer/docker-compose.yml))의 Redis 포트(6379)를 호스트로 노출하여 로컬 개발 환경에서의 연동 지원.
- [x] **AuthServer DI(의존성 주입) 수명 불일치 버그 해결**:
  - 싱글톤 `MatchService`가 스코프 수명의 `ApplicationDbContext`를 캡처하여 예외를 발생시키던 구조를 `IServiceScopeFactory` 기반 스코프 생성 방식으로 변경 완료 ([MatchService.cs](file:///C:/Dev/reverse-planning-project/AuthServer/Services/MatchService.cs)).
- [x] **누락된 빌드 종속성 및 스크립트 수정**:
  - C# 프로젝트의 `Serilog.AspNetCore` 패키지 참조 추가 및 빌드 오류 해결.
  - C++ `cl.exe` 컴파일러 경로를 찾기 위해 `build.bat`에 Visual Studio 개발자 명령 프롬프트 환경을 자동 로드하는 기능 보완.
- [x] **양방향 자동 연결성 검증(InternalTest) 및 재시도 정책**:
  - 서버 실행 시 비동기적으로 상대 서버의 활성화 여부를 확인하는 연결 테스트 메서드 구현 완료.
  - 서버 시차 기동 상황을 지원하기 위해 최대 10회(3초 간격, 30초 대기)의 논블로킹 재시도 루프 검증 완료.

---

## 2. 향후 잔여 작업 (Remaining To-Do)
- [ ] **고정 60Hz 틱 레이트 메인 루프 개선 및 정밀화 (Timer Drift 해결)**:
  - [World.cpp](file:///C:/Dev/reverse-planning-project/server/src/sources/World.cpp)의 [ScheduleNextTick](file:///C:/Dev/reverse-planning-project/server/src/sources/World.cpp#L59) 타이머 로직을 `expires_at` 절대 시간 기점 예약으로 수정하여 누적 지연(Drift) 현상 제거.
- [ ] **서버 사이드 물리 틱 Catch-up(시간 따라잡기) 로직 구현**:
  - CPU 스파이크 및 프레임 드랍 등으로 밀린 물리 틱 시간만큼 루프 내에서 연속 `Update()`를 호출하여 복구하는 캐치업 로직 설계.
- [ ] **서버 사이드 FPS 코어 루프 구현**:
  - 플레이어 이동·회전, 총 발사, 피격 판정 등의 기본 FPS 코어 로직을 서버에서 독립적으로 계산 및 관리.
- [ ] **권위적 서버 및 클라이언트 예측/보정 설계**:
  - 네트워크 연동 시 서버를 권위적(Authoritative)으로 유지하면서, 클라이언트가 예측(Prediction) 및 보정(Interpolation)을 통해 화면을 부드럽게 보이도록 처리하는 연동 아키텍처 설계.
- [ ] **실제 게임 룸 생성 로직 연동 (C++ WebServerClient)**:
  - C++ 서버가 [MatchCreateRequest](file:///C:/Dev/reverse-planning-project/proto/Internal.proto#L21) 패킷을 수신했을 때, `HandleMatchCreateRequest` 내부에서 실제 게임 룸(Room) 클래스의 생성 및 초기화 로직 연동.
- [ ] **MatchCreateResponse 응답 전송**:
  - 생성된 룸의 동적 포트 번호 등을 담아 [MatchCreateResponse](file:///C:/Dev/reverse-planning-project/proto/Internal.proto#L27) 패킷을 C# 서버로 전송하는 응답 시나리오 완료.
- [ ] **JWT 토큰 검증 핸드오버 (Security)**:
  - 매칭 성공 유저들이 C++ 로직 서버로 직접 접속할 때, AuthServer 측에서 발행한 JWT 세션 토큰 정보를 대조 및 검증하는 세션 매칭 구현.
- [ ] **게임 결과 피드백 웹훅 (Webhook)**:
  - 게임 종료 시 C++ 서버에서 AuthServer에 승리팀 및 매치 스코어 통계 정보를 쏘아주는 웹훅 엔드포인트 구현 (Phase 4 진입을 위한 사전 준비).
- [ ] **성능 최적화를 위한 패킷 버퍼 풀(Packet Buffer Pool) 구현**:
  - `IngamePacket` 전용 인스턴스 풀링 시스템 구축.
  - `shared_ptr` 커스텀 삭제자를 활용한 대여(Rent) 및 자동 반환(Auto-Return) 구조 구현.
  - 메모리 오버헤드 최소화를 위해 웜 풀(최소 유지) 및 최대 한도 제한(초과 시 동적 해제) 정책 반영.
  - 동시성 병목 방지를 위한 스레드 세이프(Mutex 또는 Lock-free) 동기화 설계.
