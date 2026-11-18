// SPDX-License-Identifier: MIT
#pragma once

#include <extensions/PxTriangleMeshExt.h>
#include <geometry/PxBoxGeometry.h>
#include <geometry/PxGeometryQuery.h>
#include <geometry/PxHeightFieldGeometry.h>
#include <geometry/PxMeshQuery.h>
#include <geometry/PxTriangle.h>
#include <geometry/PxTriangleMeshGeometry.h>

namespace PhysX
{
    inline bool HasWalkableGroundSupport(
        const physx::PxBoxGeometry& footBox, const physx::PxTransform& footPose,
        const physx::PxGeometry& geometry, const physx::PxTransform& shapePose,
        const physx::PxVec3& up, float minNormalDot)
    {
        // A single collider can contain both a tread and a wall. Check the
        // actual overlapping triangles, since a shape overlap has no normal.
        const auto meshSupportsFoot = [&](const auto& mesh)
        {
            physx::PxMeshOverlapUtil overlap;
            const physx::PxU32 count = overlap.findOverlap(footBox, footPose, mesh, shapePose);
            for (physx::PxU32 i = 0; i < count; ++i)
            {
                physx::PxTriangle triangle;
                physx::PxMeshQuery::getTriangle(mesh, shapePose, overlap.getResults()[i], triangle);
                physx::PxVec3 normal;
                triangle.normal(normal);
                if (normal.dot(up) >= minNormalDot)
                {
                    return true;
                }
            }
            return false;
        };

        switch (geometry.getType())
        {
        case physx::PxGeometryType::eTRIANGLEMESH:
            return meshSupportsFoot(static_cast<const physx::PxTriangleMeshGeometry&>(geometry));
        case physx::PxGeometryType::eHEIGHTFIELD:
            return meshSupportsFoot(static_cast<const physx::PxHeightFieldGeometry&>(geometry));
        default:
            break;
        }

        // Sweep a thin slice through the same foot box. Request a separation
        // normal for initial overlap, where a default sweep normal points up
        // even if the slice started against a vertical side.
        const float sliceHalfHeight = physx::PxMin(0.001f, 0.5f * footBox.halfExtents.z);
        const float halfTravel = footBox.halfExtents.z - sliceHalfHeight;
        const physx::PxBoxGeometry slice(footBox.halfExtents.x, footBox.halfExtents.y, sliceHalfHeight);
        const physx::PxTransform start(footPose.p + halfTravel * up, footPose.q);
        physx::PxGeomSweepHit hit;
        return physx::PxGeometryQuery::sweep(
                   -up, 2.0f * halfTravel, slice, start, geometry, shapePose, hit,
                   physx::PxHitFlag::eNORMAL | physx::PxHitFlag::eMTD)
            && hit.normal.dot(up) >= minNormalDot;
    }
}
