using UnityEngine;

[CreateAssetMenu(fileName = "PlayerSO", menuName = "Scriptable Objects/PlayerSO")]
public class PlayerSO : ScriptableObject
{
    [Header("Movement Stats")]
    public float walkSpeed = 5.5f;
    public float sprintSpeed = 9.0f;
    public float jumpHeight = 1.3f;
    public float gravity = -22.0f;

    [Header("Mouse Look Stats")]
    public float mouseSensitivity = 2.0f;
    public float maxPitchAngle = 85.0f;
    public float minPitchAngle = -85.0f;

    [Header("Health Stats")]
    public float maxHealth = 100f;

    [Header("Weapon Stats")] 
    public float damage = 25f;
    public float fireRate = 0.12f; // Seconds between shots
    public float range = 100f;
    public int magazineSize = 30;
    public int maxReserveAmmo = 120;
    public float reloadTime = 1.5f;

    [Header("Spread & Recoil")]
    public float baseSpread = 0.005f;
    public float sprintSpread = 0.02f;
    public float cameraRecoilAmount = 1.2f;
}
