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
    [SerializeField] private string gameSceneName = "SampleScene";
    [SerializeField] private bool autoConnectOnStart = false;

    private NetworkClient _client;
    private readonly List<NetworkClient> _botClients = new List<NetworkClient>();
    private readonly ConcurrentQueue<IngamePacket> _incomingPackets = new ConcurrentQueue<IngamePacket>();

    public NetworkClient Client => _client;
    public IReadOnlyList<NetworkClient> BotClients => _botClients;
    public string ServerIp => serverIp;
    public int ServerPort => serverPort;
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
            await ConnectToDedicatedServerAsync(serverIp, serverPort, 0, destroyCancellationToken);
        }
    }

    private void Update()
    {
        if (GameManager.Instance == null || _client == null) return;

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
        int port,
        int dummyBotClientCount = 0,
        CancellationToken ct = default)
    {
        serverIp = ip;
        serverPort = port;

        DisconnectAll();

        while (_incomingPackets.TryDequeue(out _)) { }

        _client = new NetworkClient();
        _client.Init(serverIp, serverPort);
        _client.OnIngamePacketReceived += OnClientIngamePacketReceived;

        Debug.Log($"[NetworkManager] Connecting Local Client to Dedicated Server {serverIp}:{serverPort}...");
        await _client.ConnectAsync(ct);

        if (dummyBotClientCount > 0)
        {
            Debug.Log($"[NetworkManager] Connecting {dummyBotClientCount} Dev Bot TCP/UDP Clients to satisfy Room expectedPlayerCount...");
            var connectTasks = new List<Task>(dummyBotClientCount);

            for (int i = 0; i < dummyBotClientCount; i++)
            {
                var botClient = new NetworkClient();
                botClient.Init(serverIp, serverPort);
                _botClients.Add(botClient);
                connectTasks.Add(botClient.ConnectAsync(ct));
            }

            await Task.WhenAll(connectTasks);
            Debug.Log($"[NetworkManager] All {dummyBotClientCount} Bot Clients connected (Total sessions: {1 + _botClients.Count}).");
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
    /// 매칭 완료 후 전달받은 Dedicated Server IP/Port 정보를 저장하고,
    /// 게임 씬(SampleScene)으로 이동한 뒤 로컬 클라이언트(및 Dev 봇 클라이언트들)의 네트워크 연결을 수행합니다.
    /// </summary>
    public async Task TransitionToGameAndConnectAsync(
        string ip,
        int port,
        string matchId,
        string targetScene = null,
        int dummyBotClientCount = 0)
    {
        serverIp = ip;
        serverPort = port;
        MatchId = matchId;

        string sceneToLoad = string.IsNullOrEmpty(targetScene) ? gameSceneName : targetScene;
        Debug.Log($"[NetworkManager] Match Ready ({matchId}) -> Loading scene '{sceneToLoad}' and connecting to {ip}:{port} (Bots: {dummyBotClientCount})...");

        AsyncOperation loadOp = SceneManager.LoadSceneAsync(sceneToLoad);
        if (loadOp != null)
        {
            while (!loadOp.isDone)
            {
                if (destroyCancellationToken.IsCancellationRequested) return;
                await Task.Yield();
            }
        }

        await ConnectToDedicatedServerAsync(serverIp, serverPort, dummyBotClientCount, destroyCancellationToken);
    }

    public void DisconnectAll()
    {
        if (_client != null)
        {
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
