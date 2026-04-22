# Project: ASP.NET & C++ Logic Server Integration (Learning Path)

This project focuses on building a robust backend infrastructure where an ASP.NET Auth/Match server orchestrates sessions and connects users to a C++ Logic (Game) server.

## Core Objective
To understand and implement the full lifecycle of a multiplayer game session, from matchmaking and chat validation to logic server synchronization and post-game result processing.

## Collaboration Rules
- **User-Led Coding:** The user will perform the majority of code implementation.
- **Agent Guidance:** Gemini CLI acts as a senior architect and peer reviewer, providing research, strategic plans, and code reviews for each curriculum step.
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

---

## Curriculum Roadmap

### Phase 1: Chat Security & Match Validation (Completed)
- [x] 1.1 SignalR Hub Validation
- [x] 1.2 Group Management & Connection Tracking

### Phase 1.3: GUID Match ID Strategy (Next)
- [ ] **성능 및 보안을 고려한 식별자 설계:** 현재 사용 중인 `Guid.NewGuid().ToString("N")` 방식의 장단점을 파악하고, DB 인덱스 효율성(Sequential GUID) 및 보안(URL 인코딩 등)을 고려한 전역 매치 식별자 전략 수립.

### Phase 2: Persistence & Distributed Caching
- [ ] 2.1 Redis Matchmaking Queue: Transition in-memory dictionaries and queues to Redis for scalability and persistence.
- [ ] 2.2 Database Schema: Design and implement EF Core entities for Users and MatchResults.

### Phase 3: C++ Logic Server Integration
- **3.1 Communication Protocol:** Define the bridge between ASP.NET and C++ (REST/gRPC/Socket) for match creation and "Ready" signals.
- **3.2 JWT Handover:** Implement a secure token exchange so the C++ server can authenticate users joining from the ASP.NET server.

### Phase 4: Game Lifecycle & Post-Processing
- **4.1 Result Webhook:** Implement an endpoint for the C++ server to report game outcomes (scores, winners).
- **4.2 Cleanup & Analytics:** Update user statistics in the DB and purge temporary match data from Redis.

---
*Note: This document serves as the foundational mandate for this workspace.*
