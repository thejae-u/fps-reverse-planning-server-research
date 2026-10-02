#pragma once

#include <cstddef>
#include <cstdint>

namespace GameRules
{
inline constexpr std::int16_t MaximumHealth = 100;
inline constexpr std::int16_t MaximumAmmo = 30;
inline constexpr std::int16_t PlayerAttackPower = 50;
// 기존 handshake 값은 실제 Player 공격력과 다르다. 이번 변경에서는 값을 보존한다.
inline constexpr std::int16_t HandshakeAttackPower = 10;
inline constexpr std::size_t DefaultHistory = 60;
inline constexpr std::size_t TeamCount = 2;
inline constexpr float SpawnCenterIndex = 4.5f;
inline constexpr float SpawnSpacing = 2.0f;
inline constexpr float DefaultDeltaSeconds = 0.016666f;
inline constexpr float MovementEpsilon = 0.0001f;
inline constexpr float MaximumTickDeltaSeconds = 0.1f;
inline constexpr float MaximumInputDeltaSeconds = 0.5f;
inline constexpr float SprintMagnitudeThreshold = 1.5f;
inline constexpr float MovementDistanceTolerance = 2.5f;
} // namespace GameRules