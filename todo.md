# Internal Connection Implementation TODO

현재 C# AuthServer와 C++ Logic Server 간의 내부 통신 연동을 위한 잔여 작업 목록입니다.

## 1. Logic Server (C++) 작업
- [ ] **Packet Framing 구현**: `WebServerClient::ReceiveAsync`에서 받은 데이터를 헤더(2바이트, Big Endian)와 바디로 분리하는 로직 추가.
- [ ] **Protobuf Deserialization**: 분리된 바디 데이터를 `Internal::GamePacket` 객체로 파싱.
- [ ] **Packet Dispatcher 구현**: `GamePacket`의 `payload_case`에 따라 적절한 핸들러 함수로 분기.
- [ ] **MatchCreateRequest 핸들러 구현**:
    - [ ] 전달받은 `match_id`와 유저 정보를 바탕으로 실제 게임 룸(Room) 생성 로직 호출.
    - [ ] 생성된 룸의 포트 정보를 포함한 `MatchCreateResponse` 생성.
- [ ] **Response 전송 로직**: 생성된 응답 패킷을 다시 `Length-Prefix` 형식으로 직렬화하여 AuthServer에 전송.

## 2. AuthServer (C#) 작업
- [ ] **에러 핸들링 강화**: Logic Server로부터 응답이 오지 않거나(Timeout), 실패 응답을 받았을 때의 예외 처리 로직 보완.
- [ ] **연결 상태 모니터링**: `LogicServerConnectionPool`에서 끊어진 연결을 감지하고 재연결하는 메커니즘 점검.

## 3. 통합 테스트
- [ ] **로컬 연동 테스트**: AuthServer에서 매칭 시뮬레이션을 돌려 Logic Server에 방 생성이 실제로 요청되고 응답이 돌아오는지 확인.
- [ ] **데이터 정합성 검증**: 전달된 JWT 토큰 및 유저 ID가 Logic Server 세션 정보에 올바르게 기록되는지 확인.
