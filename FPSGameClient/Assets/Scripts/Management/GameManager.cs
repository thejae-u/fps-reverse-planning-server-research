using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif
using FPSGame.Player;
using FPSGame.UI;

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
        private PlayerController localPlayer;

        public PlayerController LocalPlayer => localPlayer;
        public IReadOnlyList<PlayerController> ActivePlayers => activePlayers;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void AutoInitialize()
        {
            if (Instance == null && FindAnyObjectByType<GameManager>() == null)
            {
                Debug.Log("[GameManager] Auto-initializing FPS GameManager on scene load...");
                GameObject gm = new GameObject("GameManager_Auto");
                gm.AddComponent<GameManager>();
            }
        }

        private void Awake()
        {
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
            bool f1Pressed = false;
            bool f2Pressed = false;

#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null)
            {
                f1Pressed = Keyboard.current.f1Key.wasPressedThisFrame;
                f2Pressed = Keyboard.current.f2Key.wasPressedThisFrame;
            }
#else
            f1Pressed = Input.GetKeyDown(KeyCode.F1);
            f2Pressed = Input.GetKeyDown(KeyCode.F2);
#endif

            if (f1Pressed)
            {
                SpawnRemoteDummyPlayer();
            }

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
                floor.transform.localScale = new Vector3(50f, 1f, 50f);

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

            PlayerController pc = player.AddComponent<PlayerController>();
            PlayerShooter ps = player.AddComponent<PlayerShooter>();

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

        public void SpawnRemoteDummyPlayer()
        {
            if (activePlayers.Count >= maxPlayers)
            {
                Debug.Log($"[GameManager] Maximum player limit ({maxPlayers}) reached!");
                return;
            }

            int newId = activePlayers.Count;
            Vector3 spawnPos = Vector3.zero;
            Quaternion spawnRot = Quaternion.identity;

            if (spawnPoints != null && spawnPoints.Length > newId && spawnPoints[newId] != null)
            {
                spawnPos = spawnPoints[newId].position;
                spawnRot = spawnPoints[newId].rotation;
            }
            else
            {
                // Circular offset around origin if not enough spawn points
                float angle = newId * (360f / maxPlayers);
                spawnPos = new Vector3(Mathf.Sin(angle * Mathf.Deg2Rad) * 6f, 1.5f, Mathf.Cos(angle * Mathf.Deg2Rad) * 6f);
                spawnRot = Quaternion.LookRotation(-spawnPos.normalized);
            }

            GameObject remoteObj = playerPrefab != null 
                ? Instantiate(playerPrefab, spawnPos, spawnRot) 
                : CreateDynamicPlayerInstance(spawnPos, spawnRot);
            PlayerController remotePlayer = remoteObj.GetComponent<PlayerController>();

            if (remotePlayer != null)
            {
                remotePlayer.Initialize(newId, isLocal: false, $"RemotePlayer_{newId}");
                activePlayers.Add(remotePlayer);

                // Add simple dummy bot behaviour for local simulation
                remoteObj.AddComponent<RemoteDummyBehaviour>();

                Debug.Log($"[GameManager] Spawned Remote Dummy #{newId}. Current count: {activePlayers.Count}/{maxPlayers}");
                UpdatePlayerCountUI();
            }
        }

        public void RemoveAllRemotePlayers()
        {
            for (int i = activePlayers.Count - 1; i >= 1; i--)
            {
                Destroy(activePlayers[i].gameObject);
                activePlayers.RemoveAt(i);
            }

            Debug.Log("[GameManager] Cleared all remote dummy players.");
            UpdatePlayerCountUI();
        }

        private void UpdatePlayerCountUI()
        {
            if (uiController != null)
            {
                uiController.UpdatePlayerCount(activePlayers.Count, maxPlayers);
            }
        }
    }

    // Simple script to make remote dummies look around periodically for testing
    public class RemoteDummyBehaviour : MonoBehaviour
    {
        private float nextActionTime = 0f;
        private Quaternion targetRot;

        private void Start()
        {
            targetRot = transform.rotation;
        }

        private void Update()
        {
            if (Time.time >= nextActionTime)
            {
                nextActionTime = Time.time + Random.Range(2.0f, 5.0f);
                float randomAngle = Random.Range(-45f, 45f);
                targetRot = Quaternion.Euler(0f, transform.eulerAngles.y + randomAngle, 0f);
            }

            transform.rotation = Quaternion.Slerp(transform.rotation, targetRot, Time.deltaTime * 3f);
        }
    }
}
