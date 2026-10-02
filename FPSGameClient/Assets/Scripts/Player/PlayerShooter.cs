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
        [Header("Player Data SO")] 
        [SerializeField] private PlayerSO _playerSO;

        public PlayerSO PlayerData
        {
            get => _playerSO;
            set => _playerSO = value;
        }

        public float Damage => _playerSO != null ? _playerSO.damage : 25f;
        public float FireRate => _playerSO != null ? _playerSO.fireRate : 0.12f;
        public float Range => _playerSO != null ? _playerSO.range : 100f;
        public int MagazineSize => _playerSO != null ? _playerSO.magazineSize : 30;
        public int MaxReserveAmmo => _playerSO != null ? _playerSO.maxReserveAmmo : 120;
        public bool IsUnlimitedAmmo => MaxReserveAmmo == -1;
        public float ReloadTime => _playerSO != null ? _playerSO.reloadTime : 1.5f;

        public float BaseSpread => _playerSO != null ? _playerSO.baseSpread : 0.005f;
        public float SprintSpread => _playerSO != null ? _playerSO.sprintSpread : 0.02f;
        public float CameraRecoilAmount => _playerSO != null ? _playerSO.cameraRecoilAmount : 1.2f;

        [Header("References")]
        [SerializeField] private Transform muzzlePoint;
        [SerializeField] private Camera playerCamera;
        [SerializeField] private Light muzzleFlashLight;
        [SerializeField] private GameObject muzzleFlashVisual;
        [SerializeField] private ParticleSystem muzzleFlashParticles;
        [SerializeField] private GameObject impactEffectPrefab;
        [SerializeField] private GameObject bulletTracerPrefab;
        [SerializeField] private PlayerWeaponSway weaponSway;
        [SerializeField] private AudioSource audioSource;
        [SerializeField] private AudioClip shootSoundClip;
        [SerializeField] private AudioClip reloadSoundClip;
        private static AudioClip generatedReloadClip;

        // Current state
        private int currentAmmo;
        private int reserveAmmo;
        private float nextFireTime = 0f;
        private bool isReloading = false;
        private Coroutine muzzleFlashCoroutine;

        // Events for UI
        public event Action<int, int> OnAmmoChanged;
        public event Action<bool> OnHitTarget; // isHeadshot
        public event Action OnShoot;

        public int CurrentAmmo => currentAmmo;
        public int ReserveAmmo => reserveAmmo;
        public bool IsReloading => isReloading;

        private void Awake()
        {
            if (_playerSO == null)
            {
                PlayerController pc = GetComponent<PlayerController>();
                if (pc != null && pc.PlayerData != null)
                {
                    _playerSO = pc.PlayerData;
                }
            }

            currentAmmo = MagazineSize;
            reserveAmmo = MaxReserveAmmo;

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

            /*
            if (weaponSway == null)
            {
                weaponSway = GetComponentInChildren<PlayerWeaponSway>(true);
            }
            */

            if (muzzlePoint == null)
            {
                Transform foundMuzzle = transform.Find("CameraHolder/WeaponRoot/MuzzlePoint");
                if (foundMuzzle != null) muzzlePoint = foundMuzzle;
                else if (playerCamera != null) muzzlePoint = playerCamera.transform;
            }

            EnsureMuzzleFlashEffect();

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
            if (isReloadPressed && currentAmmo < MagazineSize && (IsUnlimitedAmmo || reserveAmmo > 0))
            {
                StartCoroutine(ReloadRoutine());
                return;
            }

            // Shooting
            if (isFirePressed && Time.time >= nextFireTime)
            {
                if (currentAmmo > 0)
                {
                    nextFireTime = Time.time + FireRate;
                    Shoot(isSprinting, onApplyCameraRecoil);
                }
                else if (IsUnlimitedAmmo || reserveAmmo > 0)
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

            /*
            // 1. Recoil on weapon model and camera
            if (weaponSway != null)
            {
                weaponSway.ApplyRecoil();
            }
            onApplyCameraRecoil?.Invoke(CameraRecoilAmount);
            */

            // 2. Audio & Muzzle Flash
            PlayShootEffects();

            // 3. Raycast shooting with spread
            if (playerCamera == null) return;

            float currentSpread = isSprinting ? SprintSpread : BaseSpread;
            Vector3 shootDir = playerCamera.transform.forward;
            shootDir += playerCamera.transform.right * UnityEngine.Random.Range(-currentSpread, currentSpread);
            shootDir += playerCamera.transform.up * UnityEngine.Random.Range(-currentSpread, currentSpread);
            shootDir.Normalize();

            Vector3 origin = playerCamera.transform.position;
            Ray ray = new Ray(origin, shootDir);
            Vector3 hitPoint = origin + shootDir * Range;
            int shootLayerMask = ~(1 << PlayerController.GetWeaponLayer());

            if (Physics.Raycast(ray, out RaycastHit hit, Range, shootLayerMask))
            {
                hitPoint = hit.point;

                // Check Damageable
                IDamageable damageable = hit.collider.GetComponentInParent<IDamageable>();
                bool isHeadshot = hit.collider.GetComponent<HeadHitbox>() != null || hit.collider.name.ToLower().Contains("head");

                if (damageable != null)
                {
                    damageable.TakeDamage(Damage, hit.point, hit.normal, isHeadshot);
                    OnHitTarget?.Invoke(isHeadshot);
                }

                // Physics knockback on rigidbodies
                if (hit.rigidbody != null && !hit.rigidbody.isKinematic)
                {
                    hit.rigidbody.AddForceAtPosition(shootDir * (Damage * 10f), hit.point, ForceMode.Impulse);
                }

                // Spawn Impact Effect
                SpawnImpactEffect(hit.point, hit.normal);
            }
        }

        private void PlayShootEffects()
        {
            if (audioSource != null && shootSoundClip != null)
            {
                audioSource.pitch = UnityEngine.Random.Range(0.92f, 1.08f);
                audioSource.PlayOneShot(shootSoundClip, 0.8f);
            }

            if (muzzleFlashParticles != null)
            {
                muzzleFlashParticles.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                muzzleFlashParticles.Play(true);
            }

            if (muzzleFlashCoroutine != null)
            {
                StopCoroutine(muzzleFlashCoroutine);
            }
            muzzleFlashCoroutine = StartCoroutine(MuzzleFlashRoutine());
        }

        private IEnumerator MuzzleFlashRoutine()
        {
            if (muzzleFlashLight != null)
            {
                muzzleFlashLight.enabled = true;
            }

            if (muzzleFlashVisual != null)
            {
                float randomZ = UnityEngine.Random.Range(0f, 360f);
                float randomScale = UnityEngine.Random.Range(0.85f, 1.2f);
                muzzleFlashVisual.transform.localRotation = Quaternion.Euler(0f, 0f, randomZ);
                muzzleFlashVisual.transform.localScale = Vector3.one * randomScale;
                muzzleFlashVisual.SetActive(true);
            }

            yield return new WaitForSeconds(0.04f);

            if (muzzleFlashVisual != null)
            {
                muzzleFlashVisual.SetActive(false);
            }

            if (muzzleFlashLight != null)
            {
                muzzleFlashLight.enabled = false;
            }

            muzzleFlashCoroutine = null;
        }

        private void EnsureMuzzleFlashEffect()
        {
            if (muzzlePoint == null) return;

            int weaponLayer = PlayerController.GetWeaponLayer();

            if (muzzleFlashVisual == null)
            {
                Transform existing = muzzlePoint.Find("MuzzleFlashVisual");
                if (existing != null)
                {
                    muzzleFlashVisual = existing.gameObject;
                }
            }

            if (muzzleFlashVisual == null)
            {
                muzzleFlashVisual = new GameObject("MuzzleFlashVisual");
                muzzleFlashVisual.layer = weaponLayer;
                muzzleFlashVisual.transform.SetParent(muzzlePoint, false);
                muzzleFlashVisual.transform.localPosition = Vector3.zero;
                muzzleFlashVisual.transform.localRotation = Quaternion.identity;

                Shader unlitShader = Shader.Find("Universal Render Pipeline/Unlit");
                if (unlitShader == null) unlitShader = Shader.Find("Unlit/Color");
                if (unlitShader == null) unlitShader = Shader.Find("Sprites/Default");

                Material outerFlameMat = new Material(unlitShader);
                Color outerColor = new Color(1f, 0.65f, 0.15f, 1f);
                if (outerFlameMat.HasProperty("_BaseColor")) outerFlameMat.SetColor("_BaseColor", outerColor);
                if (outerFlameMat.HasProperty("_Color")) outerFlameMat.color = outerColor;

                Material innerCoreMat = new Material(unlitShader);
                Color coreColor = new Color(1f, 0.95f, 0.7f, 1f);
                if (innerCoreMat.HasProperty("_BaseColor")) innerCoreMat.SetColor("_BaseColor", coreColor);
                if (innerCoreMat.HasProperty("_Color")) innerCoreMat.color = coreColor;

                // Forward flame jet (crossed fins + core)
                CreateMuzzleFlamePart("FlameFin_H", muzzleFlashVisual.transform, weaponLayer,
                    new Vector3(0f, 0f, 0.06f), Quaternion.identity, new Vector3(0.11f, 0.015f, 0.14f), outerFlameMat);
                CreateMuzzleFlamePart("FlameFin_V", muzzleFlashVisual.transform, weaponLayer,
                    new Vector3(0f, 0f, 0.06f), Quaternion.Euler(0f, 0f, 90f), new Vector3(0.11f, 0.015f, 0.14f), outerFlameMat);
                CreateMuzzleFlamePart("FlameStar_1", muzzleFlashVisual.transform, weaponLayer,
                    new Vector3(0f, 0f, 0.02f), Quaternion.Euler(0f, 0f, 45f), new Vector3(0.14f, 0.02f, 0.02f), outerFlameMat);
                CreateMuzzleFlamePart("FlameStar_2", muzzleFlashVisual.transform, weaponLayer,
                    new Vector3(0f, 0f, 0.02f), Quaternion.Euler(0f, 0f, -45f), new Vector3(0.14f, 0.02f, 0.02f), outerFlameMat);
                CreateMuzzleFlamePart("FlameCore", muzzleFlashVisual.transform, weaponLayer,
                    new Vector3(0f, 0f, 0.035f), Quaternion.identity, new Vector3(0.045f, 0.045f, 0.08f), innerCoreMat);
            }

            if (muzzleFlashParticles == null)
            {
                Transform existingPs = muzzlePoint.Find("MuzzleFlashParticles");
                if (existingPs != null)
                {
                    muzzleFlashParticles = existingPs.GetComponent<ParticleSystem>();
                }
            }

            if (muzzleFlashParticles == null)
            {
                GameObject psObj = new GameObject("MuzzleFlashParticles");
                psObj.layer = weaponLayer;
                psObj.transform.SetParent(muzzlePoint, false);
                psObj.transform.localPosition = Vector3.zero;
                psObj.transform.localRotation = Quaternion.identity;

                muzzleFlashParticles = psObj.AddComponent<ParticleSystem>();
                muzzleFlashParticles.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);

                var main = muzzleFlashParticles.main;
                main.playOnAwake = false;
                main.loop = false;
                main.duration = 0.05f;
                main.startLifetime = new ParticleSystem.MinMaxCurve(0.03f, 0.06f);
                main.startSpeed = new ParticleSystem.MinMaxCurve(2.5f, 6.0f);
                main.startSize = new ParticleSystem.MinMaxCurve(0.015f, 0.035f);
                main.startColor = new Color(1f, 0.8f, 0.25f, 1f);
                main.simulationSpace = ParticleSystemSimulationSpace.Local;

                var emission = muzzleFlashParticles.emission;
                emission.rateOverTime = 0f;
                emission.SetBursts(new ParticleSystem.Burst[] { new ParticleSystem.Burst(0f, 10) });

                var shape = muzzleFlashParticles.shape;
                shape.shapeType = ParticleSystemShapeType.Cone;
                shape.angle = 25f;
                shape.radius = 0.01f;

                var psRenderer = psObj.GetComponent<ParticleSystemRenderer>();
                Shader particleShader = Shader.Find("Universal Render Pipeline/Particles/Unlit");
                if (particleShader == null) particleShader = Shader.Find("Particles/Standard Unlit");
                if (particleShader == null) particleShader = Shader.Find("Sprites/Default");
                if (particleShader != null)
                {
                    Material pMat = new Material(particleShader);
                    if (pMat.HasProperty("_BaseColor")) pMat.SetColor("_BaseColor", new Color(1f, 0.8f, 0.3f, 1f));
                    psRenderer.sharedMaterial = pMat;
                }
            }

            muzzleFlashVisual.SetActive(false);
        }

        private static void CreateMuzzleFlamePart(string name, Transform parent, int layer, Vector3 localPos, Quaternion localRot, Vector3 localScale, Material mat)
        {
            GameObject part = GameObject.CreatePrimitive(PrimitiveType.Cube);
            part.name = name;
            part.layer = layer;
            part.transform.SetParent(parent, false);
            part.transform.localPosition = localPos;
            part.transform.localRotation = localRot;
            part.transform.localScale = localScale;

            Collider col = part.GetComponent<Collider>();
            if (col != null) Destroy(col);

            Renderer rend = part.GetComponent<Renderer>();
            if (rend != null)
            {
                rend.sharedMaterial = mat;
                rend.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                rend.receiveShadows = false;
            }
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

        private IEnumerator ReloadRoutine()
        {
            if (isReloading || (!IsUnlimitedAmmo && reserveAmmo <= 0) || currentAmmo >= MagazineSize) yield break;

            isReloading = true;
            if (audioSource != null)
            {
                if (reloadSoundClip == null)
                {
                    if (generatedReloadClip == null) generatedReloadClip = CreateProceduralReloadClip();
                    reloadSoundClip = generatedReloadClip;
                }
                audioSource.PlayOneShot(reloadSoundClip, 0.55f);
            }
            yield return new WaitForSeconds(ReloadTime);

            if (IsUnlimitedAmmo)
            {
                currentAmmo = MagazineSize;
                reserveAmmo = -1;
            }
            else
            {
                int neededAmmo = MagazineSize - currentAmmo;
                int ammoToAdd = Mathf.Min(neededAmmo, reserveAmmo);

                currentAmmo += ammoToAdd;
                reserveAmmo -= ammoToAdd;
            }

            isReloading = false;
            OnAmmoChanged?.Invoke(currentAmmo, reserveAmmo);
        }

        public void ApplyServerAmmoConfig(int magazineSize)
        {
            if (magazineSize > 0)
            {
                currentAmmo = magazineSize;
                OnAmmoChanged?.Invoke(currentAmmo, reserveAmmo);
            }
        }

        public void PlayRemoteFireEffect(Vector3 targetPoint)
        {
            PlayShootEffects();
        }

        private static AudioClip CreateProceduralReloadClip()
        {
            const int sampleRate = 22050;
            const float duration = 1.0f;
            const float clickDuration = 0.12f;
            float[] samples = new float[(int)(sampleRate * duration)];
            float[] clickTimes = { 0.02f, 0.42f, 0.78f };
            var noise = new System.Random(17);

            // 탄창 분리·삽입·장전의 세 번의 짧은 기계음. 게임의 Random 상태는 건드리지 않는다.
            for (int i = 0; i < samples.Length; i++)
            {
                float time = i / (float)sampleRate;
                float value = 0f;
                float random = (float)(noise.NextDouble() * 2.0 - 1.0);
                foreach (float click in clickTimes)
                {
                    float age = time - click;
                    if (age < 0f || age >= clickDuration) continue;
                    float envelope = Mathf.Exp(-age * 65f);
                    float metal = Mathf.Sin(2f * Mathf.PI * 850f * age);
                    value += (random * 0.55f + metal * 0.25f) * envelope;
                }
                samples[i] = Mathf.Clamp(value, -1f, 1f);
            }

            AudioClip clip = AudioClip.Create("ProceduralReload", samples.Length, 1, sampleRate, false);
            clip.SetData(samples, 0);
            return clip;
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
