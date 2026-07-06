using System;
using System.Collections.Concurrent;
using UnityEngine;
using Google.Protobuf;
using Protocol;

namespace Network
{
    public class PacketHandler : MonoBehaviour
    {
        public static PacketHandler Instance { get; private set; }

        // 메인 스레드 전달용 스레드-세이프 큐
        private readonly ConcurrentQueue<Action> mainThreadActions = new ConcurrentQueue<Action>();

        // 외부 연동용 C# 이벤트 정의
        public event Action<string, Vector3> OnPlayerMoved;
        public event Action<HitPacket> OnPlayerHit;
        public event Action<DebugLagCompPacket> OnDebugLagCompReceived;

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

        private void Update()
        {
            // 백그라운드 스레드에서 수신한 패킷 처리 로직을 유니티 메인 스레드에서 실행
            while (mainThreadActions.TryDequeue(out var action))
            {
                action?.Invoke();
            }
        }

        public void EnqueueAction(Action action)
        {
            mainThreadActions.Enqueue(action);
        }

        public void HandleIngamePacket(IngamePacket ingame)
        {
            if (ingame.Method == IngameType.Move)
            {
                // 12바이트 Vector3 데이터 복원 (float 3개)
                byte[] posBytes = ingame.Data.ToByteArray();
                if (posBytes.Length >= 12)
                {
                    float x = BitConverter.ToSingle(posBytes, 0);
                    float y = BitConverter.ToSingle(posBytes, 4);
                    float z = BitConverter.ToSingle(posBytes, 8);
                    Vector3 position = new Vector3(x, y, z);

                    string senderSessionId = ingame.SessionId.ToStringUtf8();

                    EnqueueAction(() =>
                    {
                        OnPlayerMoved?.Invoke(senderSessionId, position);
                    });
                }
            }
            else if (ingame.Method == IngameType.Hit)
            {
                var hitData = HitPacket.Parser.ParseFrom(ingame.Data);
                EnqueueAction(() =>
                {
                    OnPlayerHit?.Invoke(hitData);
                });
            }
            else if (ingame.Method == IngameType.DebugLagComp)
            {
                var debugData = DebugLagCompPacket.Parser.ParseFrom(ingame.Data);
                EnqueueAction(() =>
                {
                    OnDebugLagCompReceived?.Invoke(debugData);
                });
            }
        }
    }
}
