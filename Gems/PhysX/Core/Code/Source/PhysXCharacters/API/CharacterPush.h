#pragma once
#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace PhysX
{
// One force budget per character move, shared across bodies and compound shapes.
// The controller resolves its movement against the body; this is the only push.
class CharacterPush
{
    struct Contact
    {
        physx::PxRigidDynamic* body;
        physx::PxVec3 point, direction;
        float alignment;
    };
    std::vector<Contact> m_contacts;
    physx::PxVec3 m_direction{0};
    float m_strength = -1, m_dt = 0;
public:
    bool SetStrength(float newtons)
    {
        if (!std::isfinite(newtons) || newtons < -1 || (newtons < 0 && newtons != -1)) return false;
        m_strength = newtons;
        return true;
    }
    bool Enabled() const { return m_strength >= 0; }
    void Begin(const physx::PxVec3& movement, float dt)
    {
        m_contacts.clear();
        m_dt = std::isfinite(dt) && dt > 0 ? dt : 0;
        m_direction = physx::PxVec3(movement.x, movement.y, 0);
        if (!m_direction.isFinite() || m_direction.magnitudeSquared() < 1.e-12f) m_direction = physx::PxVec3(0);
        else m_direction.normalize();
    }
    void Hit(physx::PxRigidDynamic& body, const physx::PxVec3& point, const physx::PxVec3& normal)
    {
        if (!Enabled() || m_strength == 0 || m_dt == 0 || !point.isFinite() || !normal.isFinite()
            || body.getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)
            || std::abs(normal.z) >= .5f) return;
        physx::PxVec3 direction(-normal.x, -normal.y, 0);
        if (direction.normalize() < 1.e-6f) return;
        const float alignment = std::clamp(m_direction.dot(direction), 0.f, 1.f);
        if (alignment <= 0) return;
        const Contact contact{&body, point, direction, alignment};
        for (auto& previous : m_contacts)
        {
            if (previous.body != &body) continue;
            if (alignment > previous.alignment) previous = contact;
            return;
        }
        m_contacts.push_back(contact);
    }
    void Apply()
    {
        float total = 0;
        for (const auto& contact : m_contacts) total += contact.alignment;
        const float budget = std::max(m_strength, 0.f) * m_dt / std::max(total, 1.f);
        for (const auto& contact : m_contacts)
        {
            const auto impulse = contact.direction * (budget * contact.alignment);
            if (impulse.magnitudeSquared() > 0)
                physx::PxRigidBodyExt::addForceAtPos(*contact.body, impulse, contact.point, physx::PxForceMode::eIMPULSE);
        }
        m_contacts.clear();
    }
};
}
