using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.SceneManagement;
using FPSGame.Management;
using Protocol;

public class NetworkManager : MonoBehaviour
{
    public static NetworkManager Instance { get; private set; }

    [Header("Dedicated Server Target")]
    [SerializeField] private string serverIp = "127.0.0.1";
    [SerializeField] private int serverPort = 7777;
    [SerializeField] private int serverUdpPort = 7778;
    [SerializeField] private string gameSceneName = "SampleScene";
    [SerializeField] private bool autoConnectOnStart = false;

    private NetworkClient _client;
    private readonly List<NetworkClient> _botClients = new List<NetworkClient>();
    private readonly ConcurrentQueue<IngamePacket> _incomingPackets = new ConcurrentQueue<IngamePacket>();
    private readonly ConcurrentQueue<InfoHandshakePacket> _incomingInfoPackets = new ConcurrentQueue<InfoHandshakePacket>();

    public NetworkClient Client => _client;
    public IReadOnlyList<NetworkClient> BotClients => _botClients;
    public string ServerIp => serverIp;
    public int ServerPort => serverPort;
    public int ServerUdpPort => serverUdpPort;
    public string UserId { get; private set; }
    public string Username { get; private set; }
    public string JwtToken { get; private set; }
    public string MatchId { get; private set; }

    public static NetworkManager EnsureInstance()
    {
        if (Instance != null) return Instance;

        NetworkManager existing = FindAnyObjectByType<NetworkManager>();
        if (existing != null)
        {
            Instance = existing;
            return Instance;
        }

        GameObject go = new GameObject("NetworkManager");
        Instance = go.AddComponent<NetworkManager>();
        return Instance;
    }

    private void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }

        Instance = this;
        DontDestroyOnLoad(gameObject);

        _client = new NetworkClient();
    }

    private async void Start()
    {
        if (autoConnectOnStart && !string.IsNullOrEmpty(serverIp) && serverPort > 0)
        {
            await ConnectToDedicatedServerAsync(serverIp, serverPort, serverUdpPort, 0, null, destroyCancellationToken);
        }
    }

    private void Update()
    {
        if (GameManager.Instance == null || _client == null) return;

        while (_incomingInfoPackets.TryDequeue(out InfoHandshakePacket info))
        {
            GameManager.Instance.ApplyServerInfoHandshake(info);
        }

        string localSessionId = _client.SessionId;
        while (_incomingPackets.TryDequeue(out IngamePacket packet))
        {
            GameManager.Instance.ProcessServerIngamePacket(packet, localSessionId);
        }
    }

    public void SetUserSession(string userId, string username, string jwtToken)
    {
        UserId = userId;
        Username = username;
        JwtToken = jwtToken;
    }

    public async Task ConnectToDedicatedServerAsync(
        string ip,
        int tcpPort,
        int udpPort = 0,
        int dummyBotClientCount = 0,
        IReadOnlyList<string> botUserIds = null,
        CancellationToken ct = default)
    {
        serverIp = ip;
        serverPort = tcpPort;
        serverUdpPort = udpPort > 0 ? udpPort : tcpPort;

        DisconnectAll();

        while (_incomingPackets.TryDequeue(out _)) { }
        while (_incomingInfoPackets.TryDequeue(out _)) { }

        _client = new NetworkClient();
        _client.Init(serverIp, serverPort, serverUdpPort, UserId);
        _client.OnInfoHandshakeReceived += OnClientInfoHandshakeReceived;
        _client.OnIngamePacketReceived += OnClientIngamePacketReceived;

        Debug.Log($"[NetworkManager] Connecting Local Client (UserId: {UserId}) to Dedicated Server {serverIp} (TCP:{serverPort}, UDP:{serverUdpPort})...");
        await _client.ConnectAsync(ct);

        if (dummyBotClientCount > 0)
        {
            Debug.Log($"[NetworkManager] Connecting {dummyBotClientCount} Dev Bot TCP/UDP Clients to satisfy Room expectedPlayerCount...");
            var connectTasks = new List<Task>(dummyBotClientCount);

            for (int i = 0; i < dummyBotClientCount; i++)
            {
                string botId = (botUserIds != null && i < botUserIds.Count) ? botUserIds[i] : string.Empty;
                var botClient = new NetworkClient();
                botClient.Init(serverIp, serverPort, serverUdpPort, botId);
                _botClients.Add(botClient);
                connectTasks.Add(botClient.ConnectAsync(ct));
            }

            await Task.WhenAll(connectTasks);
            Debug.Log($"[NetworkManager] All {dummyBotClientCount} Bot Clients connected (Total sessions: {1 + _botClients.Count}).");
        }
    }

    private void OnClientInfoHandshakeReceived(InfoHandshakePacket info)
    {
        if (info != null)
        {
            _incomingInfoPackets.Enqueue(info);
        }
    }

    private void OnClientIngamePacketReceived(IngamePacket packet)
    {
        if (packet != null)
        {
            _incomingPackets.Enqueue(packet);
        }
    }

    /// <summary>
    /// 매칭 완료 후 전달받은 Dedicated Server IP/TCP Port/UDP Port 정보를 저장하고,
    /// 게임 씬(SampleScene)으로 이동한 뒤 로컬 클라이언트(및 Dev 봇 클라이언트들)의 네트워크 연결을 수행합니다.
    /// </summary>
    public async Task TransitionToGameAndConnectAsync(
        string ip,
        int tcpPort,
        int udpPort,
        string matchId,
        string targetScene = null,
        int dummyBotClientCount = 0,
        IReadOnlyList<string> botUserIds = null)
    {
        serverIp = ip;
        serverPort = tcpPort;
        serverUdpPort = udpPort > 0 ? udpPort : tcpPort;
        MatchId = matchId;

        string sceneToLoad = string.IsNullOrEmpty(targetScene) ? gameSceneName : targetScene;
        Debug.Log($"[NetworkManager] Match Ready ({matchId}) -> Loading scene '{sceneToLoad}' and connecting to {ip} (TCP:{serverPort}, UDP:{serverUdpPort}, Bots:{dummyBotClientCount})...");

        AsyncOperation loadOp = SceneManager.LoadSceneAsync(sceneToLoad);
        if (loadOp != null)
        {
            while (!loadOp.isDone)
            {
                if (destroyCancellationToken.IsCancellationRequested) return;
                await Task.Yield();
            }
        }

        await ConnectToDedicatedServerAsync(serverIp, serverPort, serverUdpPort, dummyBotClientCount, botUserIds, destroyCancellationToken);
    }

    public void DisconnectAll()
    {
        if (_client != null)
        {
            _client.OnInfoHandshakeReceived -= OnClientInfoHandshakeReceived;
            _client.OnIngamePacketReceived -= OnClientIngamePacketReceived;
            _client.Disconnect();
        }

        for (int i = 0; i < _botClients.Count; i++)
        {
            _botClients[i]?.Disconnect();
        }
        _botClients.Clear();
    }

    private void OnDestroy()
    {
        if (Instance == this)
        {
            DisconnectAll();
            Instance = null;
        }
    }
}
