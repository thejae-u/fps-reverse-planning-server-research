using System.Collections.Generic;
using UnityEngine;
using Protocol;

namespace Network
{
    public class NetworkManager : MonoBehaviour
    {
        public static NetworkManager Instance { get; private set; }

        [Header("Server Connection")]
        public string serverIp = "127.0.0.1";
        public int serverPort = 52800; // C++ Server Default Port is 52800

        [Header("Load Testing")]
        public int autoSpawnDummyCount = 9; // Spawn 9 dummy bots by default to satisfy 10-player match

        private NetworkSession mainSession;
        private readonly List<NetworkSession> dummySessions = new List<NetworkSession>();

        private void Awake()
        {
            if (Instance == null)
            {
                Instance = this;
                DontDestroyOnLoad(gameObject);
            }
            else
            {
                Destroy(gameObject);
            }
        }

        private void Start()
        {
            ConnectToServer();
        }

        private void ConnectToServer()
        {
            // 1. Establish Main session
            mainSession = new NetworkSession(serverIp, serverPort, true);
            mainSession.Connect();

            // 2. Automatically spawn dummy sessions to satisfy 10-player match
            if (autoSpawnDummyCount > 0)
            {
                SpawnDummyBots(autoSpawnDummyCount);
            }
        }

        public async void SpawnDummyBots(int count)
        {
            Debug.Log($"Spawning {count} dummy sessions for load testing...");
            for (int i = 0; i < count; i++)
            {
                var dummy = new NetworkSession(serverIp, serverPort, false);
                dummy.Connect();
                dummySessions.Add(dummy);
                await System.Threading.Tasks.Task.Delay(100);
            }
        }

        public void OnMainSessionMatched(string roomId)
        {
            Debug.Log($"[NetworkManager] Main session entered room: {roomId}");
        }

        public void SendIngameUdp(IngameType method, byte[] subPacketData)
        {
            mainSession?.SendIngameUdp(method, subPacketData);
        }

        public void Disconnect()
        {
            mainSession?.Disconnect();
            foreach (var dummy in dummySessions)
            {
                dummy.Disconnect();
            }
            dummySessions.Clear();
            Debug.Log("Disconnected all sessions.");
        }

        private void OnDestroy()
        {
            Disconnect();
        }
    }
}
