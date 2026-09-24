using System;
using System.Collections;
using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif
using FPSGame.Combat;

namespace FPSGame.Player
{
    public class PlayerShooter : MonoBehaviour
    {
        [Header("Weapon Stats")]
        [SerializeField] private float damage = 25f;
        [SerializeField] private float fireRate = 0.12f; // Seconds between shots
        [SerializeField] private float range = 100f;
        [SerializeField] private int magazineSize = 30;
        [SerializeField] private int maxReserveAmmo = 120;
        [SerializeField] private float reloadTime = 1.5f;

        [Header("Spread & Recoil")]
        [SerializeField] private float baseSpread = 0.005f;
        [SerializeField] private float sprintSpread = 0.02f;
        [SerializeField] private float cameraRecoilAmount = 1.2f;

        [Header("References")]
        [SerializeField] private Transform muzzlePoint;
        [SerializeField] private Camera playerCamera;
        [SerializeField] private Light muzzleFlashLight;
        [SerializeField] private GameObject impactEffectPrefab;
        [SerializeField] private GameObject bulletTracerPrefab;
        [SerializeField] private PlayerWeaponSway weaponSway;
        [SerializeField] private AudioSource audioSource;
        [SerializeField] private AudioClip shootSoundClip;

        // Current state
        private int currentAmmo;
        private int reserveAmmo;
        private float nextFireTime = 0f;
        private bool isReloading = false;

        // Events for UI
        public event Action<int, int> OnAmmoChanged;
        public event Action<bool> OnHitTarget; // isHeadshot
        public event Action OnShoot;

        public int CurrentAmmo => currentAmmo;
        public int ReserveAmmo => reserveAmmo;
        public bool IsReloading => isReloading;

        private void Awake()
        {
            currentAmmo = magazineSize;
            reserveAmmo = maxReserveAmmo;

            if (audioSource == null)
            {
                audioSource = GetComponent<AudioSource>();
                if (audioSource == null)
                {
                    audioSource = gameObject.AddComponent<AudioSource>();
                }
            }

            if (muzzleFlashLight != null)
            {
                muzzleFlashLight.enabled = false;
            }

            if (playerCamera == null)
            {
                playerCamera = GetComponentInChildren<Camera>(true);
            }

            if (weaponSway == null)
            {
                weaponSway = GetComponentInChildren<PlayerWeaponSway>(true);
            }

            if (muzzlePoint == null)
            {
                Transform foundMuzzle = transform.Find("CameraHolder/WeaponRoot/MuzzlePoint");
                if (foundMuzzle != null) muzzlePoint = foundMuzzle;
                else if (playerCamera != null) muzzlePoint = playerCamera.transform;
            }

            // Create procedural gun shoot audio clip if none assigned
            if (shootSoundClip == null)
            {
                shootSoundClip = CreateProceduralGunshotClip();
            }
        }

        private void Start()
        {
            OnAmmoChanged?.Invoke(currentAmmo, reserveAmmo);
        }

        public void HandleShooting(bool isSprinting, Action<float> onApplyCameraRecoil)
        {
            if (isReloading) return;

            bool isFirePressed = false;
            bool isReloadPressed = false;

#if ENABLE_INPUT_SYSTEM
            if (Mouse.current != null)
            {
                isFirePressed = Mouse.current.leftButton.isPressed;
            }
            if (Keyboard.current != null)
            {
                isReloadPressed = Keyboard.current.rKey.wasPressedThisFrame;
            }
#else
            isFirePressed = Input.GetMouseButton(0);
            isReloadPressed = Input.GetKeyDown(KeyCode.R);
#endif

            // Reload manual trigger
            if (isReloadPressed && currentAmmo < magazineSize && reserveAmmo > 0)
            {
                StartCoroutine(ReloadRoutine());
                return;
            }

            // Shooting
            if (isFirePressed && Time.time >= nextFireTime)
            {
                if (currentAmmo > 0)
                {
                    nextFireTime = Time.time + fireRate;
                    Shoot(isSprinting, onApplyCameraRecoil);
                }
                else if (reserveAmmo > 0)
                {
                    StartCoroutine(ReloadRoutine());
                }
            }
        }

        private void Shoot(bool isSprinting, Action<float> onApplyCameraRecoil)
        {
            currentAmmo--;
            OnAmmoChanged?.Invoke(currentAmmo, reserveAmmo);
            OnShoot?.Invoke();

            // 1. Recoil on weapon model and camera
            if (weaponSway != null)
            {
                weaponSway.ApplyRecoil();
            }
            onApplyCameraRecoil?.Invoke(cameraRecoilAmount);

            // 2. Audio & Muzzle Flash
            PlayShootEffects();

            // 3. Raycast shooting with spread
            if (playerCamera == null) return;

            float currentSpread = isSprinting ? sprintSpread : baseSpread;
            Vector3 shootDir = playerCamera.transform.forward;
            shootDir += playerCamera.transform.right * UnityEngine.Random.Range(-currentSpread, currentSpread);
            shootDir += playerCamera.transform.up * UnityEngine.Random.Range(-currentSpread, currentSpread);
            shootDir.Normalize();

            Vector3 origin = playerCamera.transform.position;
            Ray ray = new Ray(origin, shootDir);
            Vector3 hitPoint = origin + shootDir * range;

            if (Physics.Raycast(ray, out RaycastHit hit, range))
            {
                hitPoint = hit.point;

                // Check Damageable
                IDamageable damageable = hit.collider.GetComponentInParent<IDamageable>();
                bool isHeadshot = hit.collider.GetComponent<HeadHitbox>() != null || hit.collider.name.ToLower().Contains("head");

                if (damageable != null)
                {
                    damageable.TakeDamage(damage, hit.point, hit.normal, isHeadshot);
                    OnHitTarget?.Invoke(isHeadshot);
                }

                // Physics knockback on rigidbodies
                if (hit.rigidbody != null && !hit.rigidbody.isKinematic)
                {
                    hit.rigidbody.AddForceAtPosition(shootDir * (damage * 10f), hit.point, ForceMode.Impulse);
                }

                // Spawn Impact Effect
                SpawnImpactEffect(hit.point, hit.normal);
            }

            // Spawn Tracer
            SpawnTracer(hitPoint);
        }

        private void PlayShootEffects()
        {
            if (audioSource != null && shootSoundClip != null)
            {
                audioSource.pitch = UnityEngine.Random.Range(0.92f, 1.08f);
                audioSource.PlayOneShot(shootSoundClip, 0.8f);
            }

            if (muzzleFlashLight != null)
            {
                StartCoroutine(MuzzleFlashRoutine());
            }
        }

        private IEnumerator MuzzleFlashRoutine()
        {
            muzzleFlashLight.enabled = true;
            yield return new WaitForSeconds(0.04f);
            muzzleFlashLight.enabled = false;
        }

        private void SpawnImpactEffect(Vector3 position, Vector3 normal)
        {
            if (impactEffectPrefab != null)
            {
                GameObject effectObj = Instantiate(impactEffectPrefab, position, Quaternion.LookRotation(normal));
                HitImpactEffect effect = effectObj.GetComponent<HitImpactEffect>();
                if (effect != null)
                {
                    effect.Setup(position, normal, new Color(1f, 0.7f, 0.3f));
                }
            }
        }

        private void SpawnTracer(Vector3 targetPoint)
        {
            if (bulletTracerPrefab != null && muzzlePoint != null)
            {
                GameObject tracerObj = Instantiate(bulletTracerPrefab);
                BulletTracer tracer = tracerObj.GetComponent<BulletTracer>();
                if (tracer != null)
                {
                    tracer.Initialize(muzzlePoint.position, targetPoint);
                }
            }
        }

        private IEnumerator ReloadRoutine()
        {
            if (isReloading || reserveAmmo <= 0 || currentAmmo >= magazineSize) yield break;

            isReloading = true;
            yield return new WaitForSeconds(reloadTime);

            int neededAmmo = magazineSize - currentAmmo;
            int ammoToAdd = Mathf.Min(neededAmmo, reserveAmmo);

            currentAmmo += ammoToAdd;
            reserveAmmo -= ammoToAdd;

            isReloading = false;
            OnAmmoChanged?.Invoke(currentAmmo, reserveAmmo);
        }

        public void PlayRemoteFireEffect(Vector3 targetPoint)
        {
            PlayShootEffects();
            SpawnTracer(targetPoint);
        }

        private AudioClip CreateProceduralGunshotClip()
        {
            int sampleRate = 44100;
            float duration = 0.22f;
            int sampleCount = (int)(sampleRate * duration);
            float[] samples = new float[sampleCount];

            // Generate crisp gunshot pop and decaying noise
            for (int i = 0; i < sampleCount; i++)
            {
                float t = (float)i / sampleCount;
                float envelope = Mathf.Exp(-t * 22f); // Sharp decay

                // Low bass kick + high snappy noise
                float bass = Mathf.Sin(2f * Mathf.PI * 130f * (1f - t) * ((float)i / sampleRate));
                float noise = (UnityEngine.Random.value * 2f - 1f);

                samples[i] = (bass * 0.6f + noise * 0.4f) * envelope;
            }

            AudioClip clip = AudioClip.Create("ProceduralGunshot", sampleCount, 1, sampleRate, false);
            clip.SetData(samples, 0);
            return clip;
        }
    }
}
