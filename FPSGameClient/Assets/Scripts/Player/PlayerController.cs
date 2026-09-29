using UnityEngine;
using UnityEngine.Rendering.Universal;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace FPSGame.Player
{
    [RequireComponent(typeof(CharacterController))]
    public class PlayerController : MonoBehaviour
    {
        public const string WeaponLayerName = "FirstPersonWeapon";
        public const int FallbackWeaponLayer = 6;

        [Header("Identity & Networking (Up to 10 players)")]
        [SerializeField] private int playerId = 0;
        [SerializeField] private bool isLocalPlayer = true;
        [SerializeField] private string playerName = "Player";

        [Header("Player Data SO")]
        [SerializeField] private PlayerSO playerSO;

        public PlayerSO PlayerData
        {
            get => playerSO;
            set => playerSO = value;
        }

        public float WalkSpeed => playerSO != null ? playerSO.walkSpeed : 5.5f;
        public float SprintSpeed => playerSO != null ? playerSO.sprintSpeed : 9.0f;
        public float JumpHeight => playerSO != null ? playerSO.jumpHeight : 1.3f;
        public float Gravity => playerSO != null ? playerSO.gravity : -22.0f;
        public float MouseSensitivity => playerSO != null ? playerSO.mouseSensitivity : 2.0f;
        public float MaxPitchAngle => playerSO != null ? playerSO.maxPitchAngle : 85.0f;
        public float MinPitchAngle => playerSO != null ? playerSO.minPitchAngle : -85.0f;

        [Header("Component References")]
        [SerializeField] private Transform cameraHolder;
        [SerializeField] private Camera localCamera;
        [SerializeField] private Camera weaponCamera;
        [SerializeField] private AudioListener audioListener;
        [SerializeField] private GameObject firstPersonModel;
        [SerializeField] private GameObject thirdPersonModel;
        [SerializeField] private TextMesh nameTagTextMesh;
        [SerializeField] private PlayerShooter shooter;
        [SerializeField] private PlayerWeaponSway weaponSway;

        // Movement State
        private CharacterController characterController;
        private Vector3 velocity;
        private bool isGrounded;
        private float cameraPitch = 0f;
        private float cameraRecoilPitch = 0f;

        // Remote Player Interpolation
        private Vector3 targetNetworkPosition;
        private Quaternion targetNetworkRotation;
        private float networkLerpSpeed = 15f;

        // Local Player Network Sync
        private float nextNetworkSendTime = 0f;
        private const float NetworkSendInterval = 0.0166f; // ~60Hz (서버 틱과 동기화)
        private Vector3 currentMoveDirection = Vector3.zero;
        private Vector3 lastSentMoveDir = Vector3.zero;
        private bool hasSyncedServerSpawn = false;

        // Properties
        public int PlayerId => playerId;
        public bool IsLocalPlayer => isLocalPlayer;
        public string PlayerName => playerName;
        public bool HasSyncedServerSpawn => hasSyncedServerSpawn;
        public Camera LocalCamera => localCamera;
        public Camera WeaponCamera => weaponCamera;
        public PlayerShooter Shooter => shooter;

        public static int GetWeaponLayer()
        {
            int layer = LayerMask.NameToLayer(WeaponLayerName);
            return layer >= 0 ? layer : FallbackWeaponLayer;
        }

        private void Awake()
        {
            characterController = GetComponent<CharacterController>();
            if (shooter == null) shooter = GetComponent<PlayerShooter>();
            if (weaponSway == null) weaponSway = GetComponentInChildren<PlayerWeaponSway>(true);

            if (cameraHolder == null)
            {
                Camera cam = GetComponentInChildren<Camera>(true);
                if (cam != null)
                {
                    localCamera = cam;
                    cameraHolder = cam.transform.parent != null ? cam.transform.parent : cam.transform;
                }
            }

            if (localCamera == null)
            {
                localCamera = GetComponentInChildren<Camera>(true);
            }

            if (audioListener == null && localCamera != null)
            {
                audioListener = localCamera.GetComponent<AudioListener>();
            }

            if (firstPersonModel == null)
            {
                PlayerWeaponSway sway = GetComponentInChildren<PlayerWeaponSway>(true);
                if (sway != null) firstPersonModel = sway.gameObject;
            }

            if (thirdPersonModel == null)
            {
                Transform tp = transform.Find("ThirdPersonModel");
                if (tp != null) thirdPersonModel = tp.gameObject;
            }

            if (nameTagTextMesh == null)
            {
                nameTagTextMesh = GetComponentInChildren<TextMesh>(true);
            }

            if (playerSO == null && shooter != null && shooter.PlayerData != null)
            {
                playerSO = shooter.PlayerData;
            }

            SetupWeaponCameraStack();
        }

        private void SetupWeaponCameraStack()
        {
            if (localCamera == null) return;

            int weaponLayer = GetWeaponLayer();
            int weaponLayerMask = 1 << weaponLayer;

            // 1. Assign weapon model hierarchy to FirstPersonWeapon layer
            if (firstPersonModel != null)
            {
                SetLayerRecursively(firstPersonModel, weaponLayer);
            }

            // 2. Exclude FirstPersonWeapon layer from Base Camera culling mask
            localCamera.cullingMask &= ~weaponLayerMask;

            UniversalAdditionalCameraData baseCamData = localCamera.GetUniversalAdditionalCameraData();
            baseCamData.renderType = CameraRenderType.Base;

            // 3. Find or create Overlay WeaponCamera
            if (weaponCamera == null)
            {
                Transform existingWeaponCam = localCamera.transform.Find("WeaponCamera");
                if (existingWeaponCam != null)
                {
                    weaponCamera = existingWeaponCam.GetComponent<Camera>();
                }
            }

            if (weaponCamera == null)
            {
                GameObject weaponCamObj = new GameObject("WeaponCamera");
                weaponCamObj.transform.SetParent(localCamera.transform, false);
                weaponCamObj.transform.localPosition = Vector3.zero;
                weaponCamObj.transform.localRotation = Quaternion.identity;
                weaponCamera = weaponCamObj.AddComponent<Camera>();
            }

            weaponCamera.cullingMask = weaponLayerMask;
            weaponCamera.fieldOfView = localCamera.fieldOfView;
            weaponCamera.nearClipPlane = 0.01f;
            weaponCamera.farClipPlane = 10f;
            weaponCamera.clearFlags = CameraClearFlags.Depth;

            UniversalAdditionalCameraData weaponCamData = weaponCamera.GetUniversalAdditionalCameraData();
            weaponCamData.renderType = CameraRenderType.Overlay;

            if (!baseCamData.cameraStack.Contains(weaponCamera))
            {
                baseCamData.cameraStack.Add(weaponCamera);
            }
        }

        private static void SetLayerRecursively(GameObject obj, int newLayer)
        {
            if (obj == null) return;
            obj.layer = newLayer;
            foreach (Transform child in obj.transform)
            {
                if (child != null)
                {
                    SetLayerRecursively(child.gameObject, newLayer);
                }
            }
        }

        private void Start()
        {
            ApplyPlayerRole();

            if (isLocalPlayer)
            {
                SetCursorLock(true);
                if (shooter != null)
                {
                    shooter.OnShoot += OnLocalPlayerShoot;
                }
            }
        }

        private void OnDestroy()
        {
            if (shooter != null)
            {
                shooter.OnShoot -= OnLocalPlayerShoot;
            }
        }

        private void OnLocalPlayerShoot()
        {
            if (!isLocalPlayer) return;

            var netClient = NetworkManager.Instance?.Client;
            if (netClient != null && netClient.IsConnected)
            {
                Vector3 aimDir = localCamera != null ? localCamera.transform.forward : transform.forward;
                _ = netClient.SendShootAsync(aimDir);
            }
        }

        public void Initialize(int id, bool isLocal, string name = "")
        {
            playerId = id;
            isLocalPlayer = isLocal;
            playerName = string.IsNullOrEmpty(name) ? (isLocal ? "Local Player" : $"Player {id + 1}") : name;

            ApplyPlayerRole();
        }

        private void ApplyPlayerRole()
        {
            // First Person vs Third Person configuration
            if (localCamera != null) localCamera.enabled = isLocalPlayer;
            if (weaponCamera != null) weaponCamera.enabled = isLocalPlayer;
            if (audioListener != null) audioListener.enabled = isLocalPlayer;

            if (firstPersonModel != null) firstPersonModel.SetActive(isLocalPlayer);
            if (thirdPersonModel != null) thirdPersonModel.SetActive(!isLocalPlayer);

            if (nameTagTextMesh != null)
            {
                nameTagTextMesh.text = playerName;
                nameTagTextMesh.gameObject.SetActive(!isLocalPlayer);
            }

            if (!isLocalPlayer)
            {
                targetNetworkPosition = transform.position;
                targetNetworkRotation = transform.rotation;
            }
        }

        private void Update()
        {
            if (isLocalPlayer)
            {
                HandleCursorToggle();
                HandleMouseLook();
                HandleMovement();

                // Weapon Sway & Shooting
                Vector2 mouseDelta = ReadMouseDelta();
                bool isMoving = characterController.velocity.magnitude > 0.2f && isGrounded;
                bool isSprinting = IsSprintPressed() && isMoving;

                if (weaponSway != null)
                {
                    weaponSway.UpdateSway(mouseDelta, isMoving, isSprinting);
                }

                if (shooter != null)
                {
                    shooter.HandleShooting(isSprinting, ApplyCameraRecoil);
                }

                // Periodically send MovePacket to Dedicated Server after initial spawn position sync
                var netClient = NetworkManager.Instance?.Client;
                if (netClient != null && netClient.IsUdpAuthenticated && hasSyncedServerSpawn)
                {
                    Vector3 netMoveDir = currentMoveDirection.sqrMagnitude > 0.001f
                        ? (currentMoveDirection * (isSprinting ? 2.0f : 1.0f))
                        : Vector3.zero;

                    bool isCurrentlyMoving = netMoveDir.sqrMagnitude > 0.001f;
                    bool wasMoving = lastSentMoveDir.sqrMagnitude > 0.001f;

                    // 이동 중일 때는 주기적(60Hz) 전송, 이동->정지 전환 순간에는 정지(Zero) 패킷 1회 전송, 계속 정지 중이면 전송 생략
                    if ((isCurrentlyMoving && (!wasMoving || Time.time >= nextNetworkSendTime)) || (!isCurrentlyMoving && wasMoving))
                    {
                        nextNetworkSendTime = Time.time + NetworkSendInterval;
                        lastSentMoveDir = netMoveDir;
                        _ = netClient.SendMoveAsync(transform.position, netMoveDir);
                    }
                }
            }
            else
            {
                // Smooth interpolation for remote players
                transform.position = Vector3.Lerp(transform.position, targetNetworkPosition, Time.deltaTime * networkLerpSpeed);
                transform.rotation = Quaternion.Slerp(transform.rotation, targetNetworkRotation, Time.deltaTime * networkLerpSpeed);

                // Make nametag face local player camera
                if (nameTagTextMesh != null && Camera.main != null)
                {
                    nameTagTextMesh.transform.rotation = Quaternion.LookRotation(nameTagTextMesh.transform.position - Camera.main.transform.position);
                }
            }
        }

        private void HandleMovement()
        {
            isGrounded = characterController.isGrounded;
            if (isGrounded && velocity.y < 0)
            {
                velocity.y = -2f; // Slight downward push to keep grounded
            }

            Vector2 moveInput = ReadMoveInput();
            bool isSprinting = IsSprintPressed();
            float currentSpeed = isSprinting ? SprintSpeed : WalkSpeed;

            Vector3 move = transform.right * moveInput.x + transform.forward * moveInput.y;
            currentMoveDirection = move.sqrMagnitude > 0.001f ? move.normalized : Vector3.zero;
            characterController.Move(move * (currentSpeed * Time.deltaTime));

            // Jump
            if (IsJumpPressed() && isGrounded)
            {
                velocity.y = Mathf.Sqrt(JumpHeight * -2f * Gravity);
                var netClient = NetworkManager.Instance?.Client;
                if (netClient != null && netClient.IsUdpAuthenticated)
                {
                    _ = netClient.SendJumpAsync();
                }
            }

            // Gravity
            velocity.y += Gravity * Time.deltaTime;
            characterController.Move(velocity * Time.deltaTime);
        }

        private void HandleMouseLook()
        {
            if (Cursor.lockState != CursorLockMode.Locked) return;

            Vector2 mouseDelta = ReadMouseDelta();
            float mouseX = mouseDelta.x * MouseSensitivity * 0.1f;
            float mouseY = mouseDelta.y * MouseSensitivity * 0.1f;

            // Horizontal Yaw
            transform.Rotate(Vector3.up * mouseX);

            // Vertical Pitch
            cameraPitch -= mouseY;
            cameraPitch = Mathf.Clamp(cameraPitch, MinPitchAngle, MaxPitchAngle);

            // Smooth camera recoil recovery
            cameraRecoilPitch = Mathf.Lerp(cameraRecoilPitch, 0f, Time.deltaTime * 10f);

            if (cameraHolder != null)
            {
                cameraHolder.localRotation = Quaternion.Euler(cameraPitch - cameraRecoilPitch, 0f, 0f);
            }
        }

        private void ApplyCameraRecoil(float amount)
        {
            cameraRecoilPitch += amount;
            cameraRecoilPitch = Mathf.Clamp(cameraRecoilPitch, 0f, 15f);
        }

        private void HandleCursorToggle()
        {
            bool escapePressed = false;
            bool clickPressed = false;

#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null)
            {
                escapePressed = Keyboard.current.escapeKey.wasPressedThisFrame;
            }
            if (Mouse.current != null)
            {
                clickPressed = Mouse.current.leftButton.wasPressedThisFrame;
            }
#else
            escapePressed = Input.GetKeyDown(KeyCode.Escape);
            clickPressed = Input.GetMouseButtonDown(0);
#endif

            if (escapePressed)
            {
                SetCursorLock(Cursor.lockState != CursorLockMode.Locked);
            }
            else if (clickPressed && Cursor.lockState != CursorLockMode.Locked)
            {
                SetCursorLock(true);
            }
        }

        public void SetCursorLock(bool locked)
        {
            Cursor.lockState = locked ? CursorLockMode.Locked : CursorLockMode.None;
            Cursor.visible = !locked;
        }

        private Vector2 ReadMoveInput()
        {
            float horizontal = 0f;
            float vertical = 0f;

#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null)
            {
                if (Keyboard.current.wKey.isPressed) vertical += 1f;
                if (Keyboard.current.sKey.isPressed) vertical -= 1f;
                if (Keyboard.current.aKey.isPressed) horizontal -= 1f;
                if (Keyboard.current.dKey.isPressed) horizontal += 1f;
            }
#else
            horizontal = Input.GetAxisRaw("Horizontal");
            vertical = Input.GetAxisRaw("Vertical");
#endif

            Vector2 input = new Vector2(horizontal, vertical);
            if (input.sqrMagnitude > 1f) input.Normalize();
            return input;
        }

        private Vector2 ReadMouseDelta()
        {
#if ENABLE_INPUT_SYSTEM
            if (Mouse.current != null)
            {
                return Mouse.current.delta.ReadValue();
            }
            return Vector2.zero;
#else
            return new Vector2(Input.GetAxis("Mouse X") * 10f, Input.GetAxis("Mouse Y") * 10f);
#endif
        }

        private bool IsSprintPressed()
        {
#if ENABLE_INPUT_SYSTEM
            return Keyboard.current != null && Keyboard.current.leftShiftKey.isPressed;
#else
            return Input.GetKey(KeyCode.LeftShift);
#endif
        }

        private bool IsJumpPressed()
        {
#if ENABLE_INPUT_SYSTEM
            return Keyboard.current != null && Keyboard.current.spaceKey.wasPressedThisFrame;
#else
            return Input.GetKeyDown(KeyCode.Space);
#endif
        }

        // Network position synchronization from server
        public void UpdateNetworkTransform(Vector3 position, Quaternion rotation)
        {
            targetNetworkPosition = position;
            targetNetworkRotation = rotation;
        }

        public void SyncServerPosition(Vector3 serverPosition)
        {
            hasSyncedServerSpawn = true;
            if (characterController != null)
            {
                characterController.enabled = false;
                transform.position = serverPosition;
                velocity = Vector3.zero;
                characterController.enabled = true;
            }
            else
            {
                transform.position = serverPosition;
            }
        }

        public void SetPlayerName(string newName)
        {
            playerName = newName;
            if (nameTagTextMesh != null)
            {
                nameTagTextMesh.text = playerName;
            }
        }

        public void ApplyServerConfig(Protocol.InfoHandshakePacket info)
        {
            if (info == null) return;

            playerSO = playerSO != null ? Instantiate(playerSO) : ScriptableObject.CreateInstance<PlayerSO>();

            if (info.MoveSpeed > 0f) playerSO.walkSpeed = info.MoveSpeed;
            if (info.SprintSpeed > 0f) playerSO.sprintSpeed = info.SprintSpeed;

            if (info.Gravity > 0f)
            {
                playerSO.gravity = -Mathf.Abs(info.Gravity);
            }

            if (info.JumpSpeed > 0f)
            {
                float absGravity = Mathf.Abs(playerSO.gravity);
                if (absGravity > 0.001f)
                {
                    playerSO.jumpHeight = (info.JumpSpeed * info.JumpSpeed) / (2f * absGravity);
                }
            }

            if (info.AttackPower > 0) playerSO.damage = info.AttackPower;
            if (info.MaxAmmo > 0) playerSO.magazineSize = info.MaxAmmo;

            if (shooter != null)
            {
                shooter.PlayerData = playerSO;
                shooter.ApplyServerAmmoConfig(playerSO.magazineSize);
            }

            // World::Init에서 팀 배정(teamId > 0) 및 스폰 위치가 확정되어 온 경우 위치 동기화
            if (info.TeamId != 0)
            {
                Vector3 spawnPos = new Vector3(info.SpawnX, Mathf.Max(info.SpawnY, 0.05f), info.SpawnZ);
                SyncServerPosition(spawnPos);
            }
        }
    }
}
