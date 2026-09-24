using System.Collections;
using UnityEngine;

namespace FPSGame.Combat
{
    [RequireComponent(typeof(LineRenderer))]
    public class BulletTracer : MonoBehaviour
    {
        [SerializeField] private float duration = 0.05f;
        private LineRenderer lineRenderer;

        private void Awake()
        {
            lineRenderer = GetComponent<LineRenderer>();
        }

        public void Initialize(Vector3 startPoint, Vector3 endPoint)
        {
            lineRenderer.positionCount = 2;
            lineRenderer.SetPosition(0, startPoint);
            lineRenderer.SetPosition(1, endPoint);

            StartCoroutine(FadeAndDestroy());
        }

        private IEnumerator FadeAndDestroy()
        {
            float elapsed = 0f;
            Color startColor = lineRenderer.startColor;
            Color endColor = lineRenderer.endColor;

            while (elapsed < duration)
            {
                elapsed += Time.deltaTime;
                float alpha = Mathf.Lerp(1f, 0f, elapsed / duration);
                
                Color c1 = startColor; c1.a = alpha;
                Color c2 = endColor; c2.a = alpha;
                lineRenderer.startColor = c1;
                lineRenderer.endColor = c2;
                
                yield return null;
            }

            Destroy(gameObject);
        }
    }
}
