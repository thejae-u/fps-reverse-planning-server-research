using System.IO;
using UnityEngine;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine.UI;
using FPSGame.Player;
using FPSGame.Combat;
using FPSGame.UI;
using FPSGame.Management;

namespace FPSGame.Editor
{
    public static class FPSSetupTool
    {
        private const string PREFAB_DIR = "Assets/Prefabs";
        private const string MATERIAL_DIR = "Assets/Materials";
        private const string SCENE_PATH = "Assets/Scenes/SampleScene.unity";

        [InitializeOnLoadMethod]
        private static void OnEditorLoaded()
        {
            // Auto run setup if player prefab is not created yet
            EditorApplication.delayCall += () =>
            {
                if (!File.Exists(Path.Combine(Application.dataPath, "Prefabs/Player.prefab")))
                {
                    Debug.Log("[FPSSetupTool] Player prefab not found. Auto running FPS Setup...");
                    SetupEverything(false);
                }
            };
        }

        [MenuItem("Tools/FPS Sample/1. Setup Entire Scene & Prefabs", false, 1)]
        public static void SetupEverythingMenu()
        {
            SetupEverything(true);
        }

        public static void SetupEverything(bool showDialog = true)
        {
            EnsureDirectories();

            Material floorMat = CreateOrGetMaterial("M_Floor", new Color(0.2f, 0.22f, 0.25f), 0.2f, 0.5f);
            Material wallMat = CreateOrGetMaterial("M_Wall", new Color(0.35f, 0.38f, 0.42f), 0.1f, 0.3f);
            Material obstacleMat = CreateOrGetMaterial("M_Obstacle", new Color(0.18f, 0.52f, 0.75f), 0.4f, 0.6f);
            Material targetBodyMat = CreateOrGetMaterial("M_TargetBody", new Color(0.9f, 0.35f, 0.15f), 0.1f, 0.5f);
            Material targetHeadMat = CreateOrGetMaterial("M_TargetHead", new Color(0.95f, 0.8f, 0.1f), 0.2f, 0.6f);
            Material gunMat = CreateOrGetMaterial("M_Gun", new Color(0.12f, 0.12f, 0.14f), 0.8f, 0.7f);
            Material playerBodyMat = CreateOrGetMaterial("M_PlayerBody", new Color(0.25f, 0.45f, 0.65f), 0.3f, 0.5f);
            Material playerVisorMat = CreateOrGetMaterial("M_PlayerVisor", new Color(0.1f, 0.9f, 0.9f), 0.9f, 0.9f, true);
            Material tracerMat = CreateUnlitMaterial("M_BulletTracer", new Color(1f, 0.85f, 0.3f, 1f));

            GameObject tracerPrefab = CreateBulletTracerPrefab(tracerMat);
            GameObject impactPrefab = CreateImpactEffectPrefab();
            GameObject targetDummyPrefab = CreateTargetDummyPrefab(targetBodyMat, targetHeadMat, wallMat);
            GameObject playerPrefab = CreatePlayerPrefab(gunMat, playerBodyMat, playerVisorMat, tracerPrefab, impactPrefab);

            SetupScene(playerPrefab, targetDummyPrefab, floorMat, wallMat, obstacleMat);

            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();

            Debug.Log("[FPSSetupTool] 1인칭 FPS 샘플 씬과 플레이어 프리팹(최대 10인 슬롯 지원) 세팅이 완료되었습니다!");
            if (showDialog)
            {
                EditorUtility.DisplayDialog("FPS Setup Complete", "1인칭 FPS 샘플 씬과 플레이어 프리팹(최대 10인 슬롯 지원) 세팅이 완료되었습니다!\nPlay 버튼을 누르면 즉시 플레이할 수 있습니다.", "확인");
            }
        }

        private static void EnsureDirectories()
        {
            if (!AssetDatabase.IsValidFolder(PREFAB_DIR))
                AssetDatabase.CreateFolder("Assets", "Prefabs");
            if (!AssetDatabase.IsValidFolder(MATERIAL_DIR))
                AssetDatabase.CreateFolder("Assets", "Materials");
        }

        private static Material CreateOrGetMaterial(string name, Color color, float metallic, float smoothness, bool isEmissive = false)
        {
            string path = $"{MATERIAL_DIR}/{name}.mat";
            Material mat = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (mat == null)
            {
                Shader shader = Shader.Find("Universal Render Pipeline/Lit");
                if (shader == null) shader = Shader.Find("Standard");
                mat = new Material(shader);
                AssetDatabase.CreateAsset(mat, path);
            }

            if (mat.HasProperty("_BaseColor")) mat.SetColor("_BaseColor", color);
            else if (mat.HasProperty("_Color")) mat.color = color;

            if (mat.HasProperty("_Metallic")) mat.SetFloat("_Metallic", metallic);
            if (mat.HasProperty("_Smoothness")) mat.SetFloat("_Smoothness", smoothness);

            if (isEmissive)
            {
                if (mat.HasProperty("_EmissionColor"))
                {
                    mat.EnableKeyword("_EMISSION");
                    mat.SetColor("_EmissionColor", color * 2.5f);
                }
            }

            EditorUtility.SetDirty(mat);
            return mat;
        }

        private static Material CreateUnlitMaterial(string name, Color color)
        {
            string path = $"{MATERIAL_DIR}/{name}.mat";
            Material mat = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (mat == null)
            {
                Shader shader = Shader.Find("Universal Render Pipeline/Unlit");
                if (shader == null) shader = Shader.Find("Unlit/Color");
                mat = new Material(shader);
                AssetDatabase.CreateAsset(mat, path);
            }

            if (mat.HasProperty("_BaseColor")) mat.SetColor("_BaseColor", color);
            else if (mat.HasProperty("_Color")) mat.color = color;

            EditorUtility.SetDirty(mat);
            return mat;
        }

        private static GameObject CreateBulletTracerPrefab(Material tracerMat)
        {
            string path = $"{PREFAB_DIR}/BulletTracer.prefab";
            GameObject go = new GameObject("BulletTracer");
            LineRenderer lr = go.AddComponent<LineRenderer>();
            lr.material = tracerMat;
            lr.startWidth = 0.035f;
            lr.endWidth = 0.015f;
            lr.startColor = new Color(1f, 0.9f, 0.4f, 1f);
            lr.endColor = new Color(1f, 0.5f, 0.1f, 0.2f);
            lr.useWorldSpace = true;

            go.AddComponent<BulletTracer>();

            GameObject prefab = PrefabUtility.SaveAsPrefabAsset(go, path);
            Object.DestroyImmediate(go);
            return prefab;
        }

        private static GameObject CreateImpactEffectPrefab()
        {
            string path = $"{PREFAB_DIR}/HitImpactEffect.prefab";
            GameObject go = new GameObject("HitImpactEffect");
            ParticleSystem ps = go.AddComponent<ParticleSystem>();
            var main = ps.main;
            main.duration = 0.2f;
            main.loop = false;
            main.startLifetime = 0.25f;
            main.startSpeed = 4f;
            main.startSize = 0.05f;
            main.startColor = new Color(1f, 0.8f, 0.2f);

            var emission = ps.emission;
            emission.rateOverTime = 0;
            emission.SetBursts(new ParticleSystem.Burst[] { new ParticleSystem.Burst(0f, 15) });

            var shape = ps.shape;
            shape.shapeType = ParticleSystemShapeType.Cone;
            shape.angle = 45f;
            shape.radius = 0.02f;

            go.AddComponent<HitImpactEffect>();

            GameObject prefab = PrefabUtility.SaveAsPrefabAsset(go, path);
            Object.DestroyImmediate(go);
            return prefab;
        }

        private static GameObject CreateTargetDummyPrefab(Material bodyMat, Material headMat, Material poleMat)
        {
            string path = $"{PREFAB_DIR}/TargetDummy.prefab";
            GameObject root = new GameObject("TargetDummy");

            // Base stand
            GameObject stand = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            stand.name = "StandBase";
            stand.transform.SetParent(root.transform);
            stand.transform.localPosition = new Vector3(0, 0.05f, 0);
            stand.transform.localScale = new Vector3(0.8f, 0.05f, 0.8f);
            stand.GetComponent<Renderer>().sharedMaterial = poleMat;

            // Pivot for knockdown
            GameObject pivot = new GameObject("PivotKnockdown");
            pivot.transform.SetParent(root.transform);
            pivot.transform.localPosition = new Vector3(0, 0.1f, 0);

            // Pole
            GameObject pole = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            pole.name = "Pole";
            pole.transform.SetParent(pivot.transform);
            pole.transform.localPosition = new Vector3(0, 0.6f, 0);
            pole.transform.localScale = new Vector3(0.08f, 0.6f, 0.08f);
            pole.GetComponent<Renderer>().sharedMaterial = poleMat;

            // Body target
            GameObject bodyTarget = GameObject.CreatePrimitive(PrimitiveType.Cube);
            bodyTarget.name = "BodyTarget";
            bodyTarget.transform.SetParent(pivot.transform);
            bodyTarget.transform.localPosition = new Vector3(0, 1.25f, 0);
            bodyTarget.transform.localScale = new Vector3(0.6f, 0.8f, 0.1f);
            bodyTarget.GetComponent<Renderer>().sharedMaterial = bodyMat;

            // Head target
            GameObject headTarget = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            headTarget.name = "HeadTarget";
            headTarget.transform.SetParent(pivot.transform);
            headTarget.transform.localPosition = new Vector3(0, 1.85f, 0);
            headTarget.transform.localScale = new Vector3(0.35f, 0.35f, 0.35f);
            headTarget.AddComponent<HeadHitbox>();
            headTarget.GetComponent<Renderer>().sharedMaterial = headMat;

            TargetDummy dummy = root.AddComponent<TargetDummy>();
            SerializedObject so = new SerializedObject(dummy);
            so.FindProperty("pivotToKnockdown").objectReferenceValue = pivot.transform;
            so.FindProperty("headCollider").objectReferenceValue = headTarget.GetComponent<Collider>();
            
            SerializedProperty bodyRends = so.FindProperty("bodyRenderers");
            bodyRends.arraySize = 2;
            bodyRends.GetArrayElementAtIndex(0).objectReferenceValue = bodyTarget.GetComponent<Renderer>();
            bodyRends.GetArrayElementAtIndex(1).objectReferenceValue = headTarget.GetComponent<Renderer>();
            so.ApplyModifiedProperties();

            GameObject prefab = PrefabUtility.SaveAsPrefabAsset(root, path);
            Object.DestroyImmediate(root);
            return prefab;
        }

        private static GameObject CreatePlayerPrefab(Material gunMat, Material bodyMat, Material visorMat, GameObject tracerPrefab, GameObject impactPrefab)
        {
            string path = $"{PREFAB_DIR}/Player.prefab";
            GameObject player = new GameObject("Player");

            // Character Controller
            CharacterController cc = player.AddComponent<CharacterController>();
            cc.radius = 0.4f;
            cc.height = 1.8f;
            cc.center = new Vector3(0, 0.9f, 0);
            cc.stepOffset = 0.3f;
            cc.slopeLimit = 45f;

            // AudioSource
            AudioSource audio = player.AddComponent<AudioSource>();
            audio.playOnAwake = false;
            audio.spatialBlend = 0f;

            // PlayerController & PlayerShooter
            PlayerController pc = player.AddComponent<PlayerController>();
            PlayerShooter ps = player.AddComponent<PlayerShooter>();

            // 1. Camera Holder & Camera
            GameObject camHolder = new GameObject("CameraHolder");
            camHolder.transform.SetParent(player.transform);
            camHolder.transform.localPosition = new Vector3(0, 1.6f, 0);

            GameObject camObj = new GameObject("FirstPersonCamera");
            camObj.transform.SetParent(camHolder.transform);
            camObj.transform.localPosition = Vector3.zero;
            camObj.tag = "MainCamera";
            Camera cam = camObj.AddComponent<Camera>();
            cam.nearClipPlane = 0.1f;
            cam.fieldOfView = 75f;
            AudioListener listener = camObj.AddComponent<AudioListener>();

            // Weapon Root & Sway
            GameObject weaponRoot = new GameObject("WeaponRoot");
            weaponRoot.transform.SetParent(camHolder.transform);
            weaponRoot.transform.localPosition = new Vector3(0.24f, -0.2f, 0.45f);
            PlayerWeaponSway sway = weaponRoot.AddComponent<PlayerWeaponSway>();

            // Gun Body
            GameObject gunBody = GameObject.CreatePrimitive(PrimitiveType.Cube);
            gunBody.name = "GunBody";
            gunBody.transform.SetParent(weaponRoot.transform);
            gunBody.transform.localPosition = Vector3.zero;
            gunBody.transform.localScale = new Vector3(0.07f, 0.09f, 0.42f);
            gunBody.GetComponent<Renderer>().sharedMaterial = gunMat;
            Object.DestroyImmediate(gunBody.GetComponent<Collider>());

            // Gun Barrel
            GameObject gunBarrel = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            gunBarrel.name = "GunBarrel";
            gunBarrel.transform.SetParent(weaponRoot.transform);
            gunBarrel.transform.localPosition = new Vector3(0, 0.015f, 0.28f);
            gunBarrel.transform.localRotation = Quaternion.Euler(90f, 0, 0);
            gunBarrel.transform.localScale = new Vector3(0.035f, 0.12f, 0.035f);
            gunBarrel.GetComponent<Renderer>().sharedMaterial = gunMat;
            Object.DestroyImmediate(gunBarrel.GetComponent<Collider>());

            // Gun Magazine
            GameObject gunMag = GameObject.CreatePrimitive(PrimitiveType.Cube);
            gunMag.name = "GunMagazine";
            gunMag.transform.SetParent(weaponRoot.transform);
            gunMag.transform.localPosition = new Vector3(0, -0.1f, 0.05f);
            gunMag.transform.localRotation = Quaternion.Euler(15f, 0, 0);
            gunMag.transform.localScale = new Vector3(0.045f, 0.14f, 0.07f);
            gunMag.GetComponent<Renderer>().sharedMaterial = gunMat;
            Object.DestroyImmediate(gunMag.GetComponent<Collider>());

            // Gun Sight
            GameObject gunSight = GameObject.CreatePrimitive(PrimitiveType.Cube);
            gunSight.name = "GunSight";
            gunSight.transform.SetParent(weaponRoot.transform);
            gunSight.transform.localPosition = new Vector3(0, 0.06f, 0.12f);
            gunSight.transform.localScale = new Vector3(0.02f, 0.03f, 0.04f);
            gunSight.GetComponent<Renderer>().sharedMaterial = gunMat;
            Object.DestroyImmediate(gunSight.GetComponent<Collider>());

            // Muzzle Point & Light
            GameObject muzzlePoint = new GameObject("MuzzlePoint");
            muzzlePoint.transform.SetParent(weaponRoot.transform);
            muzzlePoint.transform.localPosition = new Vector3(0, 0.015f, 0.42f);

            GameObject flashLightObj = new GameObject("MuzzleFlashLight");
            flashLightObj.transform.SetParent(muzzlePoint.transform);
            flashLightObj.transform.localPosition = Vector3.zero;
            Light flashLight = flashLightObj.AddComponent<Light>();
            flashLight.type = LightType.Point;
            flashLight.color = new Color(1f, 0.85f, 0.5f);
            flashLight.range = 5f;
            flashLight.intensity = 2f;
            flashLight.enabled = false;

            // 2. Third Person Model (for remote players)
            GameObject tpModel = new GameObject("ThirdPersonModel");
            tpModel.transform.SetParent(player.transform);
            tpModel.transform.localPosition = Vector3.zero;

            GameObject tpBody = GameObject.CreatePrimitive(PrimitiveType.Capsule);
            tpBody.name = "TP_Body";
            tpBody.transform.SetParent(tpModel.transform);
            tpBody.transform.localPosition = new Vector3(0, 0.9f, 0);
            tpBody.transform.localScale = new Vector3(0.8f, 0.9f, 0.8f);
            tpBody.GetComponent<Renderer>().sharedMaterial = bodyMat;
            Object.DestroyImmediate(tpBody.GetComponent<Collider>());

            GameObject tpVisor = GameObject.CreatePrimitive(PrimitiveType.Cube);
            tpVisor.name = "TP_Visor";
            tpVisor.transform.SetParent(tpModel.transform);
            tpVisor.transform.localPosition = new Vector3(0, 1.55f, 0.28f);
            tpVisor.transform.localScale = new Vector3(0.35f, 0.15f, 0.2f);
            tpVisor.GetComponent<Renderer>().sharedMaterial = visorMat;
            Object.DestroyImmediate(tpVisor.GetComponent<Collider>());

            // 3. NameTag (3D Text)
            GameObject nameTagObj = new GameObject("NameTag");
            nameTagObj.transform.SetParent(player.transform);
            nameTagObj.transform.localPosition = new Vector3(0, 2.1f, 0);
            TextMesh nameTextMesh = nameTagObj.AddComponent<TextMesh>();
            nameTextMesh.alignment = TextAlignment.Center;
            nameTextMesh.anchor = TextAnchor.MiddleCenter;
            nameTextMesh.characterSize = 0.12f;
            nameTextMesh.fontSize = 32;
            nameTextMesh.color = Color.yellow;
            nameTextMesh.text = "Player";

            // Wire up Serialized Properties for PlayerController
            SerializedObject pcSO = new SerializedObject(pc);
            pcSO.FindProperty("cameraHolder").objectReferenceValue = camHolder.transform;
            pcSO.FindProperty("localCamera").objectReferenceValue = cam;
            pcSO.FindProperty("audioListener").objectReferenceValue = listener;
            pcSO.FindProperty("firstPersonModel").objectReferenceValue = weaponRoot;
            pcSO.FindProperty("thirdPersonModel").objectReferenceValue = tpModel;
            pcSO.FindProperty("nameTagTextMesh").objectReferenceValue = nameTextMesh;
            pcSO.FindProperty("shooter").objectReferenceValue = ps;
            pcSO.FindProperty("weaponSway").objectReferenceValue = sway;
            pcSO.ApplyModifiedProperties();

            // Wire up Serialized Properties for PlayerShooter
            SerializedObject psSO = new SerializedObject(ps);
            psSO.FindProperty("muzzlePoint").objectReferenceValue = muzzlePoint.transform;
            psSO.FindProperty("playerCamera").objectReferenceValue = cam;
            psSO.FindProperty("muzzleFlashLight").objectReferenceValue = flashLight;
            psSO.FindProperty("impactEffectPrefab").objectReferenceValue = impactPrefab;
            psSO.FindProperty("bulletTracerPrefab").objectReferenceValue = tracerPrefab;
            psSO.FindProperty("weaponSway").objectReferenceValue = sway;
            psSO.FindProperty("audioSource").objectReferenceValue = audio;
            psSO.ApplyModifiedProperties();

            GameObject prefab = PrefabUtility.SaveAsPrefabAsset(player, path);
            Object.DestroyImmediate(player);
            return prefab;
        }

        private static void SetupScene(GameObject playerPrefab, GameObject targetDummyPrefab, Material floorMat, Material wallMat, Material obstacleMat)
        {
            var scene = EditorSceneManager.OpenScene(SCENE_PATH, OpenSceneMode.Single);

            // Clean up existing scene objects except Directional Light and Global Volume
            GameObject[] rootObjects = scene.GetRootGameObjects();
            foreach (var obj in rootObjects)
            {
                if (obj.GetComponent<Camera>() != null)
                {
                    Object.DestroyImmediate(obj);
                }
                else if (obj.name == "Environment" || obj.name == "GameManager" || obj.name == "HUD_Canvas" || obj.name.StartsWith("TargetDummy"))
                {
                    Object.DestroyImmediate(obj);
                }
            }

            // 1. Environment Root
            GameObject env = new GameObject("Environment");

            // Floor (40 x 40)
            GameObject floor = GameObject.CreatePrimitive(PrimitiveType.Cube);
            floor.name = "Floor";
            floor.transform.SetParent(env.transform);
            floor.transform.localPosition = new Vector3(0, -0.5f, 0);
            floor.transform.localScale = new Vector3(50f, 1f, 50f);
            floor.GetComponent<Renderer>().sharedMaterial = floorMat;

            // Walls (North, South, East, West)
            CreateWall(env.transform, "Wall_North", new Vector3(0, 2f, 25f), new Vector3(50f, 5f, 1f), wallMat);
            CreateWall(env.transform, "Wall_South", new Vector3(0, 2f, -25f), new Vector3(50f, 5f, 1f), wallMat);
            CreateWall(env.transform, "Wall_East", new Vector3(25f, 2f, 0), new Vector3(1f, 5f, 50f), wallMat);
            CreateWall(env.transform, "Wall_West", new Vector3(-25f, 2f, 0), new Vector3(1f, 5f, 50f), wallMat);

            // Obstacles & Covers
            CreateObstacle(env.transform, "Cover_1", new Vector3(-6f, 1f, 5f), new Vector3(3f, 2f, 1f), obstacleMat);
            CreateObstacle(env.transform, "Cover_2", new Vector3(6f, 1f, 8f), new Vector3(1f, 2f, 4f), obstacleMat);
            CreateObstacle(env.transform, "Box_High", new Vector3(-12f, 1.5f, 12f), new Vector3(3f, 3f, 3f), obstacleMat);
            CreateObstacle(env.transform, "Box_Low", new Vector3(-12f, 0.6f, 8.5f), new Vector3(3f, 1.2f, 2f), obstacleMat); // Ramp/Stair step

            // 2. Targets at varying ranges (7m, 12m, 18m, 24m)
            Vector3[] targetPositions = new Vector3[]
            {
                new Vector3(-4f, 0f, 10f),
                new Vector3(0f, 0f, 15f),
                new Vector3(5f, 0f, 18f),
                new Vector3(-8f, 0f, 22f),
                new Vector3(8f, 0f, 24f)
            };

            for (int i = 0; i < targetPositions.Length; i++)
            {
                GameObject dummy = (GameObject)PrefabUtility.InstantiatePrefab(targetDummyPrefab);
                dummy.transform.position = targetPositions[i];
                dummy.transform.rotation = Quaternion.Euler(0, 180f, 0); // Face the player
            }

            // 3. 10 Spawn Points for Multiplayer readiness
            GameObject spawnGroup = new GameObject("SpawnPoints");
            Transform[] spawnPoints = new Transform[10];

            for (int i = 0; i < 10; i++)
            {
                GameObject sp = new GameObject($"SpawnPoint_{i}");
                sp.transform.SetParent(spawnGroup.transform);
                // Grid layout near south wall
                float x = ((i % 5) - 2) * 3f;
                float z = -15f - (i / 5) * 4f;
                sp.transform.position = new Vector3(x, 1f, z);
                sp.transform.rotation = Quaternion.identity; // Looking forward (+Z)
                spawnPoints[i] = sp.transform;
            }

            // 4. UI Canvas & HUD
            GameObject canvasObj = new GameObject("HUD_Canvas");
            Canvas canvas = canvasObj.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvasObj.AddComponent<CanvasScaler>();
            canvasObj.AddComponent<GraphicRaycaster>();

            FPSUIController ui = canvasObj.AddComponent<FPSUIController>();

            // Crosshair Center
            GameObject crosshair = new GameObject("Crosshair");
            crosshair.transform.SetParent(canvasObj.transform, false);
            RectTransform chRect = crosshair.AddComponent<RectTransform>();
            chRect.anchorMin = new Vector2(0.5f, 0.5f);
            chRect.anchorMax = new Vector2(0.5f, 0.5f);
            chRect.sizeDelta = new Vector2(16, 16);

            // Crosshair Bars
            CreateCrosshairBar(crosshair.transform, new Vector2(0, 8), new Vector2(2, 8)); // Top
            CreateCrosshairBar(crosshair.transform, new Vector2(0, -8), new Vector2(2, 8)); // Bottom
            CreateCrosshairBar(crosshair.transform, new Vector2(-8, 0), new Vector2(8, 2)); // Left
            CreateCrosshairBar(crosshair.transform, new Vector2(8, 0), new Vector2(8, 2)); // Right
            Image chCenterDot = CreateCrosshairBar(crosshair.transform, Vector2.zero, new Vector2(3, 3)); // Dot

            // Hit Marker
            GameObject hitMarker = new GameObject("HitMarker");
            hitMarker.transform.SetParent(canvasObj.transform, false);
            RectTransform hmRect = hitMarker.AddComponent<RectTransform>();
            hmRect.anchorMin = new Vector2(0.5f, 0.5f);
            hmRect.anchorMax = new Vector2(0.5f, 0.5f);
            hmRect.sizeDelta = new Vector2(24, 24);
            Image hmImage = hitMarker.AddComponent<Image>();
            hmImage.color = Color.white;
            hitMarker.SetActive(false);

            // Font default
            Font defaultFont = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (defaultFont == null) defaultFont = Resources.GetBuiltinResource<Font>("Arial.ttf");

            // Ammo Text (Bottom-Right)
            Text ammoText = CreateText(canvasObj.transform, "AmmoText", new Vector2(1, 0), new Vector2(1, 0), new Vector2(-40, 40), "30 / 120", 42, TextAnchor.MiddleRight, Color.white, defaultFont);

            // Health Text (Bottom-Left)
            Text healthText = CreateText(canvasObj.transform, "HealthText", new Vector2(0, 0), new Vector2(0, 0), new Vector2(40, 40), "HP: 100 / 100", 36, TextAnchor.MiddleLeft, new Color(0.2f, 1f, 0.4f), defaultFont);

            // Player Count (Top-Center)
            Text playerCountText = CreateText(canvasObj.transform, "PlayerCountText", new Vector2(0.5f, 1), new Vector2(0.5f, 1), new Vector2(0, -35), "Players: 1 / 10 (Press F1 to add dummy)", 24, TextAnchor.MiddleCenter, Color.cyan, defaultFont);

            // Guide Text (Top-Left)
            Text guideText = CreateText(canvasObj.transform, "GuideText", new Vector2(0, 1), new Vector2(0, 1), new Vector2(30, -35), "", 18, TextAnchor.UpperLeft, new Color(0.85f, 0.85f, 0.85f, 0.9f), defaultFont);

            // Bind UI Controller fields
            SerializedObject uiSO = new SerializedObject(ui);
            uiSO.FindProperty("crosshairCenter").objectReferenceValue = chCenterDot;
            uiSO.FindProperty("hitMarkerImage").objectReferenceValue = hmImage;
            uiSO.FindProperty("ammoText").objectReferenceValue = ammoText;
            uiSO.FindProperty("healthText").objectReferenceValue = healthText;
            uiSO.FindProperty("playerCountText").objectReferenceValue = playerCountText;
            uiSO.FindProperty("guideText").objectReferenceValue = guideText;
            uiSO.ApplyModifiedProperties();

            // 5. GameManager
            GameObject gmObj = new GameObject("GameManager");
            GameManager gm = gmObj.AddComponent<GameManager>();

            SerializedObject gmSO = new SerializedObject(gm);
            gmSO.FindProperty("playerPrefab").objectReferenceValue = playerPrefab;
            gmSO.FindProperty("uiController").objectReferenceValue = ui;
            SerializedProperty spArray = gmSO.FindProperty("spawnPoints");
            spArray.arraySize = 10;
            for (int i = 0; i < 10; i++)
            {
                spArray.GetArrayElementAtIndex(i).objectReferenceValue = spawnPoints[i];
            }
            gmSO.ApplyModifiedProperties();

            EditorSceneManager.MarkSceneDirty(scene);
            EditorSceneManager.SaveScene(scene);
        }

        private static void CreateWall(Transform parent, string name, Vector3 pos, Vector3 scale, Material mat)
        {
            GameObject wall = GameObject.CreatePrimitive(PrimitiveType.Cube);
            wall.name = name;
            wall.transform.SetParent(parent);
            wall.transform.localPosition = pos;
            wall.transform.localScale = scale;
            wall.GetComponent<Renderer>().sharedMaterial = mat;
        }

        private static void CreateObstacle(Transform parent, string name, Vector3 pos, Vector3 scale, Material mat)
        {
            GameObject obs = GameObject.CreatePrimitive(PrimitiveType.Cube);
            obs.name = name;
            obs.transform.SetParent(parent);
            obs.transform.localPosition = pos;
            obs.transform.localScale = scale;
            obs.GetComponent<Renderer>().sharedMaterial = mat;
        }

        private static Image CreateCrosshairBar(Transform parent, Vector2 pos, Vector2 size)
        {
            GameObject bar = new GameObject("Bar");
            bar.transform.SetParent(parent, false);
            RectTransform rt = bar.AddComponent<RectTransform>();
            rt.anchoredPosition = pos;
            rt.sizeDelta = size;
            Image img = bar.AddComponent<Image>();
            img.color = new Color(1f, 1f, 1f, 0.85f);
            return img;
        }

        private static Text CreateText(Transform parent, string name, Vector2 anchorMin, Vector2 anchorMax, Vector2 anchoredPos, string text, int fontSize, TextAnchor anchor, Color color, Font font)
        {
            GameObject obj = new GameObject(name);
            obj.transform.SetParent(parent, false);
            RectTransform rt = obj.AddComponent<RectTransform>();
            rt.anchorMin = anchorMin;
            rt.anchorMax = anchorMax;
            rt.pivot = anchorMin;
            rt.anchoredPosition = anchoredPos;
            rt.sizeDelta = new Vector2(600, 100);

            Text txt = obj.AddComponent<Text>();
            txt.text = text;
            txt.font = font;
            txt.fontSize = fontSize;
            txt.alignment = anchor;
            txt.color = color;
            return txt;
        }
    }
}
