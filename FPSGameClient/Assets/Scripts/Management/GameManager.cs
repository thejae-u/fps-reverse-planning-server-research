using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif
using FPSGame.Player;
using FPSGame.UI;
using Protocol;

namespace FPSGame.Management
{
    public class GameManager : MonoBehaviour
    {
        public static GameManager Instance { get; private set; }

        [Header("Networking & Capacity")]
        [SerializeField] private int maxPlayers = 10;
        [SerializeField] private GameObject playerPrefab;
        [SerializeField] private Transform[] spawnPoints;

        [Header("UI Reference")]
        [SerializeField] private FPSUIController uiController;

        private readonly List<PlayerController> activePlayers = new List<PlayerController>();
        private readonly Dictionary<string, PlayerController> networkPlayers = new Dictionary<string, PlayerController>();
        private readonly Dictionary<string, ScoreboardRowViewData> playerScoreMap = new Dictionary<string, ScoreboardRowViewData>();
        private PlayerController localPlayer;
        private int localTeamId = 0;
        private int lastWinningTeam = 0;
        private int lastTeamAScore = 0;
        private int lastTeamBScore = 0;
        private bool isMatchEnded = false;

        public PlayerController LocalPlayer => localPlayer;
        public IReadOnlyList<PlayerController> ActivePlayers => activePlayers;
        public bool IsMatchEnded => isMatchEnded;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void AutoInitialize()
        {
            if (UnityEngine.SceneManagement.SceneManager.GetActiveScene().name == "MenuScene" ||
                FindAnyObjectByType<MenuUIController>() != null)
            {
                return;
            }

            if (Instance == null && FindAnyObjectByType<GameManager>() == null)
            {
                Debug.Log("[GameManager] Auto-initializing FPS GameManager on scene load...");
                GameObject gm = new GameObject("GameManager_Auto");
                gm.AddComponent<GameManager>();
            }
        }

        private void Awake()
        {
            if (UnityEngine.SceneManagement.SceneManager.GetActiveScene().name == "MenuScene" ||
                FindAnyObjectByType<MenuUIController>() != null)
            {
                Destroy(gameObject);
                return;
            }

            if (Instance == null) Instance = this;
            else Destroy(gameObject);
        }

        private void Start()
        {
            SpawnLocalPlayer();
            UpdatePlayerCountUI();
        }

        private void Update()
        {
            HandleTestInputs();
        }

        private void HandleTestInputs()
        {
            bool f2Pressed = false;

#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null)
            {
                f2Pressed = Keyboard.current.f2Key.wasPressedThisFrame;
            }
#else
            f2Pressed = Input.GetKeyDown(KeyCode.F2);
#endif

            if (f2Pressed)
            {
                RemoveAllRemotePlayers();
            }
        }

        public void SpawnLocalPlayer()
        {
            EnsureEnvironmentAndUI();

            Vector3 spawnPos = Vector3.up * 1.5f;
            Quaternion spawnRot = Quaternion.identity;

            if (spawnPoints != null && spawnPoints.Length > 0 && spawnPoints[0] != null)
            {
                spawnPos = spawnPoints[0].position;
                spawnRot = spawnPoints[0].rotation;
            }

            GameObject playerObj = null;
            if (playerPrefab != null)
            {
                playerObj = Instantiate(playerPrefab, spawnPos, spawnRot);
            }
            else
            {
                Debug.LogWarning("[GameManager] Player Prefab was not assigned. Generating dynamic player instance...");
                playerObj = CreateDynamicPlayerInstance(spawnPos, spawnRot);
            }

            localPlayer = playerObj.GetComponent<PlayerController>();

            if (localPlayer != null)
            {
                localPlayer.Initialize(0, isLocal: true, "MyPlayer");
                activePlayers.Add(localPlayer);

                if (uiController != null)
                {
                    uiController.BindPlayer(localPlayer);
                }

                var cachedInfo = NetworkManager.Instance?.Client?.LastInfoHandshake;
                if (cachedInfo != null)
                {
                    ApplyServerInfoHandshake(cachedInfo);
                }
            }
        }

        private void EnsureEnvironmentAndUI()
        {
            // If floor doesn't exist in scene, create basic training ground
            if (FindAnyObjectByType<Collider>() == null)
            {
                GameObject floor = GameObject.CreatePrimitive(PrimitiveType.Cube);
                floor.name = "Floor";
                floor.transform.position = new Vector3(0, -0.5f, 0);
                floor.transform.localScale = new Vector3(100f, 1f, 100f);

                // Spawn a few sample targets
                for (int i = 0; i < 4; i++)
                {
                    Vector3 pos = new Vector3((i - 1.5f) * 5f, 0, 15f + i * 3f);
                    CreateDynamicTargetDummy(pos);
                }
            }

            // If UI Controller is missing, create a simple runtime UI canvas
            if (uiController == null)
            {
                uiController = FindAnyObjectByType<FPSUIController>();
                if (uiController == null)
                {
                    GameObject canvasObj = new GameObject("HUD_Canvas_Runtime");
                    Canvas canvas = canvasObj.AddComponent<Canvas>();
                    canvas.renderMode = RenderMode.ScreenSpaceOverlay;
                    canvasObj.AddComponent<CanvasScaler>();
                    canvasObj.AddComponent<GraphicRaycaster>();
                    uiController = canvasObj.AddComponent<FPSUIController>();
                }
            }
        }

        private GameObject CreateDynamicPlayerInstance(Vector3 position, Quaternion rotation)
        {
            GameObject player = new GameObject("Player_Local");
            player.transform.position = position;
            player.transform.rotation = rotation;

            CharacterController cc = player.AddComponent<CharacterController>();
            cc.radius = 0.4f;
            cc.height = 1.8f;
            cc.center = new Vector3(0, 0.9f, 0);

            AudioSource audio = player.AddComponent<AudioSource>();
            audio.playOnAwake = false;

            PlayerSO dynamicSO = ScriptableObject.CreateInstance<PlayerSO>();
            PlayerController pc = player.AddComponent<PlayerController>();
            PlayerShooter ps = player.AddComponent<PlayerShooter>();
            pc.PlayerData = dynamicSO;
            ps.PlayerData = dynamicSO;

            // Camera Holder
            GameObject camHolder = new GameObject("CameraHolder");
            camHolder.transform.SetParent(player.transform);
            camHolder.transform.localPosition = new Vector3(0, 1.6f, 0);

            GameObject camObj = new GameObject("FirstPersonCamera");
            camObj.transform.SetParent(camHolder.transform);
            camObj.transform.localPosition = Vector3.zero;
            camObj.tag = "MainCamera";
            Camera cam = camObj.AddComponent<Camera>();
            cam.fieldOfView = 75f;
            camObj.AddComponent<AudioListener>();

            // Gun
            GameObject weaponRoot = new GameObject("WeaponRoot");
            weaponRoot.transform.SetParent(camHolder.transform);
            weaponRoot.transform.localPosition = new Vector3(0.24f, -0.2f, 0.45f);
            PlayerWeaponSway sway = weaponRoot.AddComponent<PlayerWeaponSway>();

            GameObject gunBody = GameObject.CreatePrimitive(PrimitiveType.Cube);
            gunBody.name = "GunBody";
            gunBody.transform.SetParent(weaponRoot.transform);
            gunBody.transform.localPosition = Vector3.zero;
            gunBody.transform.localScale = new Vector3(0.07f, 0.09f, 0.42f);
            Destroy(gunBody.GetComponent<Collider>());

            GameObject muzzlePoint = new GameObject("MuzzlePoint");
            muzzlePoint.transform.SetParent(weaponRoot.transform);
            muzzlePoint.transform.localPosition = new Vector3(0, 0.015f, 0.42f);

            // Third Person Model
            GameObject tpModel = new GameObject("ThirdPersonModel");
            tpModel.transform.SetParent(player.transform);
            GameObject tpBody = GameObject.CreatePrimitive(PrimitiveType.Capsule);
            tpBody.transform.SetParent(tpModel.transform);
            tpBody.transform.localPosition = new Vector3(0, 0.9f, 0);
            tpBody.transform.localScale = new Vector3(0.8f, 0.9f, 0.8f);
            Destroy(tpBody.GetComponent<Collider>());

            return player;
        }

        private void CreateDynamicTargetDummy(Vector3 position)
        {
            GameObject target = GameObject.CreatePrimitive(PrimitiveType.Cube);
            target.name = "TargetDummy_Dynamic";
            target.transform.position = position + Vector3.up * 1f;
            target.transform.localScale = new Vector3(0.8f, 1.5f, 0.2f);
            target.AddComponent<Combat.TargetDummy>();
        }

        public void RemoveAllRemotePlayers()
        {
            for (int i = activePlayers.Count - 1; i >= 1; i--)
            {
                Destroy(activePlayers[i].gameObject);
                activePlayers.RemoveAt(i);
            }
            networkPlayers.Clear();

            Debug.Log("[GameManager] Cleared all remote players.");
            UpdatePlayerCountUI();
        }

        public void ApplyServerInfoHandshake(InfoHandshakePacket info)
        {
            if (info == null) return;

            localTeamId = info.TeamId;

            if (localPlayer != null)
            {
                localPlayer.ApplyServerConfig(info);
            }

            string mySessionId = !info.SessionId.IsEmpty
                ? info.SessionId.ToStringUtf8()
                : (NetworkManager.Instance?.Client?.SessionId ?? string.Empty);

            // World::Init에서 전달된 초기 플레이어 목록(팀/스폰 위치)으로 원격 플레이어 초기 생성 및 아군(파랑)/적군(빨강) 색상 1회 설정
            if (info.Players != null && info.Players.Count > 0)
            {
                foreach (var pInfo in info.Players)
                {
                    string pid = pInfo.PlayerId.ToStringUtf8();
                    if (string.IsNullOrEmpty(pid))
                        continue;

                    bool isLocal = IsSamePlayerId(pid, mySessionId);
                    string shortId = pid.Length > 6 ? pid.Substring(0, 6) : pid;
                    string displayName = isLocal
                        ? (!string.IsNullOrEmpty(NetworkManager.Instance?.Username) ? NetworkManager.Instance.Username : $"Player_{shortId}")
                        : $"Player_{shortId}";

                    if (!playerScoreMap.TryGetValue(pid, out ScoreboardRowViewData row))
                    {
                        row = new ScoreboardRowViewData
                        {
                            PlayerId = pid,
                            DisplayName = displayName,
                            TeamId = pInfo.TeamId,
                            IsLocalPlayer = isLocal
                        };
                    }
                    else
                    {
                        row.TeamId = pInfo.TeamId;
                        row.IsLocalPlayer = isLocal;
                        row.DisplayName = displayName;
                    }
                    playerScoreMap[pid] = row;

                    if (isLocal)
                        continue;

                    Vector3 initSpawnPos = new Vector3(pInfo.SpawnX, pInfo.SpawnY, pInfo.SpawnZ);
                    PlayerController remotePlayer = GetOrCreateNetworkPlayer(pid, initSpawnPos);
                    if (remotePlayer != null)
                    {
                        remotePlayer.UpdateNetworkTransform(initSpawnPos, remotePlayer.transform.rotation);
                        remotePlayer.SetTeamStatus(pInfo.TeamId, info.TeamId);
                    }
                }
            }

            if (uiController != null)
            {
                if (info.MaxHp > 0)
                {
                    uiController.SetHealth(info.MaxHp, info.MaxHp);
                }
                uiController.UpdateTopTeamScoreHUD(lastTeamAScore, lastTeamBScore, localTeamId);
            }

            Debug.Log($"[GameManager] Applied Server InfoHandshake -> MoveSpeed: {info.MoveSpeed}, SprintSpeed: {info.SprintSpeed}, JumpSpeed: {info.JumpSpeed}, Gravity: {info.Gravity}, HP: {info.MaxHp}, Damage: {info.AttackPower}, Ammo: {info.MaxAmmo}, Team: {info.TeamId}, Spawn: ({info.SpawnX}, {info.SpawnY}, {info.SpawnZ}), PlayersCount: {info.Players?.Count ?? 0}");
        }

        private static bool IsSamePlayerId(string a, string b)
        {
            if (string.IsNullOrEmpty(a) || string.IsNullOrEmpty(b)) return false;
            if (string.Equals(a, b, System.StringComparison.OrdinalIgnoreCase)) return true;
            if (System.Guid.TryParse(a, out System.Guid guidA) && System.Guid.TryParse(b, out System.Guid guidB))
            {
                return guidA == guidB;
            }
            return false;
        }

        private string GetPlayerDisplayNameAndTeam(string playerId, string localSessionId, out int teamId, out bool isLocal)
        {
            isLocal = IsSamePlayerId(playerId, localSessionId);
            teamId = isLocal ? localTeamId : 0;

            if (!string.IsNullOrEmpty(playerId))
            {
                foreach (var kvp in playerScoreMap)
                {
                    if (IsSamePlayerId(kvp.Key, playerId))
                    {
                        if (kvp.Value.TeamId > 0) teamId = kvp.Value.TeamId;
                        if (!string.IsNullOrEmpty(kvp.Value.DisplayName))
                            return kvp.Value.DisplayName;
                        break;
                    }
                }
            }

            string shortId = !string.IsNullOrEmpty(playerId) && playerId.Length > 6
                ? playerId.Substring(0, 6)
                : playerId;

            if (isLocal && !string.IsNullOrEmpty(NetworkManager.Instance?.Username))
            {
                return NetworkManager.Instance.Username;
            }

            return $"Player_{shortId}";
        }

        public void ProcessServerIngamePacket(IngamePacket packet, string localSessionId)
        {
            if (packet == null) return;

            switch (packet.Method)
            {
                case IngameType.Move:
                {
                    var move = MovePacket.Parser.ParseFrom(packet.Data);
                    string movePlayerId = move.PlayerId.ToStringUtf8();
                    if (string.IsNullOrEmpty(movePlayerId)) break;

                    Vector3 serverPos = new Vector3(move.OriginX, move.OriginY, move.OriginZ);
                    Vector3 serverDir = new Vector3(move.DirX, move.DirY, move.DirZ);

                    if (IsSamePlayerId(movePlayerId, localSessionId))
                    {
                        if (localPlayer != null && !localPlayer.HasSyncedServerSpawn)
                        {
                            Vector3 safeGroundPos = new Vector3(serverPos.x, Mathf.Max(serverPos.y, 0.05f), serverPos.z);
                            localPlayer.SyncServerPosition(safeGroundPos);
                        }
                    }
                    else
                    {
                        PlayerController remotePlayer = GetOrCreateNetworkPlayer(movePlayerId, serverPos);
                        if (remotePlayer != null)
                        {
                            Quaternion targetRot = remotePlayer.transform.rotation;
                            Vector3 flatDir = new Vector3(serverDir.x, 0f, serverDir.z);
                            if (flatDir.sqrMagnitude > 0.0001f)
                            {
                                targetRot = Quaternion.LookRotation(flatDir.normalized);
                            }
                            remotePlayer.UpdateNetworkTransform(serverPos, targetRot);
                        }
                    }
                    break;
                }

                case IngameType.Hit:
                {
                    var hit = HitPacket.Parser.ParseFrom(packet.Data);
                    string hitPlayerId = hit.HitPlayerId.ToStringUtf8();
                    string shooterId = hit.ShooterId.ToStringUtf8();

                    if (IsSamePlayerId(hitPlayerId, localSessionId))
                    {
                        if (uiController != null)
                        {
                            uiController.SetHealth(hit.CurrentHp, 100f);
                        }
                        if (hit.IsDead)
                        {
                            Debug.Log($"[GameManager] Local player was eliminated by {shooterId} (Deaths: {hit.Deaths})");
                        }
                    }

                    if (IsSamePlayerId(shooterId, localSessionId))
                    {
                        if (uiController != null)
                        {
                            uiController.ShowHitMarker(false);
                        }
                    }

                    if (networkPlayers.TryGetValue(hitPlayerId, out PlayerController hitRemote) && hitRemote != null)
                    {
                        hitRemote.UpdateRemoteHealth(hit.CurrentHp, 100f, hit.IsDead);
                    }

                    // 사망(Kill) 발생 시 즉시 오른쪽 상단 킬 로그 추가 및 중앙 상단 팀 스코어 갱신
                    if (hit.IsDead)
                    {
                        string killerName = GetPlayerDisplayNameAndTeam(shooterId, localSessionId, out int killerTeamId, out bool isKillerLocal);
                        string victimName = GetPlayerDisplayNameAndTeam(hitPlayerId, localSessionId, out int victimTeamId, out bool isVictimLocal);

                        if (playerScoreMap.TryGetValue(shooterId, out ScoreboardRowViewData killerRow))
                        {
                            killerRow.Kills += 1;
                            killerRow.Damage += hit.Damage;
                            playerScoreMap[shooterId] = killerRow;
                        }

                        if (playerScoreMap.TryGetValue(hitPlayerId, out ScoreboardRowViewData victimRow))
                        {
                            victimRow.Deaths = hit.Deaths > 0 ? hit.Deaths : (victimRow.Deaths + 1);
                            playerScoreMap[hitPlayerId] = victimRow;
                        }

                        if (killerTeamId == 1) lastTeamAScore += 1;
                        else if (killerTeamId == 2) lastTeamBScore += 1;

                        if (uiController != null)
                        {
                            uiController.UpdateTopTeamScoreHUD(lastTeamAScore, lastTeamBScore, localTeamId);
                            uiController.AddKillFeedEntry(
                                killerName,
                                killerTeamId,
                                isKillerLocal,
                                victimName,
                                victimTeamId,
                                isVictimLocal,
                                localTeamId);
                        }
                    }
                    break;
                }

                case IngameType.LagComp:
                {
                    var lagComp = LagCompPacket.Parser.ParseFrom(packet.Data);
                    string shooterId = lagComp.ShooterId.ToStringUtf8();

                    if (shooterId != localSessionId &&
                        networkPlayers.TryGetValue(shooterId, out PlayerController remoteShooter) &&
                        remoteShooter != null &&
                        remoteShooter.Shooter != null)
                    {
                        Vector3 targetPoint = new Vector3(
                            lagComp.OriginX + lagComp.DirX * 25f,
                            lagComp.OriginY + lagComp.DirY * 25f,
                            lagComp.OriginZ + lagComp.DirZ * 25f);
                        remoteShooter.Shooter.PlayRemoteFireEffect(targetPoint);
                    }
                    break;
                }

                case IngameType.Score:
                {
                    var scoreboard = ScoreboardPacket.Parser.ParseFrom(packet.Data);
                    if (scoreboard.WinningTeam > 0) lastWinningTeam = scoreboard.WinningTeam;

                    int sumTeamAKills = 0;
                    int sumTeamBKills = 0;

                    foreach (var entry in scoreboard.Scores)
                    {
                        string scorePlayerId = entry.PlayerId.ToStringUtf8();
                        if (string.IsNullOrEmpty(scorePlayerId)) continue;

                        bool isLocal = IsSamePlayerId(scorePlayerId, localSessionId);
                        string shortId = scorePlayerId.Length > 6 ? scorePlayerId.Substring(0, 6) : scorePlayerId;
                        string displayName = isLocal
                            ? (!string.IsNullOrEmpty(NetworkManager.Instance?.Username) ? NetworkManager.Instance.Username : $"Player_{shortId}")
                            : $"Player_{shortId}";

                        if (!playerScoreMap.TryGetValue(scorePlayerId, out ScoreboardRowViewData row))
                        {
                            row = new ScoreboardRowViewData
                            {
                                PlayerId = scorePlayerId,
                                DisplayName = displayName,
                                TeamId = entry.TeamId,
                                IsLocalPlayer = isLocal
                            };
                        }

                        if (entry.TeamId > 0) row.TeamId = entry.TeamId;
                        row.IsLocalPlayer = isLocal;
                        row.DisplayName = displayName;
                        row.Kills = entry.Kill;
                        row.Deaths = entry.Death;
                        row.Damage = entry.Damage;
                        playerScoreMap[scorePlayerId] = row;

                        if (row.TeamId == 1) sumTeamAKills += row.Kills;
                        else if (row.TeamId == 2) sumTeamBKills += row.Kills;

                        if (!isLocal &&
                            networkPlayers.TryGetValue(scorePlayerId, out PlayerController remotePlayer) &&
                            remotePlayer != null)
                        {
                            remotePlayer.SetPlayerName($"Player_{shortId} ({entry.Kill}K/{entry.Death}D)");
                        }
                    }

                    lastTeamAScore = scoreboard.TeamAScore > 0 ? scoreboard.TeamAScore : sumTeamAKills;
                    lastTeamBScore = scoreboard.TeamBScore > 0 ? scoreboard.TeamBScore : sumTeamBKills;

                    if (uiController != null)
                    {
                        uiController.UpdateTopTeamScoreHUD(lastTeamAScore, lastTeamBScore, localTeamId);
                    }

                    if (isMatchEnded)
                    {
                        RefreshEndGameScoreboardUI();
                    }
                    break;
                }

                case IngameType.MatchEnd:
                {
                    OnMatchEnded();
                    break;
                }
            }
        }

        public void OnMatchEnded()
        {
            if (isMatchEnded)
            {
                RefreshEndGameScoreboardUI();
                return;
            }

            isMatchEnded = true;
            Debug.Log("[GameManager] Match Ended! Displaying Scoreboard and disabling local player controls.");

            if (localPlayer != null)
            {
                localPlayer.SetControlEnabled(false);
            }

            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;

            RefreshEndGameScoreboardUI();
        }

        private void RefreshEndGameScoreboardUI()
        {
            EnsureEnvironmentAndUI();
            if (uiController == null) return;

            var rows = new List<ScoreboardRowViewData>(playerScoreMap.Values);
            rows.Sort((a, b) =>
            {
                int teamCmp = a.TeamId.CompareTo(b.TeamId);
                if (teamCmp != 0) return teamCmp;
                int killCmp = b.Kills.CompareTo(a.Kills);
                if (killCmp != 0) return killCmp;
                return a.Deaths.CompareTo(b.Deaths);
            });

            int computedTeamA = 0;
            int computedTeamB = 0;
            foreach (var r in rows)
            {
                if (r.TeamId == 1) computedTeamA += r.Kills;
                else if (r.TeamId == 2) computedTeamB += r.Kills;
            }

            int finalTeamAScore = lastTeamAScore > 0 ? lastTeamAScore : computedTeamA;
            int finalTeamBScore = lastTeamBScore > 0 ? lastTeamBScore : computedTeamB;
            int finalWinningTeam = lastWinningTeam;
            if (finalWinningTeam == 0)
            {
                if (finalTeamAScore > finalTeamBScore) finalWinningTeam = 1;
                else if (finalTeamBScore > finalTeamAScore) finalWinningTeam = 2;
                else finalWinningTeam = 3;
            }

            uiController.ShowEndGameScoreboard(
                rows,
                localTeamId,
                finalWinningTeam,
                finalTeamAScore,
                finalTeamBScore,
                () => NetworkManager.EnsureInstance().ReturnToAuthLobby("MenuScene"));
        }

        private PlayerController GetOrCreateNetworkPlayer(string remoteSessionId, Vector3 initialPos)
        {
            if (networkPlayers.TryGetValue(remoteSessionId, out PlayerController existing) && existing != null)
            {
                return existing;
            }

            int newId = activePlayers.Count;
            GameObject remoteObj = playerPrefab != null
                ? Instantiate(playerPrefab, initialPos, Quaternion.identity)
                : CreateDynamicPlayerInstance(initialPos, Quaternion.identity);

            remoteObj.name = $"NetworkPlayer_{remoteSessionId}";
            PlayerController remotePlayer = remoteObj.GetComponent<PlayerController>();

            if (remotePlayer != null)
            {
                string shortId = remoteSessionId.Length > 6 ? remoteSessionId.Substring(0, 6) : remoteSessionId;
                remotePlayer.Initialize(newId, isLocal: false, $"Player_{shortId}");
                activePlayers.Add(remotePlayer);
                networkPlayers[remoteSessionId] = remotePlayer;
                UpdatePlayerCountUI();
                Debug.Log($"[GameManager] Spawned Network Player '{remoteSessionId}' at {initialPos}. Active players: {activePlayers.Count}/{maxPlayers}");
            }

            return remotePlayer;
        }

        private void UpdatePlayerCountUI()
        {
            if (uiController != null)
            {
                uiController.UpdatePlayerCount(activePlayers.Count, maxPlayers);
            }
        }
    }

}
