using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Protocol;
using Network;

namespace Visualizer
{
    public class LagCompVisualizer : MonoBehaviour
    {
        [Header("Prefabs")]
        public GameObject playerPrefab;   // enemy player 3D prefab
        public GameObject presentGhostPrefab; // red transparent ghost
        public GameObject rewoundGhostPrefab; // green transparent ghost

        [Header("Visual Settings")]
        public LineRenderer laserLine; // shoot line
        public float laserDuration = 0.5f;
        public float ghostDestroyTime = 1.5f;

        private readonly Dictionary<string, GameObject> players = new Dictionary<string, GameObject>();

        private void Start()
        {
            if (PacketHandler.Instance != null)
            {
                PacketHandler.Instance.OnPlayerMoved += UpdatePlayerPosition;
                PacketHandler.Instance.OnDebugLagCompReceived += VisualizeLagCompensation;
            }
        }

        private void OnDestroy()
        {
            if (PacketHandler.Instance != null)
            {
                PacketHandler.Instance.OnPlayerMoved -= UpdatePlayerPosition;
                PacketHandler.Instance.OnDebugLagCompReceived -= VisualizeLagCompensation;
            }
        }

        private void UpdatePlayerPosition(string sessionId, Vector3 position)
        {
            if (!players.TryGetValue(sessionId, out var playerObj))
            {
                playerObj = Instantiate(playerPrefab, position, Quaternion.identity);
                playerObj.name = $"Player_{sessionId.Substring(0, Mathf.Min(8, sessionId.Length))}";
                players[sessionId] = playerObj;
            }
            else
            {
                playerObj.transform.position = position;
            }
        }

        private void VisualizeLagCompensation(DebugLagCompPacket packet)
        {
            Vector3 shootOrigin = new Vector3(packet.OriginX, packet.OriginY, packet.OriginZ);
            Vector3 shootDir = new Vector3(packet.DirX, packet.DirY, packet.DirZ).normalized;

            Vector3 shootEnd = shootOrigin + shootDir * 100f; 

            StartCoroutine(DrawLaser(shootOrigin, shootEnd));

            foreach (var target in packet.Targets)
            {
                string targetId = target.TargetId.ToStringUtf8();

                // Spawn Present Ghost (Red)
                Vector3 presentPos = new Vector3(target.PresentX, target.PresentY, target.PresentZ);
                SpawnGhost(presentGhostPrefab, presentPos, $"PresentGhost_{targetId.Substring(0, Mathf.Min(8, targetId.Length))}");

                // Spawn Rewound Ghost (Green)
                Vector3 rewoundPos = new Vector3(target.RewoundX, target.RewoundY, target.RewoundZ);
                var rewNode = SpawnGhost(rewoundGhostPrefab, rewoundPos, $"RewoundGhost_{targetId.Substring(0, Mathf.Min(8, targetId.Length))}");

                if (target.IsHit && rewNode != null)
                {
                    rewNode.transform.localScale = Vector3.one * 1.2f;
                    Debug.Log($"<color=red>HIT SUCCESS</color> on {targetId.Substring(0, Mathf.Min(8, targetId.Length))} at rewound pos: {rewoundPos}");
                }
            }
        }

        private GameObject SpawnGhost(GameObject prefab, Vector3 position, string name)
        {
            if (prefab == null) return null;

            GameObject ghost = Instantiate(prefab, position, Quaternion.identity);
            ghost.name = name;
            
            Destroy(ghost, ghostDestroyTime);
            return ghost;
        }

        private IEnumerator DrawLaser(Vector3 start, Vector3 end)
        {
            if (laserLine == null) yield break;

            laserLine.enabled = true;
            laserLine.SetPosition(0, start);
            laserLine.SetPosition(1, end);

            yield return new WaitForSeconds(laserDuration);

            laserLine.enabled = false;
        }
    }
}
