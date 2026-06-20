# AuthServer API Specification

## 1. REST API

### Authentication (`/auth`)
| Method | Endpoint | Description | Auth |
| :--- | :--- | :--- | :--- |
| `POST` | `/auth/register` | 새로운 사용자 계정 생성 | No |
| `POST` | `/auth/login` | 로그인 및 JWT 토큰 발급 | No |
| `GET` | `/auth/me` | 현재 로그인된 사용자 정보 조회 | Yes |

### Matchmaking (`/match`)
| Method | Endpoint | Description | Auth |
| :--- | :--- | :--- | :--- |
| `POST` | `/match/join` | 매칭 대기열 참가 요청 | Yes |
| `POST` | `/match/cancel` | 매칭 대기열 참가 취소 | Yes |
| `GET` | `/match/status` | 현재 매칭 대기 상태 또는 매칭 결과 조회 | Yes |

---

## 2. SignalR Hub (`/matchHub`)

매칭 및 실시간 채팅을 위한 웹소켓 허브입니다. 모든 통신에는 JWT 인증이 필요합니다.

### Client to Server (Methods)
| Method | Arguments | Description |
| :--- | :--- | :--- |
| `Ping` | `-` | 서버 연결 상태 확인 |
| `SendMatchMessage` | `matchId`, `message` | 소속된 매치 채팅방에 메시지 전송 |
| `LeaveMatchChat` | `matchId` | 매치 채팅방 퇴장 |

### Server to Client (Events)
| Event | Payload | Description |
| :--- | :--- | :--- |
| `Pong` | `{ Message, Time }` | Ping에 대한 응답 |
| `Matched` | `{ UserId, Username, Status, MatchId, ServerAddress, MatchedAtUtc }` | 매칭 성공 시 수신하는 상세 정보 |
| `JoinedMatchChat` | `{ matchId, connectionId }` | 매칭 성공 후 채팅방 자동 입장이 완료됨을 알림 |
| `ReceiveMatchMessage` | `{ matchId, sendeerUserId, senderName, message, sendAt }` | 다른 사용자로부터 메시지 수신 |
| `SystemMessage` | `{ matchId, message, sendAt }` | 입장/퇴장 등 시스템 공지 수신 |
| `LeftMatchChat` | `{ matchId, connectionId }` | 채팅방 퇴장 처리 완료 알림 |

---

## 3. Data Transfer Objects (Key DTOs)

### Auth
- **LoginResponse**: `userId`, `username`, `token`

### Match
- **MatchJoinResponse**: `userId`, `username`, `status`, `joinedAtUtc`
- **MatchStatusResponse**: `userId`, `username`, `status`, `joinedAtUtc`, `matchId`, `serverAddress`
