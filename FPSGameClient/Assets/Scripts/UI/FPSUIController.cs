using System.Collections;
using UnityEngine;
using UnityEngine.UI;
using FPSGame.Player;

namespace FPSGame.UI
{
    public class FPSUIController : MonoBehaviour
    {
        [Header("Crosshair Elements")]
        [SerializeField] private Image crosshairCenter;
        [SerializeField] private Image hitMarkerImage;

        [Header("HUD Text Elements")]
        [SerializeField] private Text ammoText;
        [SerializeField] private Text healthText;
        [SerializeField] private Text playerCountText;
        [SerializeField] private Text guideText;

        private Coroutine hitMarkerCoroutine;

        private void Awake()
        {
            if (ammoText == null) ammoText = transform.Find("AmmoText")?.GetComponent<Text>();
            if (healthText == null) healthText = transform.Find("HealthText")?.GetComponent<Text>();
            if (playerCountText == null) playerCountText = transform.Find("PlayerCountText")?.GetComponent<Text>();
            if (guideText == null) guideText = transform.Find("GuideText")?.GetComponent<Text>();
            if (hitMarkerImage == null) hitMarkerImage = transform.Find("HitMarker")?.GetComponent<Image>();
        }

        public void BindPlayer(PlayerController player)
        {
            if (player == null) return;

            PlayerShooter shooter = player.Shooter;
            if (shooter != null)
            {
                shooter.OnAmmoChanged += UpdateAmmoUI;
                shooter.OnHitTarget += ShowHitMarker;
                UpdateAmmoUI(shooter.CurrentAmmo, shooter.ReserveAmmo);
            }

            SetHealth(100f, 100f);
        }

        public void UpdatePlayerCount(int currentCount, int maxCount)
        {
            if (playerCountText != null)
            {
                playerCountText.text = $"Players: {currentCount} / {maxCount} (Press F1 to add dummy)";
            }
        }

        public void UpdateAmmoUI(int current, int reserve)
        {
            if (ammoText != null)
            {
                ammoText.text = $"{current} / {reserve}";
            }
        }

        public void SetHealth(float current, float max)
        {
            if (healthText != null)
            {
                healthText.text = $"HP: {Mathf.CeilToInt(current)} / {Mathf.CeilToInt(max)}";
            }
        }

        public void ShowHitMarker(bool isHeadshot)
        {
            if (hitMarkerImage == null) return;

            if (hitMarkerCoroutine != null)
            {
                StopCoroutine(hitMarkerCoroutine);
            }

            hitMarkerCoroutine = StartCoroutine(HitMarkerRoutine(isHeadshot));
        }

        private IEnumerator HitMarkerRoutine(bool isHeadshot)
        {
            hitMarkerImage.enabled = true;
            hitMarkerImage.color = isHeadshot ? Color.red : Color.white;

            yield return new WaitForSeconds(0.12f);

            hitMarkerImage.enabled = false;
        }

        private void Start()
        {
            if (hitMarkerImage != null)
            {
                hitMarkerImage.enabled = false;
            }

            if (guideText != null)
            {
                guideText.text = "[WASD] 이동  |  [Shift] 달리기  |  [Space] 점프\n[좌클릭] 사격  |  [R] 재장전  |  [ESC] 커서 잠금 토글\n[F1] 리모트 플레이어 더미 소환 (최대 10명 멀티플레이어 환경 테스트)";
            }
        }
    }
}
