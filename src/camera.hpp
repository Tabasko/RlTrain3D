#pragma once
#include "raylib.h"

struct RPGCamera {
    Camera3D  cam            = {};
    Vector3  *followTarget   = nullptr;
    float     distance       = 10.0f;
    float     targetDistance = 10.0f;
    float     yaw            = 3.14159265f;
    float     pitch          = -0.3f;
    float     moveSpeed      = 10.0f;
    float     runMultiplier  = 3.0f;
    float     forwardVelocity = 0.0f;
    Vector3   orbitPivot     = {};
    float     orbitDist      = 0.0f;

    RPGCamera();
    void update(float dt);
};
