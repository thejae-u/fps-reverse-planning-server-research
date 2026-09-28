using System;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Networking;

namespace FPSGame.Network
{
    [Serializable]
    public class AuthRequestDto
    {
        public string username;
        public string password;
    }

    [Serializable]
    public class LoginResponseDto
    {
        public string userId;
        public string username;
        public string token;
    }

    [Serializable]
    public class MatchJoinResponseDto
    {
        public string userId;
        public string username;
        public string status;
        public string joinedAtUtc;
    }

    [Serializable]
    public class MatchStatusResponseDto
    {
        public string userId;
        public string username;
        public string status;
        public string joinedAtUtc;
        public string matchId;
        public string serverAddress;
    }

    [Serializable]
    public class MatchedHubEventDto
    {
        public string matchId;
        public int tcpPort;
        public int udpPort;
        public string serverAddreess; // AuthServer MatchWorker.cs 필드명 (오타 포함 호환)
        public string serverAddress;
        public string matchedAtUtc;

        public string GetEffectiveServerAddress()
        {
            if (!string.IsNullOrWhiteSpace(serverAddress)) return serverAddress;
            if (!string.IsNullOrWhiteSpace(serverAddreess)) return serverAddreess;
            if (tcpPort > 0) return $"127.0.0.1:{tcpPort}";
            return string.Empty;
        }
    }

    [Serializable]
    public class ApiErrorResponseDto
    {
        public string message;
        public string code;
    }

    public struct ApiResult<T>
    {
        public bool IsSuccess;
        public long StatusCode;
        public T Data;
        public string ErrorCode;
        public string ErrorMessage;

        public static ApiResult<T> Success(T data, long statusCode = 200) => new ApiResult<T>
        {
            IsSuccess = true,
            StatusCode = statusCode,
            Data = data
        };

        public static ApiResult<T> Fail(string message, string code = "ERROR", long statusCode = 0) => new ApiResult<T>
        {
            IsSuccess = false,
            StatusCode = statusCode,
            ErrorCode = code,
            ErrorMessage = message
        };
    }

    public class AuthApiClient
    {
        private const char SignalRRecordSeparator = '\u001e';

        public string BaseUrl { get; set; } = "http://localhost:5000";
        public string UserId { get; private set; }
        public string Username { get; private set; }
        public string JwtToken { get; private set; }

        private ClientWebSocket _matchHubWs;

        public bool IsAuthenticated => !string.IsNullOrEmpty(JwtToken);
        public bool IsWebSocketConnected => _matchHubWs != null && _matchHubWs.State == WebSocketState.Open;

        public AuthApiClient(string baseUrl = "http://localhost:5000")
        {
            BaseUrl = NormalizeBaseUrl(baseUrl);
        }

        public void SetBaseUrl(string url)
        {
            BaseUrl = NormalizeBaseUrl(url);
        }

        public async Task ClearAuthAsync()
        {
            await DisconnectMatchHubWebSocketAsync();
            UserId = null;
            Username = null;
            JwtToken = null;
        }

        public async Task<ApiResult<bool>> RegisterAsync(string username, string password, CancellationToken ct = default)
        {
            if (string.IsNullOrWhiteSpace(username) || string.IsNullOrWhiteSpace(password))
            {
                return ApiResult<bool>.Fail("아이디와 비밀번호를 입력해주세요.", "INVALID_INPUT");
            }

            var bodyObj = new AuthRequestDto { username = username.Trim(), password = password };
            string jsonBody = JsonUtility.ToJson(bodyObj);

            using UnityWebRequest req = CreateJsonRequest($"{BaseUrl}/auth/register", UnityWebRequest.kHttpVerbPOST, jsonBody, null);
            var (ok, statusCode, text, error) = await SendRequestWithCancellationAsync(req, ct);

            if (ok && (statusCode == 200 || statusCode == 201))
            {
                return ApiResult<bool>.Success(true, statusCode);
            }

            return ParseErrorResult<bool>(statusCode, text, error);
        }

        public async Task<ApiResult<LoginResponseDto>> LoginAsync(string username, string password, CancellationToken ct = default)
        {
            if (string.IsNullOrWhiteSpace(username) || string.IsNullOrWhiteSpace(password))
            {
                return ApiResult<LoginResponseDto>.Fail("아이디와 비밀번호를 입력해주세요.", "INVALID_INPUT");
            }

            var bodyObj = new AuthRequestDto { username = username.Trim(), password = password };
            string jsonBody = JsonUtility.ToJson(bodyObj);

            using UnityWebRequest req = CreateJsonRequest($"{BaseUrl}/auth/login", UnityWebRequest.kHttpVerbPOST, jsonBody, null);
            var (ok, statusCode, text, error) = await SendRequestWithCancellationAsync(req, ct);

            if (ok && statusCode == 200)
            {
                var data = JsonUtility.FromJson<LoginResponseDto>(text);
                if (data != null && !string.IsNullOrEmpty(data.token))
                {
                    UserId = data.userId;
                    Username = data.username;
                    JwtToken = data.token;
                    return ApiResult<LoginResponseDto>.Success(data, statusCode);
                }

                return ApiResult<LoginResponseDto>.Fail("로그인 응답 토큰이 비어 있습니다.", "INVALID_RESPONSE", statusCode);
            }

            return ParseErrorResult<LoginResponseDto>(statusCode, text, error);
        }

        public async Task<ApiResult<MatchJoinResponseDto>> JoinMatchAsync(CancellationToken ct = default)
        {
            if (!IsAuthenticated)
            {
                return ApiResult<MatchJoinResponseDto>.Fail("로그인이 필요합니다.", "UNAUTHORIZED");
            }

            using UnityWebRequest req = CreateJsonRequest($"{BaseUrl}/match/join", UnityWebRequest.kHttpVerbPOST, "{}", JwtToken);
            var (ok, statusCode, text, error) = await SendRequestWithCancellationAsync(req, ct);

            if (ok && statusCode == 200)
            {
                var data = JsonUtility.FromJson<MatchJoinResponseDto>(text);
                return ApiResult<MatchJoinResponseDto>.Success(data, statusCode);
            }

            return ParseErrorResult<MatchJoinResponseDto>(statusCode, text, error);
        }

        public async Task<ApiResult<MatchStatusResponseDto>> CancelMatchAsync(CancellationToken ct = default)
        {
            if (!IsAuthenticated)
            {
                return ApiResult<MatchStatusResponseDto>.Fail("로그인이 필요합니다.", "UNAUTHORIZED");
            }

            using UnityWebRequest req = CreateJsonRequest($"{BaseUrl}/match/cancel", UnityWebRequest.kHttpVerbPOST, "{}", JwtToken);
            var (ok, statusCode, text, error) = await SendRequestWithCancellationAsync(req, ct);

            await DisconnectMatchHubWebSocketAsync();

            if (ok && statusCode == 200)
            {
                var data = JsonUtility.FromJson<MatchStatusResponseDto>(text);
                return ApiResult<MatchStatusResponseDto>.Success(data, statusCode);
            }

            return ParseErrorResult<MatchStatusResponseDto>(statusCode, text, error);
        }

        /// <summary>
        /// AuthServer의 SignalR WebSocket 허브(/hubs/match)에 연결하고
        /// 서버에서 푸시하는 "Matched" 이벤트를 폴링 없이 실시간 수신합니다.
        /// </summary>
        public async Task<ApiResult<bool>> ConnectMatchHubWebSocketAsync(CancellationToken ct = default)
        {
            if (!IsAuthenticated)
            {
                return ApiResult<bool>.Fail("로그인이 필요합니다.", "UNAUTHORIZED");
            }

            if (IsWebSocketConnected)
            {
                return ApiResult<bool>.Success(true);
            }

            await DisconnectMatchHubWebSocketAsync();

            try
            {
                string wsUrl = BuildMatchHubWebSocketUrl(BaseUrl, JwtToken);
                _matchHubWs = new ClientWebSocket();
                await _matchHubWs.ConnectAsync(new Uri(wsUrl), ct);

                // SignalR JSON Hub Protocol Handshake 전송: {"protocol":"json","version":1}\x1e
                string handshake = "{\"protocol\":\"json\",\"version\":1}" + SignalRRecordSeparator;
                byte[] handshakeBytes = Encoding.UTF8.GetBytes(handshake);
                await _matchHubWs.SendAsync(
                    new ArraySegment<byte>(handshakeBytes),
                    WebSocketMessageType.Text,
                    endOfMessage: true,
                    ct);

                Debug.Log("[AuthApiClient] MatchHub WebSocket connected (/hubs/match).");
                return ApiResult<bool>.Success(true);
            }
            catch (OperationCanceledException)
            {
                await DisconnectMatchHubWebSocketAsync();
                throw;
            }
            catch (Exception ex)
            {
                await DisconnectMatchHubWebSocketAsync();
                return ApiResult<bool>.Fail($"MatchHub WebSocket 연결 실패: {ex.Message}", "WS_CONNECT_ERROR");
            }
        }

        /// <summary>
        /// WebSocket을 통해 서버가 푸시하는 "Matched" 이벤트를 대기합니다 (주기적 HTTP 요청 없음).
        /// </summary>
        public async Task<MatchedHubEventDto> WaitForMatchedEventViaWebSocketAsync(
            Action<string> onHubSystemMessage = null,
            CancellationToken ct = default)
        {
            if (!IsWebSocketConnected)
            {
                var connRes = await ConnectMatchHubWebSocketAsync(ct);
                if (!connRes.IsSuccess)
                {
                    throw new InvalidOperationException(connRes.ErrorMessage);
                }
            }

            byte[] buffer = new byte[4096];
            var sb = new StringBuilder();

            while (!ct.IsCancellationRequested && _matchHubWs != null && _matchHubWs.State == WebSocketState.Open)
            {
                WebSocketReceiveResult result = await _matchHubWs.ReceiveAsync(new ArraySegment<byte>(buffer), ct);

                if (result.MessageType == WebSocketMessageType.Close)
                {
                    throw new InvalidOperationException("MatchHub WebSocket connection closed by server.");
                }

                sb.Append(Encoding.UTF8.GetString(buffer, 0, result.Count));
                if (!result.EndOfMessage) continue;

                string payload = sb.ToString();
                sb.Clear();

                string[] records = payload.Split(SignalRRecordSeparator, StringSplitOptions.RemoveEmptyEntries);
                foreach (string record in records)
                {
                    // 1. SignalR Keep-Alive Ping (type: 6) -> Pong 응답
                    if (record.Contains("\"type\":6"))
                    {
                        byte[] pong = Encoding.UTF8.GetBytes("{\"type\":6}" + SignalRRecordSeparator);
                        await _matchHubWs.SendAsync(new ArraySegment<byte>(pong), WebSocketMessageType.Text, true, ct);
                        continue;
                    }

                    // 2. SystemMessage / JoinedMatchChat 알림
                    if (record.Contains("\"target\":\"JoinedMatchChat\""))
                    {
                        onHubSystemMessage?.Invoke("매칭 성사! Dedicated Server 프로세스 준비 중...");
                        continue;
                    }

                    // 3. Matched 이벤트 수신: {"type":1,"target":"Matched","arguments":[{...}]}
                    if (record.Contains("\"target\":\"Matched\""))
                    {
                        MatchedHubEventDto matchedDto = ExtractMatchedDtoFromSignalRRecord(record);
                        if (matchedDto != null && !string.IsNullOrEmpty(matchedDto.GetEffectiveServerAddress()))
                        {
                            return matchedDto;
                        }
                    }
                }
            }

            ct.ThrowIfCancellationRequested();
            return null;
        }

        public async Task DisconnectMatchHubWebSocketAsync()
        {
            if (_matchHubWs == null) return;

            var ws = _matchHubWs;
            _matchHubWs = null;

            try
            {
                if (ws.State == WebSocketState.Open || ws.State == WebSocketState.CloseReceived)
                {
                    using var closeCts = new CancellationTokenSource(TimeSpan.FromSeconds(1));
                    await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "Client disconnect", closeCts.Token);
                }
            }
            catch
            {
                // Ignore close errors during cleanup
            }
            finally
            {
                ws.Dispose();
            }
        }

        private readonly System.Collections.Generic.List<string> _lastCreatedBotUserIds = new System.Collections.Generic.List<string>();
        public System.Collections.Generic.IReadOnlyList<string> LastCreatedBotUserIds => _lastCreatedBotUserIds;

        /// <summary>
        /// C++ App::TriggerTenPlayerMatch와 동일하게 개발/단독 테스트 시
        /// 9명의 봇 계정을 생성 및 큐에 참가시켜 즉시 10인 매칭 및 Dedicated Server 스폰을 유도합니다.
        /// </summary>
        public async Task<ApiResult<bool>> TriggerTenPlayerBotFillAsync(Action<string> onProgress = null, CancellationToken ct = default)
        {
            _lastCreatedBotUserIds.Clear();
            long timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() % 1000000;

            for (int i = 1; i <= 9; i++)
            {
                ct.ThrowIfCancellationRequested();

                string botName = $"bot_{timestamp}_{i}";
                const string botPass = "password123!";
                string botJson = JsonUtility.ToJson(new AuthRequestDto { username = botName, password = botPass });

                using (UnityWebRequest regReq = CreateJsonRequest($"{BaseUrl}/auth/register", UnityWebRequest.kHttpVerbPOST, botJson, null))
                {
                    await SendRequestWithCancellationAsync(regReq, ct);
                }

                string botToken = null;
                string botUserId = null;
                using (UnityWebRequest loginReq = CreateJsonRequest($"{BaseUrl}/auth/login", UnityWebRequest.kHttpVerbPOST, botJson, null))
                {
                    var (ok, code, text, _) = await SendRequestWithCancellationAsync(loginReq, ct);
                    if (ok && code == 200)
                    {
                        var loginRes = JsonUtility.FromJson<LoginResponseDto>(text);
                        botToken = loginRes?.token;
                        botUserId = loginRes?.userId;
                    }
                }

                if (string.IsNullOrEmpty(botToken))
                {
                    return ApiResult<bool>.Fail($"봇 #{i} 로그인에 실패했습니다.", "BOT_LOGIN_FAIL");
                }

                if (!string.IsNullOrEmpty(botUserId))
                {
                    _lastCreatedBotUserIds.Add(botUserId);
                }

                using (UnityWebRequest joinReq = CreateJsonRequest($"{BaseUrl}/match/join", UnityWebRequest.kHttpVerbPOST, "{}", botToken))
                {
                    await SendRequestWithCancellationAsync(joinReq, ct);
                }

                onProgress?.Invoke($"[Dev] 봇 매칭 큐 참가 완료 ({i}/9: {botName})");
            }

            return ApiResult<bool>.Success(true);
        }

        public static bool TryParseServerAddress(string serverAddress, out string ip, out int port)
        {
            ip = string.Empty;
            port = 0;

            if (string.IsNullOrWhiteSpace(serverAddress)) return false;

            int colonIdx = serverAddress.LastIndexOf(':');
            if (colonIdx <= 0 || colonIdx >= serverAddress.Length - 1) return false;

            ip = serverAddress.Substring(0, colonIdx).Trim();
            string portStr = serverAddress.Substring(colonIdx + 1).Trim();
            return int.TryParse(portStr, out port) && port > 0 && port <= 65535;
        }

        private static MatchedHubEventDto ExtractMatchedDtoFromSignalRRecord(string record)
        {
            int argsIdx = record.IndexOf("\"arguments\":[", StringComparison.Ordinal);
            if (argsIdx < 0) return null;

            int firstBrace = record.IndexOf('{', argsIdx);
            int lastBrace = record.LastIndexOf(']', record.Length - 1);
            if (firstBrace < 0 || lastBrace <= firstBrace) return null;

            int objEnd = record.LastIndexOf('}', lastBrace);
            if (objEnd < firstBrace) return null;

            string argJson = record.Substring(firstBrace, objEnd - firstBrace + 1);
            return JsonUtility.FromJson<MatchedHubEventDto>(argJson);
        }

        private static string BuildMatchHubWebSocketUrl(string httpBaseUrl, string jwtToken)
        {
            string trimmed = NormalizeBaseUrl(httpBaseUrl);
            string wsBase;
            if (trimmed.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
            {
                wsBase = "wss://" + trimmed.Substring("https://".Length);
            }
            else if (trimmed.StartsWith("http://", StringComparison.OrdinalIgnoreCase))
            {
                wsBase = "ws://" + trimmed.Substring("http://".Length);
            }
            else
            {
                wsBase = "ws://" + trimmed;
            }

            return $"{wsBase}/hubs/match?access_token={Uri.EscapeDataString(jwtToken)}";
        }

        private static string NormalizeBaseUrl(string url)
        {
            if (string.IsNullOrWhiteSpace(url)) return "http://localhost:5000";
            return url.Trim().TrimEnd('/');
        }

        private static UnityWebRequest CreateJsonRequest(string url, string method, string jsonBody, string bearerToken)
        {
            var req = new UnityWebRequest(url, method);
            req.downloadHandler = new DownloadHandlerBuffer();
            req.timeout = 5;

            if (!string.IsNullOrEmpty(jsonBody))
            {
                byte[] bodyBytes = Encoding.UTF8.GetBytes(jsonBody);
                req.uploadHandler = new UploadHandlerRaw(bodyBytes);
                req.SetRequestHeader("Content-Type", "application/json");
            }

            if (!string.IsNullOrEmpty(bearerToken))
            {
                req.SetRequestHeader("Authorization", $"Bearer {bearerToken}");
            }

            return req;
        }

        private static async Task<(bool ok, long statusCode, string text, string error)> SendRequestWithCancellationAsync(
            UnityWebRequest req,
            CancellationToken ct)
        {
            ct.ThrowIfCancellationRequested();

            var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

            using (ct.Register(() =>
            {
                try { req.Abort(); } catch { }
                tcs.TrySetCanceled(ct);
            }))
            {
                UnityWebRequestAsyncOperation op = req.SendWebRequest();
                op.completed += _ => tcs.TrySetResult(true);

                await tcs.Task;

                bool ok = req.result == UnityWebRequest.Result.Success;
                string responseText = req.downloadHandler != null ? req.downloadHandler.text : string.Empty;
                return (ok, req.responseCode, responseText, req.error);
            }
        }

        private static ApiResult<T> ParseErrorResult<T>(long statusCode, string responseText, string fallbackError)
        {
            if (!string.IsNullOrWhiteSpace(responseText))
            {
                try
                {
                    var err = JsonUtility.FromJson<ApiErrorResponseDto>(responseText);
                    if (err != null && !string.IsNullOrEmpty(err.message))
                    {
                        return ApiResult<T>.Fail(err.message, string.IsNullOrEmpty(err.code) ? "API_ERROR" : err.code, statusCode);
                    }
                }
                catch
                {
                    // Fallback to raw text
                }
            }

            string msg = !string.IsNullOrEmpty(fallbackError)
                ? $"서버 요청 실패 ({statusCode}): {fallbackError}"
                : $"서버 요청 실패 (HTTP {statusCode})";
            return ApiResult<T>.Fail(msg, "HTTP_ERROR", statusCode);
        }
    }
}
