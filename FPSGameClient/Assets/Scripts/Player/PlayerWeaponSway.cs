using UnityEngine;

namespace FPSGame.Player
{
    public class PlayerWeaponSway : MonoBehaviour
    {
        [Header("Sway Settings")]
        [SerializeField] private float swayAmount = 1.5f;
        [SerializeField] private float maxSwayAmount = 4.0f;
        [SerializeField] private float swaySmooth = 8.0f;

        [Header("Bobbing Settings")]
        [SerializeField] private float bobbingSpeed = 10f;
        [SerializeField] private float bobbingAmount = 0.03f;
        [SerializeField] private float sprintBobbingMultiplier = 1.4f;

        [Header("Recoil Animation")]
        [SerializeField] private float recoilKickBack = 0.08f;
        [SerializeField] private float recoilRotation = 3.0f;
        [SerializeField] private float recoilRecoverySpeed = 12.0f;

        private Vector3 initialPosition;
        private Quaternion initialRotation;

        private Vector3 targetRecoilPos;
        private Vector3 currentRecoilPos;
        private Vector3 targetRecoilRot;
        private Vector3 currentRecoilRot;

        private float bobTimer = 0f;

        private void Start()
        {
            initialPosition = transform.localPosition;
            initialRotation = transform.localRotation;
        }

        public void UpdateSway(Vector2 mouseInput, bool isMoving, bool isSprinting)
        {
            // 1. Mouse Sway
            float mouseX = Mathf.Clamp(mouseInput.x * swayAmount, -maxSwayAmount, maxSwayAmount);
            float mouseY = Mathf.Clamp(mouseInput.y * swayAmount, -maxSwayAmount, maxSwayAmount);

            Quaternion targetSwayRot = Quaternion.Euler(-mouseY, mouseX, -mouseX * 0.5f);

            // 2. Weapon Bobbing
            Vector3 bobbingOffset = Vector3.zero;
            if (isMoving)
            {
                float speedMultiplier = isSprinting ? sprintBobbingMultiplier : 1f;
                bobTimer += Time.deltaTime * bobbingSpeed * speedMultiplier;
                bobbingOffset.x = Mathf.Cos(bobTimer * 0.5f) * bobbingAmount * 0.5f;
                bobbingOffset.y = Mathf.Sin(bobTimer) * bobbingAmount;
            }
            else
            {
                bobTimer = 0f;
            }

            // 3. Recoil Recovery
            targetRecoilPos = Vector3.Lerp(targetRecoilPos, Vector3.zero, Time.deltaTime * recoilRecoverySpeed);
            currentRecoilPos = Vector3.Lerp(currentRecoilPos, targetRecoilPos, Time.deltaTime * recoilRecoverySpeed);

            targetRecoilRot = Vector3.Lerp(targetRecoilRot, Vector3.zero, Time.deltaTime * recoilRecoverySpeed);
            currentRecoilRot = Vector3.Lerp(currentRecoilRot, targetRecoilRot, Time.deltaTime * recoilRecoverySpeed);

            // Apply All
            transform.localPosition = initialPosition + bobbingOffset + currentRecoilPos;
            transform.localRotation = Quaternion.Slerp(transform.localRotation, initialRotation * targetSwayRot * Quaternion.Euler(currentRecoilRot), Time.deltaTime * swaySmooth);
        }

        public void ApplyRecoil()
        {
            targetRecoilPos += new Vector3(
                Random.Range(-0.01f, 0.01f),
                Random.Range(0.005f, 0.015f),
                -recoilKickBack
            );

            targetRecoilRot += new Vector3(
                -recoilRotation,
                Random.Range(-recoilRotation * 0.4f, recoilRotation * 0.4f),
                Random.Range(-recoilRotation * 0.3f, recoilRotation * 0.3f)
            );
        }
    }
}
