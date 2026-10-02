#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>
#include "Packet.pb.h"

struct Participant
{
    std::uint64_t generation{};
    std::int32_t presetId{};
};

struct Recipient
{
    uuids::uuid id;
    // 송신 시각이 아닌 연결 인스턴스 번호. 재접속 이전의 명령을 거른다.
    std::uint64_t generation{};
};

enum class Transport
{
    Udp,
    Tcp
};

enum class StateKind
{
    Movement,
    Scoreboard
};

struct StateKey
{
    StateKind kind;
    uuids::uuid entityId;
    bool operator==(const StateKey &) const = default;
};

struct OutboundMessage
{
    Protocol::PacketType type;
    Transport transport;
    std::vector<Recipient> recipients;
    std::shared_ptr<const std::string> payload;
    std::optional<StateKey> stateKey; // 네트워크 지연 또는 패킷 밀림 발생 시 지연을 최소화 하기 위한 StateKey
    std::chrono::steady_clock::time_point submitted = std::chrono::steady_clock::now();
};

class NetworkGateway
{
  public:
    virtual ~NetworkGateway() = default;
    virtual void PostSend(OutboundMessage message) = 0;
};
