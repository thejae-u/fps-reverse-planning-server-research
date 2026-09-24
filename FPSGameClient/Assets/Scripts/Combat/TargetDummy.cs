using System.Collections;
using UnityEngine;

namespace FPSGame.Combat
{
    public class TargetDummy : MonoBehaviour, IDamageable
    {
        [Header("Health Settings")]
        [SerializeField] private float maxHealth = 100f;
        private float currentHealth;

        [Header("Target Mesh & Renderers")]
        [SerializeField] private Renderer[] bodyRenderers;
        [SerializeField] private Collider headCollider;
        [SerializeField] private Transform pivotToKnockdown;

        [Header("Respawn")]
        [SerializeField] private float respawnTime = 3f;

        private Color[] originalColors;
        private Coroutine flashCoroutine;
        private bool isDead = false;
        private Quaternion initialRotation;

        public bool IsDead => isDead;

        private void Awake()
        {
            currentHealth = maxHealth;
            if (pivotToKnockdown != null)
            {
                initialRotation = pivotToKnockdown.localRotation;
            }

            if (bodyRenderers == null || bodyRenderers.Length == 0)
            {
                bodyRenderers = GetComponentsInChildren<Renderer>();
            }

            if (bodyRenderers != null && bodyRenderers.Length > 0)
            {
                originalColors = new Color[bodyRenderers.Length];
                for (int i = 0; i < bodyRenderers.Length; i++)
                {
                    if (bodyRenderers[i].material.HasProperty("_BaseColor"))
                        originalColors[i] = bodyRenderers[i].material.GetColor("_BaseColor");
                    else if (bodyRenderers[i].material.HasProperty("_Color"))
                        originalColors[i] = bodyRenderers[i].material.color;
                    else
                        originalColors[i] = Color.white;
                }
            }
        }

        public void TakeDamage(float damage, Vector3 hitPoint, Vector3 hitNormal, bool isHeadshot = false)
        {
            if (isDead) return;

            float finalDamage = isHeadshot ? damage * 2f : damage;
            currentHealth -= finalDamage;

            if (flashCoroutine != null)
            {
                StopCoroutine(flashCoroutine);
            }
            flashCoroutine = StartCoroutine(HitFlashRoutine(isHeadshot ? Color.yellow : Color.red));

            if (currentHealth <= 0f)
            {
                Die();
            }
        }

        private IEnumerator HitFlashRoutine(Color flashColor)
        {
            if (bodyRenderers == null) yield break;

            for (int i = 0; i < bodyRenderers.Length; i++)
            {
                if (bodyRenderers[i] != null && bodyRenderers[i].material != null)
                {
                    if (bodyRenderers[i].material.HasProperty("_BaseColor"))
                        bodyRenderers[i].material.SetColor("_BaseColor", flashColor);
                    else if (bodyRenderers[i].material.HasProperty("_Color"))
                        bodyRenderers[i].material.color = flashColor;
                }
            }

            yield return new WaitForSeconds(0.12f);

            for (int i = 0; i < bodyRenderers.Length; i++)
            {
                if (bodyRenderers[i] != null && bodyRenderers[i].material != null && originalColors != null && i < originalColors.Length)
                {
                    if (bodyRenderers[i].material.HasProperty("_BaseColor"))
                        bodyRenderers[i].material.SetColor("_BaseColor", originalColors[i]);
                    else if (bodyRenderers[i].material.HasProperty("_Color"))
                        bodyRenderers[i].material.color = originalColors[i];
                }
            }
        }

        private void Die()
        {
            isDead = true;
            StartCoroutine(KnockdownAndRespawnRoutine());
        }

        private IEnumerator KnockdownAndRespawnRoutine()
        {
            // Knock down backwards
            if (pivotToKnockdown != null)
            {
                Quaternion startRot = pivotToKnockdown.localRotation;
                Quaternion targetRot = Quaternion.Euler(90f, 0f, 0f) * startRot;

                float t = 0f;
                while (t < 0.25f)
                {
                    t += Time.deltaTime;
                    pivotToKnockdown.localRotation = Quaternion.Slerp(startRot, targetRot, t / 0.25f);
                    yield return null;
                }
            }

            yield return new WaitForSeconds(respawnTime);

            // Stand back up
            if (pivotToKnockdown != null)
            {
                Quaternion startRot = pivotToKnockdown.localRotation;
                float t = 0f;
                while (t < 0.35f)
                {
                    t += Time.deltaTime;
                    pivotToKnockdown.localRotation = Quaternion.Slerp(startRot, initialRotation, t / 0.35f);
                    yield return null;
                }
                pivotToKnockdown.localRotation = initialRotation;
            }

            currentHealth = maxHealth;
            isDead = false;
        }
    }
}
