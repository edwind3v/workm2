#pragma once
#include <granny.h>

// Asset proof, not a name-based idle-motion heuristic. Animation clocks and
// game-side events still advance when a proven constant pose is reused.
inline bool IsConstantPoseAnimation(const granny_animation* animation)
{
    if (!animation || animation->TrackGroupCount != 1) return false;
    unsigned tracks = 0;
    for (int groupIndex = 0; groupIndex < animation->TrackGroupCount; ++groupIndex)
    {
        const auto* group = animation->TrackGroups[groupIndex];
        if (!group || group->VectorTrackCount || group->PeriodicLoop ||
            (group->Flags & ~GrannyTrackGroupIsSorted) ||
            group->LoopTranslation[0] != 0 || group->LoopTranslation[1] != 0 || group->LoopTranslation[2] != 0)
            return false;
        for (int trackIndex = 0; trackIndex < group->TransformTrackCount; ++trackIndex)
        {
            const auto& track = group->TransformTracks[trackIndex];
            if (track.Flags || !GrannyCurveIsConstantOrIdentity(&track.PositionCurve) ||
                !GrannyCurveIsConstantOrIdentity(&track.OrientationCurve) ||
                !GrannyCurveIsConstantOrIdentity(&track.ScaleShearCurve)) return false;
            ++tracks;
        }
    }
    return tracks != 0;
}
