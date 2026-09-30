using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;
using FPSGame.Player;

namespace FPSGame.UI
{
    public struct ScoreboardRowViewData
    {
        public string PlayerId;
        public string DisplayName;
        public int TeamId;
        public int Kills;
        public int Deaths;
        public int Damage;
        public bool IsLocalPlayer;
    }

    public class FPSUIController : MonoBehaviour
    {
        [Header("Crosshair Elements")]
        [SerializeField] private Image crosshairCenter;
        [SerializeField] private Image hitMarkerImage;

        [Header("HUD Text Elements")]
        [SerializeField] private Text ammoText;
        [SerializeField] private Text healthText;
        [SerializeField] private Text playerCountText;
        [SerializeField] private Text guideText;

        private Coroutine hitMarkerCoroutine;

        // 상단 중앙 팀 스코어 HUD
        private GameObject topScoreHudRoot;
        private Text teamALabelText;
        private Text teamAScoreText;
        private Text teamBLabelText;
        private Text teamBScoreText;
        private Image teamABarAccent;
        private Image teamBBarAccent;

        // 우측 상단 킬 로그(Kill Feed) 컨테이너
        private RectTransform killFeedContainer;
        private readonly List<KillFeedItem> activeKillFeedItems = new List<KillFeedItem>();
        private const int MaxKillFeedCount = 6;
        private const float KillFeedDuration = 5.0f;

        private class KillFeedItem
        {
            public GameObject RootObj;
            public RectTransform Rect;
            public Image BgImage;
            public Image AccentBar;
            public Text LogText;
            public float CreatedTime;
        }

        // 게임 종료 스코어보드 모달
        private GameObject scoreboardModalRoot;
        private Text scoreboardResultTitleText;
        private Text scoreboardTeamScoreText;
        private Transform scoreboardRowContainer;
        private Button scoreboardConfirmButton;
        private Action onConfirmCallback;

        private void Awake()
        {
            if (ammoText == null) ammoText = transform.Find("AmmoText")?.GetComponent<Text>();
            if (healthText == null) healthText = transform.Find("HealthText")?.GetComponent<Text>();
            if (playerCountText == null) playerCountText = transform.Find("PlayerCountText")?.GetComponent<Text>();
            if (guideText == null) guideText = transform.Find("GuideText")?.GetComponent<Text>();
            if (hitMarkerImage == null) hitMarkerImage = transform.Find("HitMarker")?.GetComponent<Image>();
        }

        public void BindPlayer(PlayerController player)
        {
            if (player == null) return;

            PlayerShooter shooter = player.Shooter;
            if (shooter != null)
            {
                shooter.OnAmmoChanged += UpdateAmmoUI;
                shooter.OnHitTarget += ShowHitMarker;
                UpdateAmmoUI(shooter.CurrentAmmo, shooter.ReserveAmmo);
            }

            SetHealth(100f, 100f);
            EnsureIngameScoreAndKillFeedHUD();
            UpdateTopTeamScoreHUD(0, 0, player.TeamId);
        }

        public void UpdatePlayerCount(int currentCount, int maxCount)
        {
            if (playerCountText != null)
            {
                playerCountText.text = $"Players: {currentCount} / {maxCount} (Press F1 to add dummy)";
            }
        }

        public void UpdateAmmoUI(int current, int reserve)
        {
            if (ammoText != null)
            {
                string reserveDisplay = reserve == -1 ? "∞" : reserve.ToString();
                ammoText.text = $"{current} / {reserveDisplay}";
            }
        }

        public void SetHealth(float current, float max)
        {
            if (healthText != null)
            {
                healthText.text = $"HP: {Mathf.CeilToInt(current)} / {Mathf.CeilToInt(max)}";
            }
        }

        public void ShowHitMarker(bool isHeadshot)
        {
            if (hitMarkerImage == null) return;

            if (hitMarkerCoroutine != null)
            {
                StopCoroutine(hitMarkerCoroutine);
            }

            hitMarkerCoroutine = StartCoroutine(HitMarkerRoutine(isHeadshot));
        }

        private IEnumerator HitMarkerRoutine(bool isHeadshot)
        {
            hitMarkerImage.enabled = true;
            hitMarkerImage.color = isHeadshot ? Color.red : Color.white;

            yield return new WaitForSeconds(0.12f);

            hitMarkerImage.enabled = false;
        }

        private void Start()
        {
            if (hitMarkerImage != null)
            {
                hitMarkerImage.enabled = false;
            }

            if (guideText != null)
            {
                guideText.text = "[WASD] 이동  |  [Shift] 달리기  |  [Space] 점프\n[좌클릭] 사격  |  [R] 재장전  |  [ESC] 커서 잠금 토글\n[F1] 리모트 플레이어 더미 소환 (최대 10명 멀티플레이어 환경 테스트)";
            }

            EnsureIngameScoreAndKillFeedHUD();
            UpdateTopTeamScoreHUD(0, 0, 0);
        }

        private void Update()
        {
            UpdateKillFeedLifetimes();
        }

        /// <summary>
        /// 중앙 상단의 팀별 킬 스코어 현황판(Team A vs Team B)을 갱신합니다.
        /// 아군 팀은 파란색, 적군 팀은 빨간색으로 구분 표시합니다.
        /// </summary>
        public void UpdateTopTeamScoreHUD(int teamAKills, int teamBKills, int localTeamId)
        {
            EnsureIngameScoreAndKillFeedHUD();

            Color allyColor = new Color(0.22f, 0.68f, 1f, 1f);  // 파란색 (아군)
            Color enemyColor = new Color(1f, 0.32f, 0.32f, 1f); // 빨간색 (적군)

            bool isTeamAAlly = (localTeamId == 0 || localTeamId == 1);
            Color teamAColor = isTeamAAlly ? allyColor : enemyColor;
            Color teamBColor = isTeamAAlly ? enemyColor : allyColor;

            string teamATag = localTeamId == 1 ? "TEAM A (아군)" : (localTeamId == 2 ? "TEAM A (적군)" : "TEAM A");
            string teamBTag = localTeamId == 2 ? "TEAM B (아군)" : (localTeamId == 1 ? "TEAM B (적군)" : "TEAM B");

            if (teamALabelText != null)
            {
                teamALabelText.text = teamATag;
                teamALabelText.color = teamAColor;
            }
            if (teamAScoreText != null)
            {
                teamAScoreText.text = teamAKills.ToString();
                teamAScoreText.color = Color.white;
            }
            if (teamABarAccent != null)
            {
                teamABarAccent.color = teamAColor;
            }

            if (teamBLabelText != null)
            {
                teamBLabelText.text = teamBTag;
                teamBLabelText.color = teamBColor;
            }
            if (teamBScoreText != null)
            {
                teamBScoreText.text = teamBKills.ToString();
                teamBScoreText.color = Color.white;
            }
            if (teamBBarAccent != null)
            {
                teamBBarAccent.color = teamBColor;
            }
        }

        /// <summary>
        /// 오른쪽 상단에 새로운 킬 로그(Kill Feed) 항목을 추가합니다.
        /// </summary>
        public void AddKillFeedEntry(
            string killerName,
            int killerTeamId,
            bool isKillerLocal,
            string victimName,
            int victimTeamId,
            bool isVictimLocal,
            int localTeamId)
        {
            EnsureIngameScoreAndKillFeedHUD();
            if (killFeedContainer == null) return;

            while (activeKillFeedItems.Count >= MaxKillFeedCount)
            {
                var oldest = activeKillFeedItems[0];
                if (oldest.RootObj != null) Destroy(oldest.RootObj);
                activeKillFeedItems.RemoveAt(0);
            }

            Font font = GetDefaultFont();

            bool isKillerAlly = isKillerLocal || (localTeamId > 0 && killerTeamId == localTeamId) || (localTeamId == 0 && killerTeamId == 1);
            bool isVictimAlly = isVictimLocal || (localTeamId > 0 && victimTeamId == localTeamId) || (localTeamId == 0 && victimTeamId == 1);

            string killerHex = isKillerLocal ? "#FFE866" : (isKillerAlly ? "#4DB8FF" : "#FF5959");
            string victimHex = isVictimLocal ? "#FFE866" : (isVictimAlly ? "#4DB8FF" : "#FF5959");

            Color accentColor = isKillerLocal
                ? new Color(1f, 0.88f, 0.32f, 1f)
                : (isKillerAlly ? new Color(0.25f, 0.70f, 1f, 1f) : new Color(1f, 0.35f, 0.35f, 1f));

            GameObject itemObj = CreateUIPanel(
                killFeedContainer,
                "KillFeedEntry",
                new Color(0.06f, 0.09f, 0.14f, 0.86f),
                new Vector2(1f, 1f),
                new Vector2(1f, 1f),
                Vector2.zero,
                new Vector2(360f, 34f));

            RectTransform itemRt = itemObj.GetComponent<RectTransform>();
            itemRt.pivot = new Vector2(1f, 1f);

            // 좌측 팀 컬러 강조 바
            GameObject accentObj = CreateUIPanel(
                itemObj.transform,
                "AccentBar",
                accentColor,
                new Vector2(0f, 0f),
                new Vector2(0f, 1f),
                new Vector2(2.5f, 0f),
                new Vector2(5f, 0f));

            string killerTag = isKillerLocal ? $"{killerName} [나]" : killerName;
            string victimTag = isVictimLocal ? $"{victimName} [나]" : victimName;
            string richLog = $"<color={killerHex}><b>{killerTag}</b></color>   <color=#FFB84D>▶ 처치 ▶</color>   <color={victimHex}><b>{victimTag}</b></color>";

            Text logTxt = CreateUIText(
                itemObj.transform,
                "KillLogText",
                richLog,
                14,
                new Vector2(4f, 0f),
                new Vector2(336f, 32f),
                Color.white,
                font,
                TextAnchor.MiddleRight);
            logTxt.supportRichText = true;

            var feedItem = new KillFeedItem
            {
                RootObj = itemObj,
                Rect = itemRt,
                BgImage = itemObj.GetComponent<Image>(),
                AccentBar = accentObj.GetComponent<Image>(),
                LogText = logTxt,
                CreatedTime = Time.time
            };

            activeKillFeedItems.Add(feedItem);
            RepositionKillFeedItems();
        }

        private void RepositionKillFeedItems()
        {
            // 최신 킬 로그가 맨 위(또는 순서대로 위에서 아래로) 정렬되도록 배치
            for (int i = 0; i < activeKillFeedItems.Count; i++)
            {
                var item = activeKillFeedItems[i];
                if (item.Rect != null)
                {
                    item.Rect.anchoredPosition = new Vector2(0f, -i * 38f);
                }
            }
        }

        private void UpdateKillFeedLifetimes()
        {
            if (activeKillFeedItems.Count == 0) return;

            bool removedAny = false;
            float now = Time.time;

            for (int i = activeKillFeedItems.Count - 1; i >= 0; i--)
            {
                var item = activeKillFeedItems[i];
                float age = now - item.CreatedTime;

                if (age >= KillFeedDuration)
                {
                    if (item.RootObj != null) Destroy(item.RootObj);
                    activeKillFeedItems.RemoveAt(i);
                    removedAny = true;
                }
                else if (age >= KillFeedDuration - 0.8f)
                {
                    float alpha = Mathf.Clamp01((KillFeedDuration - age) / 0.8f);
                    if (item.BgImage != null)
                    {
                        Color c = item.BgImage.color;
                        c.a = 0.86f * alpha;
                        item.BgImage.color = c;
                    }
                    if (item.AccentBar != null)
                    {
                        Color c = item.AccentBar.color;
                        c.a = alpha;
                        item.AccentBar.color = c;
                    }
                    if (item.LogText != null)
                    {
                        Color c = item.LogText.color;
                        c.a = alpha;
                        item.LogText.color = c;
                    }
                }
            }

            if (removedAny)
            {
                RepositionKillFeedItems();
            }
        }

        private void EnsureIngameScoreAndKillFeedHUD()
        {
            if (topScoreHudRoot != null && killFeedContainer != null) return;

            Canvas canvas = GetOrCreateMainCanvas();
            Font font = GetDefaultFont();

            if (topScoreHudRoot == null)
            {
                // 중앙 상단 팀 스코어 보드 바 (Top-Center)
                topScoreHudRoot = CreateUIPanel(
                    canvas.transform,
                    "TopCenterTeamScoreHUD",
                    new Color(0.06f, 0.08f, 0.13f, 0.88f),
                    new Vector2(0.5f, 1f),
                    new Vector2(0.5f, 1f),
                    new Vector2(0f, -14f),
                    new Vector2(460f, 64f));

                RectTransform hudRt = topScoreHudRoot.GetComponent<RectTransform>();
                hudRt.pivot = new Vector2(0.5f, 1f);

                // Team A 영역 (좌측)
                GameObject teamABox = CreateUIPanel(
                    topScoreHudRoot.transform,
                    "TeamABox",
                    new Color(0.10f, 0.14f, 0.22f, 0.95f),
                    new Vector2(0.5f, 0.5f),
                    new Vector2(0.5f, 0.5f),
                    new Vector2(-132f, 0f),
                    new Vector2(180f, 52f));

                GameObject teamAAccentObj = CreateUIPanel(
                    teamABox.transform,
                    "TeamAAccent",
                    new Color(0.22f, 0.68f, 1f, 1f),
                    new Vector2(0f, 0f),
                    new Vector2(1f, 0f),
                    new Vector2(0f, 2f),
                    new Vector2(0f, 4f));
                teamABarAccent = teamAAccentObj.GetComponent<Image>();

                teamALabelText = CreateUIText(
                    teamABox.transform,
                    "TeamALabel",
                    "TEAM A",
                    13,
                    new Vector2(-32f, 0f),
                    new Vector2(105f, 44f),
                    new Color(0.22f, 0.68f, 1f, 1f),
                    font,
                    TextAnchor.MiddleLeft);

                teamAScoreText = CreateUIText(
                    teamABox.transform,
                    "TeamAScore",
                    "0",
                    28,
                    new Vector2(54f, 0f),
                    new Vector2(64f, 48f),
                    Color.white,
                    font,
                    TextAnchor.MiddleCenter);

                // 중앙 VS & 목표 킬 배지
                GameObject centerBadge = CreateUIPanel(
                    topScoreHudRoot.transform,
                    "CenterVSBadge",
                    new Color(0.15f, 0.19f, 0.27f, 1f),
                    new Vector2(0.5f, 0.5f),
                    new Vector2(0.5f, 0.5f),
                    Vector2.zero,
                    new Vector2(72f, 52f));

                CreateUIText(
                    centerBadge.transform,
                    "VSText",
                    "VS",
                    16,
                    new Vector2(0f, 8f),
                    new Vector2(68f, 24f),
                    new Color(1f, 0.88f, 0.42f, 1f),
                    font,
                    TextAnchor.MiddleCenter);

                CreateUIText(
                    centerBadge.transform,
                    "TargetText",
                    "목표 30",
                    11,
                    new Vector2(0f, -11f),
                    new Vector2(68f, 18f),
                    new Color(0.75f, 0.82f, 0.92f, 1f),
                    font,
                    TextAnchor.MiddleCenter);

                // Team B 영역 (우측)
                GameObject teamBBox = CreateUIPanel(
                    topScoreHudRoot.transform,
                    "TeamBBox",
                    new Color(0.10f, 0.14f, 0.22f, 0.95f),
                    new Vector2(0.5f, 0.5f),
                    new Vector2(0.5f, 0.5f),
                    new Vector2(132f, 0f),
                    new Vector2(180f, 52f));

                GameObject teamBAccentObj = CreateUIPanel(
                    teamBBox.transform,
                    "TeamBAccent",
                    new Color(1f, 0.32f, 0.32f, 1f),
                    new Vector2(0f, 0f),
                    new Vector2(1f, 0f),
                    new Vector2(0f, 2f),
                    new Vector2(0f, 4f));
                teamBBarAccent = teamBAccentObj.GetComponent<Image>();

                teamBScoreText = CreateUIText(
                    teamBBox.transform,
                    "TeamBScore",
                    "0",
                    28,
                    new Vector2(-54f, 0f),
                    new Vector2(64f, 48f),
                    Color.white,
                    font,
                    TextAnchor.MiddleCenter);

                teamBLabelText = CreateUIText(
                    teamBBox.transform,
                    "TeamBLabel",
                    "TEAM B",
                    13,
                    new Vector2(32f, 0f),
                    new Vector2(105f, 44f),
                    new Color(1f, 0.32f, 0.32f, 1f),
                    font,
                    TextAnchor.MiddleRight);
            }

            if (killFeedContainer == null)
            {
                // 오른쪽 상단 킬 로그(Kill Feed) 컨테이너 (Top-Right)
                GameObject feedObj = new GameObject("TopRightKillFeedContainer");
                feedObj.transform.SetParent(canvas.transform, false);
                killFeedContainer = feedObj.AddComponent<RectTransform>();
                killFeedContainer.anchorMin = new Vector2(1f, 1f);
                killFeedContainer.anchorMax = new Vector2(1f, 1f);
                killFeedContainer.pivot = new Vector2(1f, 1f);
                killFeedContainer.anchoredPosition = new Vector2(-18f, -18f);
                killFeedContainer.sizeDelta = new Vector2(360f, 260f);
            }
        }

        private Canvas GetOrCreateMainCanvas()
        {
            Canvas canvas = GetComponentInParent<Canvas>();
            if (canvas == null)
            {
                canvas = FindAnyObjectByType<Canvas>();
            }
            if (canvas == null)
            {
                GameObject canvasObj = new GameObject("Scoreboard_Canvas_Runtime");
                canvas = canvasObj.AddComponent<Canvas>();
                canvas.renderMode = RenderMode.ScreenSpaceOverlay;
                var scaler = canvasObj.AddComponent<CanvasScaler>();
                scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
                scaler.referenceResolution = new Vector2(1920, 1080);
                canvasObj.AddComponent<GraphicRaycaster>();
            }
            else if (canvas.GetComponent<GraphicRaycaster>() == null)
            {
                canvas.gameObject.AddComponent<GraphicRaycaster>();
            }
            return canvas;
        }

        public void ShowEndGameScoreboard(
            IReadOnlyList<ScoreboardRowViewData> rows,
            int localTeamId,
            int winningTeam,
            int teamAScore,
            int teamBScore,
            Action onConfirm)
        {
            EnsureEventSystem();
            EnsureScoreboardModalUI();

            onConfirmCallback = onConfirm;
            scoreboardModalRoot.SetActive(true);
            if (topScoreHudRoot != null) topScoreHudRoot.SetActive(false);

            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;

            if (crosshairCenter != null) crosshairCenter.enabled = false;
            if (hitMarkerImage != null) hitMarkerImage.enabled = false;

            // 승리/패배/무승부 판정 텍스트
            string resultHeadline = "MATCH FINISHED";
            Color headlineColor = new Color(0.95f, 0.96f, 1f);
            if (winningTeam == 3)
            {
                resultHeadline = "무승부 (DRAW)";
                headlineColor = new Color(1f, 0.88f, 0.35f);
            }
            else if (localTeamId > 0 && winningTeam > 0)
            {
                if (localTeamId == winningTeam)
                {
                    resultHeadline = "승리 (VICTORY)";
                    headlineColor = new Color(0.25f, 0.78f, 1f);
                }
                else
                {
                    resultHeadline = "패배 (DEFEAT)";
                    headlineColor = new Color(1f, 0.35f, 0.35f);
                }
            }
            else if (winningTeam == 1 || winningTeam == 2)
            {
                resultHeadline = $"TEAM {(winningTeam == 1 ? "A" : "B")} VICTORY";
                headlineColor = new Color(0.35f, 0.9f, 1f);
            }

            if (scoreboardResultTitleText != null)
            {
                scoreboardResultTitleText.text = $"게임 종료  —  {resultHeadline}";
                scoreboardResultTitleText.color = headlineColor;
            }

            if (scoreboardTeamScoreText != null)
            {
                scoreboardTeamScoreText.text = $"TEAM A  [ {teamAScore} ]   vs   [ {teamBScore} ]  TEAM B";
            }

            // 기존 행 제거 후 재생성
            if (scoreboardRowContainer != null)
            {
                for (int i = scoreboardRowContainer.childCount - 1; i >= 0; i--)
                {
                    Destroy(scoreboardRowContainer.GetChild(i).gameObject);
                }

                Font font = GetDefaultFont();
                int rowIndex = 0;
                if (rows != null)
                {
                    foreach (var row in rows)
                    {
                        CreateScoreboardRow(scoreboardRowContainer, row, localTeamId, rowIndex, font);
                        rowIndex++;
                    }
                }
            }
        }

        private void EnsureEventSystem()
        {
            if (FindAnyObjectByType<EventSystem>() != null) return;

            GameObject esObj = new GameObject("EventSystem_Runtime");
            esObj.AddComponent<EventSystem>();
#if ENABLE_INPUT_SYSTEM
            esObj.AddComponent<UnityEngine.InputSystem.UI.InputSystemUIInputModule>();
#else
            esObj.AddComponent<StandaloneInputModule>();
#endif
        }

        private void EnsureScoreboardModalUI()
        {
            if (scoreboardModalRoot != null) return;

            Canvas canvas = GetComponentInParent<Canvas>();
            if (canvas == null)
            {
                canvas = FindAnyObjectByType<Canvas>();
            }
            if (canvas == null)
            {
                GameObject canvasObj = new GameObject("Scoreboard_Canvas_Runtime");
                canvas = canvasObj.AddComponent<Canvas>();
                canvas.renderMode = RenderMode.ScreenSpaceOverlay;
                var scaler = canvasObj.AddComponent<CanvasScaler>();
                scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
                scaler.referenceResolution = new Vector2(1920, 1080);
                canvasObj.AddComponent<GraphicRaycaster>();
            }
            else if (canvas.GetComponent<GraphicRaycaster>() == null)
            {
                canvas.gameObject.AddComponent<GraphicRaycaster>();
            }

            Font font = GetDefaultFont();

            // 전체 화면 반투명 어두운 배경 오버레이
            scoreboardModalRoot = CreateUIPanel(
                canvas.transform,
                "EndGameScoreboardModal",
                new Color(0.04f, 0.06f, 0.10f, 0.88f),
                Vector2.zero,
                Vector2.one,
                Vector2.zero,
                Vector2.zero);

            // 중앙 스코어보드 카드
            GameObject card = CreateUIPanel(
                scoreboardModalRoot.transform,
                "ScoreboardCard",
                new Color(0.11f, 0.14f, 0.20f, 0.98f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0.5f, 0.5f),
                Vector2.zero,
                new Vector2(920f, 680f));

            // 상단 결과 타이틀
            scoreboardResultTitleText = CreateUIText(
                card.transform,
                "ResultTitle",
                "게임 종료 — MATCH FINISHED",
                32,
                new Vector2(0f, 285f),
                new Vector2(840f, 50f),
                Color.white,
                font,
                TextAnchor.MiddleCenter);

            // 팀 총 킬 스코어
            scoreboardTeamScoreText = CreateUIText(
                card.transform,
                "TeamScoreText",
                "TEAM A [ 0 ]  vs  [ 0 ] TEAM B",
                22,
                new Vector2(0f, 238f),
                new Vector2(840f, 36f),
                new Color(0.82f, 0.90f, 1f),
                font,
                TextAnchor.MiddleCenter);

            // 테이블 컬럼 헤더 배경
            GameObject headerBar = CreateUIPanel(
                card.transform,
                "TableHeader",
                new Color(0.18f, 0.23f, 0.32f, 1f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0f, 190f),
                new Vector2(840f, 38f));

            CreateUIText(headerBar.transform, "ColTeam", "팀 (TEAM)", 16, new Vector2(-330f, 0f), new Vector2(140f, 34f), new Color(0.75f, 0.85f, 1f), font, TextAnchor.MiddleCenter);
            CreateUIText(headerBar.transform, "ColPlayer", "플레이어 (PLAYER)", 16, new Vector2(-95f, 0f), new Vector2(310f, 34f), new Color(0.75f, 0.85f, 1f), font, TextAnchor.MiddleLeft);
            CreateUIText(headerBar.transform, "ColKills", "처치 (K)", 16, new Vector2(125f, 0f), new Vector2(100f, 34f), new Color(0.75f, 0.85f, 1f), font, TextAnchor.MiddleCenter);
            CreateUIText(headerBar.transform, "ColDeaths", "사망 (D)", 16, new Vector2(235f, 0f), new Vector2(100f, 34f), new Color(0.75f, 0.85f, 1f), font, TextAnchor.MiddleCenter);
            CreateUIText(headerBar.transform, "ColDamage", "피해량 (DMG)", 16, new Vector2(345f, 0f), new Vector2(120f, 34f), new Color(0.75f, 0.85f, 1f), font, TextAnchor.MiddleCenter);

            // 플레이어 행 컨테이너
            GameObject rowContainerObj = CreateUIPanel(
                card.transform,
                "RowContainer",
                new Color(0.08f, 0.10f, 0.15f, 0.7f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0f, -15f),
                new Vector2(840f, 360f));
            scoreboardRowContainer = rowContainerObj.transform;

            // 하단 확인 버튼 (AuthServer 로비로 복귀)
            GameObject btnObj = CreateUIPanel(
                card.transform,
                "ConfirmReturnButton",
                new Color(0.18f, 0.58f, 0.96f, 1f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0f, -265f),
                new Vector2(320f, 56f));

            scoreboardConfirmButton = btnObj.AddComponent<Button>();
            scoreboardConfirmButton.targetGraphic = btnObj.GetComponent<Image>();
            CreateUIText(
                btnObj.transform,
                "ConfirmLabel",
                "확인 (AuthServer 로비로 돌아가기)",
                20,
                Vector2.zero,
                new Vector2(320f, 56f),
                Color.white,
                font,
                TextAnchor.MiddleCenter);

            scoreboardConfirmButton.onClick.RemoveAllListeners();
            scoreboardConfirmButton.onClick.AddListener(() =>
            {
                onConfirmCallback?.Invoke();
            });
        }

        private static void CreateScoreboardRow(
            Transform parent,
            ScoreboardRowViewData data,
            int localTeamId,
            int rowIndex,
            Font font)
        {
            float topY = 158f - (rowIndex * 34f);

            bool isAlly = (localTeamId > 0 && data.TeamId == localTeamId) || (localTeamId == 0 && data.TeamId == 1);
            Color rowBgColor = data.IsLocalPlayer
                ? new Color(0.22f, 0.34f, 0.52f, 0.95f)
                : (rowIndex % 2 == 0 ? new Color(0.13f, 0.16f, 0.22f, 0.85f) : new Color(0.10f, 0.13f, 0.18f, 0.85f));

            GameObject rowObj = CreateUIPanel(
                parent,
                $"Row_{rowIndex}",
                rowBgColor,
                new Vector2(0.5f, 0.5f),
                new Vector2(0.5f, 0.5f),
                new Vector2(0f, topY),
                new Vector2(832f, 31f));

            string teamName = data.TeamId == 1 ? "TEAM A" : (data.TeamId == 2 ? "TEAM B" : "-");
            Color teamBadgeColor = isAlly
                ? new Color(0.25f, 0.68f, 1f)   // 아군 파란색
                : new Color(1f, 0.35f, 0.35f);  // 적군 빨간색

            string displayName = data.IsLocalPlayer ? $"{data.DisplayName}  [나 / ME]" : data.DisplayName;
            Color nameColor = data.IsLocalPlayer ? new Color(1f, 0.95f, 0.45f) : Color.white;

            CreateUIText(rowObj.transform, "Team", teamName, 15, new Vector2(-330f, 0f), new Vector2(140f, 30f), teamBadgeColor, font, TextAnchor.MiddleCenter);
            CreateUIText(rowObj.transform, "Player", displayName, 15, new Vector2(-95f, 0f), new Vector2(310f, 30f), nameColor, font, TextAnchor.MiddleLeft);
            CreateUIText(rowObj.transform, "Kills", data.Kills.ToString(), 16, new Vector2(125f, 0f), new Vector2(100f, 30f), Color.white, font, TextAnchor.MiddleCenter);
            CreateUIText(rowObj.transform, "Deaths", data.Deaths.ToString(), 16, new Vector2(235f, 0f), new Vector2(100f, 30f), new Color(0.85f, 0.85f, 0.85f), font, TextAnchor.MiddleCenter);
            CreateUIText(rowObj.transform, "Damage", data.Damage.ToString(), 16, new Vector2(345f, 0f), new Vector2(120f, 30f), new Color(1f, 0.82f, 0.45f), font, TextAnchor.MiddleCenter);
        }

        private static Font GetDefaultFont()
        {
            Font font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (font == null) font = Font.CreateDynamicFontFromOSFont("Arial", 20);
            return font;
        }

        private static GameObject CreateUIPanel(
            Transform parent,
            string name,
            Color color,
            Vector2 anchorMin,
            Vector2 anchorMax,
            Vector2 anchoredPos,
            Vector2 sizeDelta)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);
            RectTransform rt = go.AddComponent<RectTransform>();
            rt.anchorMin = anchorMin;
            rt.anchorMax = anchorMax;
            rt.anchoredPosition = anchoredPos;
            rt.sizeDelta = sizeDelta;
            Image img = go.AddComponent<Image>();
            img.color = color;
            return go;
        }

        private static Text CreateUIText(
            Transform parent,
            string name,
            string text,
            int fontSize,
            Vector2 pos,
            Vector2 size,
            Color color,
            Font font,
            TextAnchor align)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);
            RectTransform rt = go.AddComponent<RectTransform>();
            rt.anchoredPosition = pos;
            rt.sizeDelta = size;
            Text txt = go.AddComponent<Text>();
            txt.font = font;
            txt.fontSize = fontSize;
            txt.color = color;
            txt.alignment = align;
            txt.text = text;
            return txt;
        }
    }
}
