/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Matrix3x4.h>
#include <EMotionFX/Source/Node.h>
#include <EMotionFX/Source/Pose.h>
#include <EMotionFX/Source/Skeleton.h>

namespace AZ::Render::Internal
{
    inline bool RequiresAffineSkinning(const EMotionFX::Skeleton& skeleton,
        const EMotionFX::Pose& pose, const EMotionFX::Pose& bindPose)
    {
#ifndef EMFX_SCALE_DISABLED
        for (size_t index = 0; index < skeleton.GetNumNodes(); ++index)
        {
            const auto& scale = pose.GetLocalSpaceTransform(index).m_scale;
            const auto& bindScale = bindPose.GetLocalSpaceTransform(index).m_scale;
            if (!scale.IsClose(AZ::Vector3(scale.GetX()), 1e-5f) ||
                !bindScale.IsClose(AZ::Vector3(bindScale.GetX()), 1e-5f))
            {
                return true;
            }
        }
#else
        AZ_UNUSED(skeleton);
        AZ_UNUSED(pose);
        AZ_UNUSED(bindPose);
#endif
        return false;
    }

    inline AZ::Matrix3x4 MakeAffineMatrix(const EMotionFX::Transform& transform)
    {
        auto matrix = AZ::Matrix3x4::CreateFromQuaternionAndTranslation(transform.m_rotation, transform.m_position);
#ifndef EMFX_SCALE_DISABLED
        matrix.MultiplyByScale(transform.m_scale);
#endif
        return matrix;
    }

    //! Preserve nonuniform scale and the shear it produces through a rotated hierarchy.
    //! EMotionFX's model-space QST and ToAZTransform cannot represent those affine transforms.
    inline void BuildAffineSkinningMatrices(const EMotionFX::Skeleton& skeleton,
        const EMotionFX::Pose& pose, const EMotionFX::Pose& bindPose, AZ::Matrix3x4* matrices)
    {
        const auto visit = [&](const auto& self, size_t index,
            const AZ::Matrix3x4& parentPose, const AZ::Matrix3x4& parentBind) -> void
        {
            const auto modelPose = parentPose * MakeAffineMatrix(pose.GetLocalSpaceTransform(index));
            const auto modelBind = parentBind * MakeAffineMatrix(bindPose.GetLocalSpaceTransform(index));
            matrices[index] = modelPose * modelBind.GetInverseFull();

            const auto* node = skeleton.GetNode(index);
            for (size_t child = 0; child < node->GetNumChildNodes(); ++child)
            {
                self(self, node->GetChildIndex(child), modelPose, modelBind);
            }
        };

        const auto identity = AZ::Matrix3x4::CreateIdentity();
        for (size_t root = 0; root < skeleton.GetNumRootNodes(); ++root)
        {
            visit(visit, skeleton.GetRootNodeIndex(root), identity, identity);
        }
    }
}
