// Field of view settings: stubbed for State of Decay. DAORecomp's version hooked
// Dragon Age: Origins' camera update and SetFov at DAO's addresses; SoD's camera
// code has not been located yet, so these do nothing and report the defaults.
#include "stdafx.h"
#include "gpu/camera_fov.h"

namespace sod::gpu
{
    void SetCameraFov(int, int)
    {
    }

    double GameplayFovDegrees()
    {
        return 60.0;
    }

    double VerticalFov(double degrees)
    {
        return degrees;
    }

    void SetPhotoFov(double)
    {
    }
}
