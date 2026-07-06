#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;
using System.IO;
using Network;
using Visualizer;

public class VisualizerSetup : EditorWindow
{
    [MenuItem("Tools/LagComp Visualizer/Auto Setup")]
    public static void PerformSetup()
    {
        Debug.Log("Starting LagComp Visualizer Auto Setup...");

        // 1. Create Directories if they don't exist
        string matFolder = "Assets/Materials";
        string prefabFolder = "Assets/Prefabs";

        if (!Directory.Exists(matFolder))
            Directory.CreateDirectory(matFolder);
        if (!Directory.Exists(prefabFolder))
            Directory.CreateDirectory(prefabFolder);

        AssetDatabase.Refresh();

        // 2. Create Materials
        Material playerMat = SetupMaterial("Assets/Materials/PlayerMat.mat", Color.white, false);
        Material redGhostMat = SetupMaterial("Assets/Materials/PresentGhostMat.mat", new Color(1f, 0f, 0f, 0.4f), true);
        Material greenGhostMat = SetupMaterial("Assets/Materials/RewoundGhostMat.mat", new Color(0f, 1f, 0f, 0.4f), true);

        // 3. Create Prefab GameObjects
        GameObject playerPrefab = CreateAndSaveCapsulePrefab(playerMat, "Assets/Prefabs/PlayerPrefab.prefab");
        GameObject presentGhostPrefab = CreateAndSaveCapsulePrefab(redGhostMat, "Assets/Prefabs/PresentGhostPrefab.prefab");
        GameObject rewoundGhostPrefab = CreateAndSaveCapsulePrefab(greenGhostMat, "Assets/Prefabs/RewoundGhostPrefab.prefab");

        // 4. Setup Scene GameObjects
        // Create or find _NetworkManager
        GameObject netManagerObj = GameObject.Find("_NetworkManager");
        if (netManagerObj == null)
            netManagerObj = new GameObject("_NetworkManager");

        var netManager = netManagerObj.GetComponent<NetworkManager>();
        if (netManager == null)
            netManager = netManagerObj.AddComponent<NetworkManager>();

        var packetHandler = netManagerObj.GetComponent<PacketHandler>();
        if (packetHandler == null)
            packetHandler = netManagerObj.AddComponent<PacketHandler>();

        // Create or find _LagCompVisualizer
        GameObject visualizerObj = GameObject.Find("_LagCompVisualizer");
        if (visualizerObj == null)
            visualizerObj = new GameObject("_LagCompVisualizer");

        var visualizer = visualizerObj.GetComponent<LagCompVisualizer>();
        if (visualizer == null)
            visualizer = visualizerObj.AddComponent<LagCompVisualizer>();

        var lineRenderer = visualizerObj.GetComponent<LineRenderer>();
        if (lineRenderer == null)
            lineRenderer = visualizerObj.AddComponent<LineRenderer>();

        // Configure LineRenderer
        lineRenderer.startWidth = 0.05f;
        lineRenderer.endWidth = 0.05f;
        lineRenderer.positionCount = 2;
        lineRenderer.enabled = false;
        
        // Setup simple line renderer material (Yellow)
        Material lineMat = SetupMaterial("Assets/Materials/LaserMat.mat", Color.yellow, false);
        lineRenderer.sharedMaterial = lineMat;

        // 5. Connect fields on Visualizer
        visualizer.playerPrefab = playerPrefab;
        visualizer.presentGhostPrefab = presentGhostPrefab;
        visualizer.rewoundGhostPrefab = rewoundGhostPrefab;
        visualizer.laserLine = lineRenderer;

        // Mark scene as dirty so Unity knows to save the changes
        if (!Application.isPlaying)
        {
            UnityEditor.SceneManagement.EditorSceneManager.MarkSceneDirty(UnityEngine.SceneManagement.SceneManager.GetActiveScene());
        }

        Debug.Log("LagComp Visualizer Auto Setup Completed Successfully!");
        EditorUtility.DisplayDialog("Setup Complete", "LagComp Visualizer has been successfully set up in your active scene and project assets!", "OK");
    }

    private static Material SetupMaterial(string path, Color color, bool transparent)
    {
        Material mat = AssetDatabase.LoadAssetAtPath<Material>(path);
        
        Shader shader = Shader.Find("Universal Render Pipeline/Lit");
        bool isURP = (shader != null);
        if (!isURP)
        {
            shader = Shader.Find("Standard");
        }

        if (mat == null)
        {
            mat = new Material(shader);
            AssetDatabase.CreateAsset(mat, path);
        }
        else
        {
            mat.shader = shader;
        }

        mat.color = color;

        if (isURP)
        {
            if (transparent)
            {
                mat.SetFloat("_Surface", 1); // 1 = Transparent
                mat.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.SrcAlpha);
                mat.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
                mat.SetInt("_ZWrite", 0);
                mat.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
                mat.renderQueue = 3000;
            }
            else
            {
                mat.SetFloat("_Surface", 0); // 0 = Opaque
                mat.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.One);
                mat.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.Zero);
                mat.SetInt("_ZWrite", 1);
                mat.DisableKeyword("_SURFACE_TYPE_TRANSPARENT");
                mat.renderQueue = -1;
            }
        }
        else
        {
            // Built-in Standard shader properties
            if (transparent)
            {
                mat.SetFloat("_Mode", 2); // 2 = Fade
                mat.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.SrcAlpha);
                mat.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
                mat.SetInt("_ZWrite", 0);
                mat.DisableKeyword("_ALPHATEST_ON");
                mat.EnableKeyword("_ALPHABLEND_ON");
                mat.DisableKeyword("_ALPHAPREMULTIPLY_ON");
                mat.renderQueue = 3000;
            }
            else
            {
                mat.SetFloat("_Mode", 0); // Opaque
                mat.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.One);
                mat.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.Zero);
                mat.SetInt("_ZWrite", 1);
                mat.DisableKeyword("_ALPHATEST_ON");
                mat.DisableKeyword("_ALPHABLEND_ON");
                mat.DisableKeyword("_ALPHAPREMULTIPLY_ON");
                mat.renderQueue = -1;
            }
        }

        EditorUtility.SetDirty(mat);
        return mat;
    }

    private static GameObject CreateAndSaveCapsulePrefab(Material material, string path)
    {
        // Check if prefab already exists
        GameObject existingPrefab = AssetDatabase.LoadAssetAtPath<GameObject>(path);
        if (existingPrefab != null)
        {
            // Update its material just in case
            var renderer = existingPrefab.GetComponent<Renderer>();
            if (renderer != null)
            {
                renderer.sharedMaterial = material;
                EditorUtility.SetDirty(existingPrefab);
            }
            return existingPrefab;
        }

        // Create temporary capsule in scene
        GameObject tempObj = GameObject.CreatePrimitive(PrimitiveType.Capsule);
        tempObj.name = Path.GetFileNameWithoutExtension(path);

        var rendererComp = tempObj.GetComponent<Renderer>();
        if (rendererComp != null)
        {
            rendererComp.sharedMaterial = material;
        }

        // Save as prefab
        GameObject prefabObj = PrefabUtility.SaveAsPrefabAsset(tempObj, path);
        
        // Destroy temporary scene object
        DestroyImmediate(tempObj);

        return prefabObj;
    }
}
#endif
