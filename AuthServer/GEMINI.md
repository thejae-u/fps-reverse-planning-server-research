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

### ✅ Phase 2.4: Database Schema & Security Hardening (Completed)
- **Security & Environment:** `.env` 파일을 통한 민감 정보(DB, JWT) 관리 및 Docker 컨테이너 포트 격리(Internal Only)로 보안 강화.
- **EF Core & PostgreSQL:** `Npgsql` 프로바이더 설치 및 `ApplicationDbContext`를 통한 `User`, `MatchResult` 엔티티 매핑 완료.
- **Stateless Architecture:** Redis(휘발성/캐시)와 PostgreSQL(영구/저장)의 역할 분담 체계 구축.

---

## Curriculum Roadmap

### Phase 1: Chat Security & Match Validation (Completed)
- [x] 1.1 SignalR Hub Validation
- [x] 1.2 Group Management & Connection Tracking

### Phase 2: Persistence & Distributed Caching (Completed)
- [x] 2.1 Redis Environment Setup
- [x] 2.2 Redis Matchmaking Queue Implementation
- [x] 2.3 Distributed Connection Management
- [x] 2.4 Database Schema & Security Hardening

### Phase 3: C++ Logic Server Integration
- [ ] **3.1 Protobuf & TCP Connection Pool:**
  - [x] **3.1.1 Schema Definition:** `.proto` 파일을 통한 ASP.NET ↔ C++ 공용 메시지 규격 확립 및 빌드 환경 최적화.
  - [x] **3.1.2 Packet Framing:** `Length-Prefix` 기반의 TCP 패킷 구조 설계.
  - [x] **3.1.3 Connection Pool:** 고성능 통신을 위한 TCP 소켓 관리 로직 구현.
- [ ] **3.2 Match Creation Workflow:**
  - [ ] **3.2.1 MatchWorker Integration:** 매칭 성공 시 C++ 서버에 방 생성 요청 연동.
  - [ ] **3.2.2 Result Handling:** C++ 서버 응답 처리 및 클라이언트 접속 정보 전달.
- [ ] **3.3 JWT Handover:**
  - [ ] **3.3.1 Token Exchange:** C++ 서버 인증을 위한 보안 토큰 전달 메커니즘 구현.

### Phase 4: Game Lifecycle & Post-Processing
- **4.1 Result Webhook:** Implement an endpoint for the C++ server to report game outcomes (scores, winners).
- **4.2 Cleanup & Analytics:** Update user statistics in the DB and purge temporary match data from Redis.

---
*Note: This document serves as the foundational mandate for this workspace.*
