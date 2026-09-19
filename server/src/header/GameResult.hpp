#pragma once

#include "Player.hpp"

struct TeamInfo {
    TeamType teamType;

    std::int16_t kills;
    std::int16_t deaths;
    std::int16_t assists;

    std::int32_t damages;
    std::int32_t heals;
    std::int32_t guards;

    TeamInfo() : teamType(TeamType::None), kills(0), deaths(0), assists(0), damages(0), heals(0), guards(0) {}
    TeamInfo(TeamType teamType) : teamType(teamType), kills(0), deaths(0), assists(0), damages(0), heals(0), guards(0) {}
};

struct GameResult
{
    TeamInfo teamAInfo;
    TeamInfo teamBInfo;

    TeamType winningTeam;

    GameResult() : teamAInfo(TeamType::TeamA), teamBInfo(TeamType::TeamB), winningTeam(TeamType::None) {}
    GameResult(const std::vector<TeamInfo>& teamInfos)
    {
        teamAInfo = teamInfos[0]; 
        teamBInfo = teamInfos[1];
        winningTeam = teamAInfo.kills > teamBInfo.kills ? TeamType::TeamA : TeamType::TeamB; // kill이 더 많은 팀이 승리 (임시조건)
    }
};