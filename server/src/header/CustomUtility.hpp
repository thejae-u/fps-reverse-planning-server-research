#pragma once

#include "Packet.pb.h"

struct ServerConfig
{
    std::string matchId = "";
    std::string authToken = "";
    std::uint16_t tcpPort = 0; // default tcp port
    std::uint16_t udpPort = 0; // default udp port
    std::vector<std::string> allowedPlayers;

    static ServerConfig Parse(int argc, char** argv)
    {
        ServerConfig config;

        for(int i = 1; i < argc; ++i) // 0 is main
        {
            std::string arg = argv[i];

            if((arg == "--tcp-port" || arg == "-tp") && i + 1 < argc)
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
                << "    --players <p1, p2>      Allowed Players tokens (CSV)\n";
                std::exit(0);
            }
        }

        spdlog::info("[Config] MatchId: {}, TCP Port: {}, UDP Port: {}, Player Count: {}",
                     config.matchId, config.tcpPort, config.udpPort, config.allowedPlayers.size());

        return config;
    }
};

enum class SessionState
{
    Invalid,
    Initializing,
    InitializeComplete,
    Ingame,
};