#pragma once

#include "Packet.pb.h"
#include "Internal.pb.h"

struct ServerConfig
{
    std::string matchId = "";
    std::string authToken = "";
    std::uint16_t tcpPort = 0; // default tcp port
    std::uint16_t udpPort = 0; // default udp port
    std::vector<std::string> allowedPlayers;
    bool isTestMode = false;

    static ServerConfig Parse(int argc, char** argv)
    {
        ServerConfig config;

        for(int i = 1; i < argc; ++i) // 0 is main
        {
            std::string arg = argv[i];

            if(arg == "--test" || arg == "-t")
            {
                config.isTestMode = true;
            }
            else if((arg == "--tcp-port" || arg == "-tp") && i + 1 < argc)
            {
                config.tcpPort = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            }
            else if((arg == "--udp-port" || arg == "-up") && i + 1 < argc)
            {
                config.udpPort = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            }
            else if((arg == "--match-id" || arg == "-m") && i + 1 < argc)
            {
                config.matchId = argv[++i];
            }
            else if((arg == "--auth-token" || arg == "--token" || arg == "-at") && i + 1 < argc)
            {
                config.authToken = argv[++i];
            }
            else if((arg == "--players" || arg == "-p") && i + 1 < argc)
            {
                std::string playersCsv = argv[++i];
                std::stringstream ss(playersCsv);
                std::string token;

                while(std::getline(ss, token, ','))
                {
                    if(!token.empty())
                        config.allowedPlayers.push_back(token);
                }
            }
            else if(arg == "--help" || arg == "-h")
            {
                std::cout << "Usage: LogicServer [Options]\n"
                << "    --match-id <id>         Match UUID\n"
                << "    --auth-token <token>    Auth Token for validate\n"
                << "    --tcp-port <port>       TCP Listen Port\n"
                << "    --udp-port <port>       UDP Listen Port\n"
                << "    --players <p1, p2>      Allowed Players tokens (CSV)\n"
                << "    --test                  Run in test simulation mode\n";
                std::exit(0);
            }
        }

        spdlog::info("[Config] MatchId: {}, TCP Port: {}, UDP Port: {}, Player Count: {}, TestMode: {}",
                     config.matchId, config.tcpPort, config.udpPort, config.allowedPlayers.size(), config.isTestMode);

        return config;
    }
};

enum class SessionState
{
    Invalid,
    Initializing,
    InitializeComplete,
    WaitMatching,
    Matched,
    Ingame,
};

using CallbackHandle = std::uint64_t;

enum class CPacketType
{
    Error = -1,

    // Normal
    PacketOk = 0,
    InvalidData = 1,
    ErrorOccured = 2,

    // Tcp
    PortHandshake = 100,
    InfoHandshake = 101,
    Ping = 102,
    Match = 103,

    // Udp
    Ingame = 200,
    Authentication = 201
};

enum class CIngameType
{
    IngameOk = 0,
    Move = 1,
    Jump = 2,
    Shoot = 3,
    Hit = 4
};

enum class CMatchmakingType
{
    MatchmakingOk = 0,
    Request = 1,
    Success = 2,
    Failed = 3
};

enum class CAuthenticationType
{
    AuthenticationOk = 0,
    UdpHolePunching = 1
};

enum class MatchingLastError
{
    Success = 0,
    FailedByExsists = 1,
    FailedByInternalError = 2,
};

namespace CUtility
{
inline CPacketType ToCPacketType(Protocol::PacketType type)
{
    switch(type)
    {
    // Normal
    case Protocol::PacketType::PacketOk: return CPacketType::PacketOk;
    case Protocol::PacketType::InvalidData: return CPacketType::InvalidData;
    case Protocol::PacketType::ErrorOccured: return CPacketType::ErrorOccured;

    // Tcp
    case Protocol::PacketType::PortHandshake: return CPacketType::PortHandshake;
    case Protocol::PacketType::InfoHandshake: return CPacketType::InfoHandshake;
    case Protocol::PacketType::Ping: return CPacketType::Ping;
    case Protocol::PacketType::Match: return CPacketType::Match;

    // Udp
    case Protocol::PacketType::Ingame: return CPacketType::Ingame;
    case Protocol::PacketType::Authentication: return CPacketType::Authentication;

    default: return CPacketType::Error;
    }
}

inline std::string ConvertTypeToString(Protocol::PacketType type)
{
    switch(type)
    {
    case Protocol::PacketType::PacketOk: return "Ok";
    case Protocol::PacketType::InvalidData: return "InvalidData";
    case Protocol::PacketType::ErrorOccured: return "ErrorOccured";
    case Protocol::PacketType::PortHandshake: return "PortHandshake";
    case Protocol::PacketType::InfoHandshake: return "InfoHandshake";
    case Protocol::PacketType::Ping: return "Ping";
    case Protocol::PacketType::Ingame: return "Ingame";
    case Protocol::PacketType::Authentication: return "Authentication";

    default: return "INVALID_TYPE_ERROR";
    }
}

inline std::unique_ptr<std::string> InternalPacketSerializer(const Internal::GamePacket& packet)
{
    /* 
        payload 
        header(2byte) + body(8byte)

        header : body size (uint16_t)
        body : Protobuf Internal::GamePacket
    */

    const std::uint16_t bodySize = static_cast<std::uint16_t>(packet.ByteSizeLong());
    const std::uint16_t networkSize = htons(bodySize);
    auto payload = std::make_unique<std::string>(sizeof(std::uint16_t) + bodySize, '\0'); // header 2byte + body packet size + '\0'

    std::memcpy(&(*payload)[0], &networkSize, 2);          // header
    if(!packet.SerializeToArray(&(*payload)[2], bodySize)) // body serialize after header
    {
        return nullptr;
    }

    return payload;
}

inline std::unique_ptr<Internal::GamePacket> InternalPacketDeserializer(const char* data, std::size_t size)
{
    auto packet = std::make_unique<Internal::GamePacket>();
    if(!packet->ParseFromArray(data, static_cast<int>(size)))
    {
        return nullptr;
    }

    return packet;
}

} // namespace CUtility