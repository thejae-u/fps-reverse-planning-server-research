using UnityEngine;

namespace FPSGame.Combat
{
    public interface IDamageable
    {
        void TakeDamage(float damage, Vector3 hitPoint, Vector3 hitNormal, bool isHeadshot = false);
    }
}
