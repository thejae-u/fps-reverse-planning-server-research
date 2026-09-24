using UnityEngine;

namespace FPSGame.Combat
{
    public class HitImpactEffect : MonoBehaviour
    {
        [SerializeField] private float lifetime = 2f;
        [SerializeField] private ParticleSystem particleSys;

        private void Start()
        {
            if (particleSys == null)
            {
                particleSys = GetComponentInChildren<ParticleSystem>();
            }

            if (particleSys != null)
            {
                particleSys.Play();
            }

            Destroy(gameObject, lifetime);
        }

        public void Setup(Vector3 position, Vector3 normal, Color sparkColor)
        {
            transform.position = position;
            transform.forward = normal;

            if (particleSys != null)
            {
                var main = particleSys.main;
                main.startColor = sparkColor;
            }
        }
    }
}
