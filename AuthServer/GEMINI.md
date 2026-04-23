# Project: ASP.NET & C++ Logic Server Integration (Learning Path)

This project focuses on building a robust backend infrastructure where an ASP.NET Auth/Match server orchestrates sessions and connects users to a C++ Logic (Game) server.

## Core Objective
To understand and implement the full lifecycle of a multiplayer game session, from matchmaking and chat validation to logic server synchronization and post-game result processing.

## Collaboration Rules
- **User-Led Coding:** The user will perform the majority of code implementation.
- **Agent Guidance:** Gemini CLI acts as a senior architect and peer reviewer, providing research, strategic plans, and code reviews for each curriculum step.
- **No Unsolicited Edits:** The agent MUST NOT modify any code unless explicitly requested by the user. If no request is made, the agent should only provide analysis and proposals.
- **Language Preference:** All responses from the agent MUST be in Korean.
- **Validation First:** Every step must be validated through discussion or testing before moving to the next.

## 📈 Progress Log

### ✅ Phase 1: Chat Security & Match Validation (Completed)
- **1.1 SignalR Hub Validation:**
  - `MatchHub`에 `MatchService`를 주입하여 유저의 매치 소속 여부를 실시간 검증하도록 구현.
  - 잘못된 `matchId`로의 접근이나 메시지 전송을 `HubException`으로 차단.
- **1.2 Group Management & Automation:**
  - `MatchService`에서 `connectionId`를 추적하는 저장소(`_userConnections`) 구현.
  - 매칭 성공 시 서버가 즉시 유저를 SignalR 그룹에 참여시키고 입장을 알리는 자동화 흐름 완성.
  - `OnConnectedAsync` 및 `OnDisconnectedAsync`를 통한 실시간 커넥션 생명주기 관리.
- **UI/Test Environment:**
  - `index.html`을 수정하여 자동 채팅 참여 로직을 검증할 수 있는 테스트 UI 구축 및 시각적 피드백(ok-border) 추가.

### ✅ Phase 2.1: Persistence Environment Setup (Completed)
- **Redis Integration:** `StackExchange.Redis` 패키지 설치 및 `Program.cs` 내 의존성 주입 완료.
- **Docker Compose:** Redis 인스턴스 추가 및 `auth-server`와의 네트워크 연동 설정 완료.

### ✅ Phase 2.2: Redis Matchmaking Queue Implementation (Completed)
- **Redis 전환:** `MatchService`의 인메모리 대기열(`Queue`), 유저 엔트리(`Dictionary`), 매칭 결과 데이터를 모두 Redis(List, Hash)로 이전 완료.
- **비동기화 및 트랜잭션:** 모든 데이터 접근 로직을 `async/await` 및 Redis Transaction(`ITransaction`)으로 구현하여 데이터 정합성 확보.
- **Hub Lifecycle 연동:** 유저의 Hub 연결 종료(`OnDisconnectedAsync`) 시 Redis 대기열 및 엔트리 정보를 즉시 삭제하여 불필요한 매칭 방지 로직 구현.

### ✅ Phase 2.3: Distributed Connection Management (Completed)
- **무상태성(Stateless) 확보:** `MatchService` 내부의 모든 인메모리 컨테이너(`Dictionary`, `Queue`)를 삭제하고 Redis로 완전 교체.
- **분산 커넥션 관리:** 유저의 SignalR `connectionId` 정보를 Redis Hash에 저장하여 여러 서버 인스턴스 간에 실시간 커넥션 정보를 공유할 수 있도록 구현.

---

## Curriculum Roadmap

### Phase 1: Chat Security & Match Validation (Completed)
- [x] 1.1 SignalR Hub Validation
- [x] 1.2 Group Management & Connection Tracking

### Phase 2: Persistence & Distributed Caching (Completed)
- [x] 2.1 Redis Environment Setup
- [x] 2.2 Redis Matchmaking Queue Implementation
- [x] 2.3 Distributed Connection Management
- [ ] **2.4 Database Schema:** Design and implement EF Core entities for Users and MatchResults.


### Phase 3: C++ Logic Server Integration
- **3.1 Communication Protocol:** Define the bridge between ASP.NET and C++ (REST/gRPC/Socket) for match creation and "Ready" signals.
- **3.2 JWT Handover:** Implement a secure token exchange so the C++ server can authenticate users joining from the ASP.NET server.

### Phase 4: Game Lifecycle & Post-Processing
- **4.1 Result Webhook:** Implement an endpoint for the C++ server to report game outcomes (scores, winners).
- **4.2 Cleanup & Analytics:** Update user statistics in the DB and purge temporary match data from Redis.

---
*Note: This document serves as the foundational mandate for this workspace.*
