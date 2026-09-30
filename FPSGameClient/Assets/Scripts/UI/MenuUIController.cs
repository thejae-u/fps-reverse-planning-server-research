using System;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;
using FPSGame.Network;

namespace FPSGame.UI
{
    public class MenuUIController : MonoBehaviour
    {
        [Header("Scene Settings")]
        [SerializeField] private string gameSceneName = "GameScene";
        [SerializeField] private string defaultAuthServerUrl = "http://localhost:18080";

        [Header("Server Config UI")]
        [SerializeField] private InputField serverUrlInput;
        [SerializeField] private Text statusBannerText;

        [Header("Auth Panel UI")]
        [SerializeField] private GameObject authPanel;
        [SerializeField] private InputField usernameInput;
        [SerializeField] private InputField passwordInput;
        [SerializeField] private Button loginButton;
        [SerializeField] private Button registerButton;

        [Header("Lobby / Matchmaking Panel UI")]
        [SerializeField] private GameObject lobbyPanel;
        [SerializeField] private Text userInfoText;
        [SerializeField] private Text matchStateText;
        [SerializeField] private Button startMatchButton;
        [SerializeField] private Button cancelMatchButton;
        [SerializeField] private Button devBotMatchButton;
        [SerializeField] private Button logoutButton;

        private AuthApiClient _authClient;
        private CancellationTokenSource _matchmakingCts;
        private bool _isBusy = false;
        private bool _isMatching = false;
        private bool _usedDevBotFill = false;
        private float _matchStartTime = 0f;

        private void Awake()
        {
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;

            NetworkManager netMgr = NetworkManager.EnsureInstance();
            string effectiveUrl = !string.IsNullOrWhiteSpace(netMgr.AuthServerUrl)
                ? netMgr.AuthServerUrl
                : defaultAuthServerUrl;

            _authClient = new AuthApiClient(effectiveUrl);

            if (!string.IsNullOrEmpty(netMgr.JwtToken))
            {
                _authClient.RestoreSession(netMgr.UserId, netMgr.Username, netMgr.JwtToken, effectiveUrl);
            }

            EnsureUIElements();
            BindUIEvents();
            if (serverUrlInput != null)
            {
                serverUrlInput.text = effectiveUrl;
            }
            RefreshPanelState();

            if (_authClient.IsAuthenticated)
            {
                SetStatus($"게임 종료 후 AuthServer 로비로 복귀했습니다 ({_authClient.Username}님). 다시 매칭을 시작할 수 있습니다.", new Color(0.35f, 1f, 0.55f));
                if (matchStateText != null)
                {
                    matchStateText.text = "대기 상태: 매칭 시작 버튼을 눌러 큐에 진입하세요.";
                }
            }
            else
            {
                SetStatus("AuthServer에 로그인하거나 새 계정을 등록하세요.", new Color(0.8f, 0.88f, 1f));
            }
        }

        private void Update()
        {
            if (_isMatching && matchStateText != null)
            {
                int elapsedSec = Mathf.FloorToInt(Time.realtimeSinceStartup - _matchStartTime);
                matchStateText.text = $"매칭 대기 중 (WebSocket 연결됨)... ({elapsedSec / 60:00}:{elapsedSec % 60:00})";
            }
        }

        private void OnDestroy()
        {
            CancelMatchmakingWait();
            _ = _authClient?.DisconnectMatchHubWebSocketAsync();
        }

        private void BindUIEvents()
        {
            if (serverUrlInput != null)
            {
                serverUrlInput.text = defaultAuthServerUrl;
                serverUrlInput.onEndEdit.RemoveAllListeners();
                serverUrlInput.onEndEdit.AddListener(url => _authClient.SetBaseUrl(url));
            }

            if (loginButton != null)
            {
                loginButton.onClick.RemoveAllListeners();
                loginButton.onClick.AddListener(() => _ = OnLoginClickedAsync());
            }

            if (registerButton != null)
            {
                registerButton.onClick.RemoveAllListeners();
                registerButton.onClick.AddListener(() => _ = OnRegisterClickedAsync());
            }

            if (startMatchButton != null)
            {
                startMatchButton.onClick.RemoveAllListeners();
                startMatchButton.onClick.AddListener(() => _ = OnStartMatchClickedAsync());
            }

            if (cancelMatchButton != null)
            {
                cancelMatchButton.onClick.RemoveAllListeners();
                cancelMatchButton.onClick.AddListener(() => _ = OnCancelMatchClickedAsync());
            }

            if (devBotMatchButton != null)
            {
                devBotMatchButton.onClick.RemoveAllListeners();
                devBotMatchButton.onClick.AddListener(() => _ = OnDevBotMatchClickedAsync());
            }

            if (logoutButton != null)
            {
                logoutButton.onClick.RemoveAllListeners();
                logoutButton.onClick.AddListener(() => _ = OnLogoutClickedAsync());
            }
        }

        private void RefreshPanelState()
        {
            bool loggedIn = _authClient != null && _authClient.IsAuthenticated;

            if (authPanel != null) authPanel.SetActive(!loggedIn);
            if (lobbyPanel != null) lobbyPanel.SetActive(loggedIn);

            if (loggedIn && userInfoText != null)
            {
                userInfoText.text = $"접속 계정: {_authClient.Username}  (ID: {_authClient.UserId})";
            }

            if (startMatchButton != null) startMatchButton.interactable = !_isBusy && !_isMatching;
            if (cancelMatchButton != null) cancelMatchButton.interactable = _isMatching;
            if (devBotMatchButton != null) devBotMatchButton.interactable = !_isBusy;
            if (loginButton != null) loginButton.interactable = !_isBusy;
            if (registerButton != null) registerButton.interactable = !_isBusy;
        }

        private async Task OnRegisterClickedAsync()
        {
            if (_isBusy) return;
            SyncBaseUrl();

            string username = usernameInput != null ? usernameInput.text : string.Empty;
            string password = passwordInput != null ? passwordInput.text : string.Empty;

            _isBusy = true;
            RefreshPanelState();
            SetStatus("회원가입 요청 중...", Color.yellow);

            try
            {
                var result = await _authClient.RegisterAsync(username, password, destroyCancellationToken);
                if (result.IsSuccess)
                {
                    SetStatus($"회원가입 완료 ({username})! 이제 로그인 버튼을 눌러주세요.", new Color(0.35f, 1f, 0.55f));
                }
                else
                {
                    SetStatus($"회원가입 실패: {result.ErrorMessage}", new Color(1f, 0.45f, 0.45f));
                }
            }
            catch (OperationCanceledException) { }
            finally
            {
                _isBusy = false;
                RefreshPanelState();
            }
        }

        private async Task OnLoginClickedAsync()
        {
            if (_isBusy) return;
            SyncBaseUrl();

            string username = usernameInput != null ? usernameInput.text : string.Empty;
            string password = passwordInput != null ? passwordInput.text : string.Empty;

            _isBusy = true;
            RefreshPanelState();
            SetStatus("로그인 요청 중...", Color.yellow);

            try
            {
                var result = await _authClient.LoginAsync(username, password, destroyCancellationToken);
                if (result.IsSuccess && result.Data != null)
                {
                    NetworkManager.EnsureInstance().SetUserSession(
                        result.Data.userId,
                        result.Data.username,
                        result.Data.token,
                        _authClient.BaseUrl);

                    SetStatus($"환영합니다, {result.Data.username}님! 매칭을 시작할 수 있습니다.", new Color(0.35f, 1f, 0.55f));
                    if (matchStateText != null)
                    {
                        matchStateText.text = "대기 상태: 매칭 시작 버튼을 눌러 큐에 진입하세요.";
                    }
                }
                else
                {
                    SetStatus($"로그인 실패: {result.ErrorMessage}", new Color(1f, 0.45f, 0.45f));
                }
            }
            catch (OperationCanceledException) { }
            finally
            {
                _isBusy = false;
                RefreshPanelState();
            }
        }

        private async Task OnStartMatchClickedAsync()
        {
            if (_isBusy || _isMatching) return;
            SyncBaseUrl();

            _isBusy = true;
            RefreshPanelState();
            SetStatus("MatchHub WebSocket 연결 및 매칭 큐 진입 중...", Color.yellow);

            try
            {
                // 1. SignalR MatchHub WebSocket(/hubs/match) 연결
                var wsRes = await _authClient.ConnectMatchHubWebSocketAsync(destroyCancellationToken);
                if (!wsRes.IsSuccess)
                {
                    SetStatus(wsRes.ErrorMessage, new Color(1f, 0.45f, 0.45f));
                    return;
                }

                // 2. HTTP POST /match/join 1회 호출
                var joinRes = await _authClient.JoinMatchAsync(destroyCancellationToken);
                if (!joinRes.IsSuccess)
                {
                    SetStatus($"매칭 참가 실패: {joinRes.ErrorMessage}", new Color(1f, 0.45f, 0.45f));
                    return;
                }

                _isBusy = false;
                StartMatchmakingWebSocketWait();
            }
            catch (OperationCanceledException) { }
            finally
            {
                _isBusy = false;
                RefreshPanelState();
            }
        }

        private async Task OnCancelMatchClickedAsync()
        {
            if (!_isMatching) return;

            CancelMatchmakingWait();
            SetStatus("매칭 취소 요청 중...", Color.yellow);

            try
            {
                var cancelRes = await _authClient.CancelMatchAsync(destroyCancellationToken);
                if (cancelRes.IsSuccess)
                {
                    SetStatus("매칭 대기가 취소되었습니다.", new Color(0.85f, 0.85f, 0.85f));
                    if (matchStateText != null) matchStateText.text = "매칭 취소됨";
                }
                else
                {
                    SetStatus($"매칭 취소 응답: {cancelRes.ErrorMessage}", new Color(1f, 0.6f, 0.4f));
                }
            }
            catch (OperationCanceledException) { }
            finally
            {
                RefreshPanelState();
            }
        }

        private async Task OnDevBotMatchClickedAsync()
        {
            if (_isBusy) return;
            SyncBaseUrl();

            _isBusy = true;
            RefreshPanelState();

            try
            {
                if (!_isMatching)
                {
                    SetStatus("[Dev] MatchHub WebSocket 연결 및 내 계정 큐 등록 중...", Color.yellow);
                    var wsRes = await _authClient.ConnectMatchHubWebSocketAsync(destroyCancellationToken);
                    if (!wsRes.IsSuccess)
                    {
                        SetStatus(wsRes.ErrorMessage, new Color(1f, 0.45f, 0.45f));
                        return;
                    }

                    var joinRes = await _authClient.JoinMatchAsync(destroyCancellationToken);
                    if (!joinRes.IsSuccess && joinRes.StatusCode != 409)
                    {
                        SetStatus($"[Dev] 내 계정 큐 등록 실패: {joinRes.ErrorMessage}", new Color(1f, 0.45f, 0.45f));
                        return;
                    }

                    StartMatchmakingWebSocketWait();
                }

                _usedDevBotFill = true;
                var botRes = await _authClient.TriggerTenPlayerBotFillAsync(
                    progressMsg => SetStatus(progressMsg, new Color(0.5f, 0.9f, 1f)),
                    destroyCancellationToken);

                if (botRes.IsSuccess)
                {
                    SetStatus("[Dev] 10인 큐 등록 완료! WebSocket으로 Dedicated Server 정보 수신 대기 중...", new Color(0.35f, 1f, 0.55f));
                }
                else
                {
                    SetStatus($"[Dev] 봇 투입 실패: {botRes.ErrorMessage}", new Color(1f, 0.45f, 0.45f));
                }
            }
            catch (OperationCanceledException) { }
            finally
            {
                _isBusy = false;
                RefreshPanelState();
            }
        }

        private async Task OnLogoutClickedAsync()
        {
            CancelMatchmakingWait();
            await _authClient.ClearAuthAsync();
            NetworkManager.EnsureInstance().SetUserSession(null, null, null);
            SetStatus("로그아웃 되었습니다.", new Color(0.8f, 0.88f, 1f));
            RefreshPanelState();
        }

        private void StartMatchmakingWebSocketWait()
        {
            CancelMatchmakingWait();

            _isMatching = true;
            _matchStartTime = Time.realtimeSinceStartup;
            _matchmakingCts = CancellationTokenSource.CreateLinkedTokenSource(destroyCancellationToken);
            RefreshPanelState();

            SetStatus("매칭 큐 등록 완료! WebSocket(/hubs/match)에서 매칭 이벤트를 대기합니다.", new Color(0.35f, 1f, 0.55f));
            _ = WaitForMatchViaWebSocketAsync(_matchmakingCts.Token);
        }

        private void CancelMatchmakingWait()
        {
            _isMatching = false;
            _usedDevBotFill = false;
            if (_matchmakingCts != null)
            {
                _matchmakingCts.Cancel();
                _matchmakingCts.Dispose();
                _matchmakingCts = null;
            }
        }

        private async Task WaitForMatchViaWebSocketAsync(CancellationToken ct)
        {
            try
            {
                MatchedHubEventDto matched = await _authClient.WaitForMatchedEventViaWebSocketAsync(
                    hubMsg => SetStatus(hubMsg, new Color(0.5f, 0.95f, 1f)),
                    ct);

                if (matched == null) return;

                string effectiveAddress = matched.GetEffectiveServerAddress();
                if (!AuthApiClient.TryParseServerAddress(effectiveAddress, out string serverIp, out int serverPort))
                {
                    SetStatus($"서버 주소 파싱 실패: {effectiveAddress}", new Color(1f, 0.45f, 0.45f));
                    return;
                }

                int tcpPort = matched.tcpPort > 0 ? matched.tcpPort : serverPort;
                int udpPort = matched.udpPort > 0 ? matched.udpPort : tcpPort;
                int botClientCount = _usedDevBotFill ? 9 : 0;
                _isMatching = false;
                _usedDevBotFill = false;
                RefreshPanelState();

                string matchInfo = $"매칭 성사! Dedicated Server ({serverIp} TCP:{tcpPort} / UDP:{udpPort}) 접속 중...";
                SetStatus(matchInfo, new Color(0.3f, 1f, 0.5f));
                if (matchStateText != null) matchStateText.text = matchInfo;

                await _authClient.DisconnectMatchHubWebSocketAsync();
                await NetworkManager.EnsureInstance().TransitionToGameAndConnectAsync(
                    serverIp,
                    tcpPort,
                    udpPort,
                    matched.matchId,
                    gameSceneName,
                    botClientCount,
                    _authClient.LastCreatedBotUserIds);
            }
            catch (OperationCanceledException)
            {
                // Cancelled by user or scene destroy
            }
            catch (Exception ex)
            {
                if (!ct.IsCancellationRequested)
                {
                    _isMatching = false;
                    RefreshPanelState();
                    SetStatus($"매칭 WebSocket 오류: {ex.Message}", new Color(1f, 0.45f, 0.45f));
                }
            }
        }

        private void SyncBaseUrl()
        {
            if (serverUrlInput != null && !string.IsNullOrWhiteSpace(serverUrlInput.text))
            {
                _authClient.SetBaseUrl(serverUrlInput.text);
            }
        }

        private void SetStatus(string message, Color color)
        {
            if (statusBannerText != null)
            {
                statusBannerText.text = message;
                statusBannerText.color = color;
            }
            Debug.Log($"[MenuUI] {message}");
        }

        #region Runtime UI Fallback Builder

        private void EnsureUIElements()
        {
            if (authPanel != null && lobbyPanel != null) return;

            if (FindAnyObjectByType<EventSystem>() == null)
            {
                var esObj = new GameObject("EventSystem");
                esObj.AddComponent<EventSystem>();
#if ENABLE_INPUT_SYSTEM
                esObj.AddComponent<UnityEngine.InputSystem.UI.InputSystemUIInputModule>();
#else
                esObj.AddComponent<StandaloneInputModule>();
#endif
            }

            Canvas canvas = GetComponentInParent<Canvas>();
            if (canvas == null)
            {
                GameObject canvasObj = new GameObject("MenuCanvas");
                canvas = canvasObj.AddComponent<Canvas>();
                canvas.renderMode = RenderMode.ScreenSpaceOverlay;
                var scaler = canvasObj.AddComponent<CanvasScaler>();
                scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
                scaler.referenceResolution = new Vector2(1920, 1080);
                canvasObj.AddComponent<GraphicRaycaster>();
                transform.SetParent(canvasObj.transform, false);
            }

            Font font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (font == null) font = Font.CreateDynamicFontFromOSFont("Arial", 24);

            // Background
            GameObject bg = CreatePanel(canvas.transform, "Background", new Color(0.08f, 0.1f, 0.14f, 1f), Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);

            // Center Card
            GameObject card = CreatePanel(bg.transform, "MainCard", new Color(0.14f, 0.17f, 0.23f, 0.96f),
                new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), new Vector2(0, 0), new Vector2(680, 620));

            CreateLabel(card.transform, "TitleText", "FPS REVERSE-PLANNING CLIENT", 30, new Vector2(0, 250), new Vector2(600, 50), new Color(0.95f, 0.97f, 1f), font);

            // Auth Server URL Row
            CreateLabel(card.transform, "ServerUrlLabel", "AuthServer URL:", 18, new Vector2(-210, 195), new Vector2(160, 36), new Color(0.75f, 0.82f, 0.9f), font, TextAnchor.MiddleLeft);
            serverUrlInput = CreateInputField(card.transform, "ServerUrlInput", defaultAuthServerUrl, new Vector2(60, 195), new Vector2(360, 38), false, font);

            // Status Banner
            statusBannerText = CreateLabel(card.transform, "StatusBanner", "Ready", 18, new Vector2(0, 140), new Vector2(580, 44), Color.white, font);

            // 1. Auth Panel
            authPanel = CreatePanel(card.transform, "AuthPanel", new Color(0, 0, 0, 0),
                new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), new Vector2(0, -50), new Vector2(580, 360));

            CreateLabel(authPanel.transform, "UserLabel", "아이디 (Username)", 18, new Vector2(0, 115), new Vector2(460, 30), new Color(0.8f, 0.85f, 0.95f), font, TextAnchor.MiddleLeft);
            usernameInput = CreateInputField(authPanel.transform, "UsernameInput", "player1", new Vector2(0, 75), new Vector2(460, 44), false, font);

            CreateLabel(authPanel.transform, "PassLabel", "비밀번호 (Password)", 18, new Vector2(0, 15), new Vector2(460, 30), new Color(0.8f, 0.85f, 0.95f), font, TextAnchor.MiddleLeft);
            passwordInput = CreateInputField(authPanel.transform, "PasswordInput", "password123!", new Vector2(0, -25), new Vector2(460, 44), true, font);

            loginButton = CreateButton(authPanel.transform, "LoginButton", "로그인 (LOGIN)", new Vector2(-115, -110), new Vector2(215, 52), new Color(0.2f, 0.58f, 0.95f), font);
            registerButton = CreateButton(authPanel.transform, "RegisterButton", "회원가입 (REGISTER)", new Vector2(115, -110), new Vector2(215, 52), new Color(0.28f, 0.35f, 0.48f), font);

            // 2. Lobby Panel
            lobbyPanel = CreatePanel(card.transform, "LobbyPanel", new Color(0, 0, 0, 0),
                new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), new Vector2(0, -50), new Vector2(580, 360));

            userInfoText = CreateLabel(lobbyPanel.transform, "UserInfoText", "접속 계정: -", 20, new Vector2(0, 115), new Vector2(520, 36), new Color(0.6f, 0.9f, 1f), font);
            matchStateText = CreateLabel(lobbyPanel.transform, "MatchStateText", "대기 상태", 22, new Vector2(0, 55), new Vector2(520, 46), new Color(1f, 0.9f, 0.4f), font);

            startMatchButton = CreateButton(lobbyPanel.transform, "StartMatchButton", "매칭 시작 (FIND MATCH)", new Vector2(-120, -20), new Vector2(225, 54), new Color(0.18f, 0.72f, 0.42f), font);
            cancelMatchButton = CreateButton(lobbyPanel.transform, "CancelMatchButton", "매칭 취소 (CANCEL)", new Vector2(120, -20), new Vector2(225, 54), new Color(0.82f, 0.28f, 0.28f), font);

            devBotMatchButton = CreateButton(lobbyPanel.transform, "DevBotMatchButton", "[Dev] 10인 매칭 즉시 성사 (Bot 9명 큐 투입)", new Vector2(0, -90), new Vector2(465, 48), new Color(0.52f, 0.35f, 0.85f), font);
            logoutButton = CreateButton(lobbyPanel.transform, "LogoutButton", "로그아웃 (LOGOUT)", new Vector2(0, -152), new Vector2(220, 40), new Color(0.3f, 0.33f, 0.4f), font);
        }

        private static GameObject CreatePanel(Transform parent, string name, Color color, Vector2 anchorMin, Vector2 anchorMax, Vector2 anchoredPos, Vector2 sizeDelta)
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

        private static Text CreateLabel(Transform parent, string name, string text, int fontSize, Vector2 pos, Vector2 size, Color color, Font font, TextAnchor align = TextAnchor.MiddleCenter)
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

        private static InputField CreateInputField(Transform parent, string name, string defaultText, Vector2 pos, Vector2 size, bool isPassword, Font font)
        {
            GameObject go = CreatePanel(parent, name, new Color(0.09f, 0.11f, 0.16f, 1f), new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), pos, size);
            InputField input = go.AddComponent<InputField>();

            GameObject textObj = new GameObject("Text");
            textObj.transform.SetParent(go.transform, false);
            RectTransform textRt = textObj.AddComponent<RectTransform>();
            textRt.anchorMin = Vector2.zero;
            textRt.anchorMax = Vector2.one;
            textRt.offsetMin = new Vector2(14, 4);
            textRt.offsetMax = new Vector2(-14, -4);

            Text txt = textObj.AddComponent<Text>();
            txt.font = font;
            txt.fontSize = 20;
            txt.color = Color.white;
            txt.alignment = TextAnchor.MiddleLeft;
            txt.supportRichText = false;

            input.textComponent = txt;
            input.text = defaultText;
            if (isPassword)
            {
                input.contentType = InputField.ContentType.Password;
            }

            return input;
        }

        private static Button CreateButton(Transform parent, string name, string label, Vector2 pos, Vector2 size, Color bgColor, Font font)
        {
            GameObject go = CreatePanel(parent, name, bgColor, new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), pos, size);
            Button btn = go.AddComponent<Button>();
            btn.targetGraphic = go.GetComponent<Image>();

            CreateLabel(go.transform, "Label", label, 18, Vector2.zero, size, Color.white, font);
            return btn;
        }

        #endregion
    }
}
