#include "raylib.h"
#include "raymath.h"
#include "camera.hpp"
#include <math.h>

RPGCamera::RPGCamera() {
    cam.position   = Vector3{0.0f, 5.0f, 10.0f};
    cam.target     = Vector3{0.0f, 0.0f, 0.0f};
    cam.up         = Vector3{0.0f, 1.0f, 0.0f};
    cam.fovy       = 60.0f;
    cam.projection = CAMERA_PERSPECTIVE;
}

void RPGCamera::update(float dt) {
    float wheel = GetMouseWheelMove();

    // --- Smooth zoom (follow mode only) ---
    if (followTarget != nullptr && wheel != 0.0f) {
        targetDistance -= wheel * 1.5f;
        if (targetDistance < 2.0f)  targetDistance = 2.0f;
        if (targetDistance > 50.0f) targetDistance = 50.0f;
    }
    distance += (targetDistance - distance) * 8.0f * dt;

    // --- Mouse look ---
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        yaw   += GetMouseDelta().x * 0.003f;
        pitch += GetMouseDelta().y * 0.003f;
        if (pitch >  1.4f) pitch =  1.4f;
        if (pitch < -1.4f) pitch = -1.4f;
    }

    // --- Follow mode ---
    if (followTarget != nullptr) {
        Vector3 target = *followTarget;
        Vector3 offset = {
            -sinf(yaw) * cosf(pitch) * distance,
            -sinf(pitch) * distance,
            -cosf(yaw) * cosf(pitch) * distance
        };
        cam.target   = target;
        cam.position = Vector3Add(target, offset);
        return;
    }

    // --- Free movement ---
    float speed = moveSpeed * (IsKeyDown(KEY_LEFT_SHIFT) ? runMultiplier : 1.0f);

    Vector3 forward = {sinf(yaw), 0.0f, cosf(yaw)};
    Vector3 right   = {cosf(yaw), 0.0f, -sinf(yaw)};
    Vector3 look    = {sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)};

    if (wheel != 0.0f)
        forwardVelocity += wheel * 12.0f;
    forwardVelocity *= powf(0.01f, dt);
    cam.position = Vector3Add(cam.position, Vector3Scale(look, forwardVelocity * dt));

    // Orbit around Y=0 intersection on middle-mouse drag
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        if (look.y < -0.001f) {
            float t = -cam.position.y / look.y;
            orbitPivot = Vector3Add(cam.position, Vector3Scale(look, t));
        } else {
            orbitPivot = Vector3{cam.position.x, 0.0f, cam.position.z};
        }
        orbitDist = Vector3Distance(cam.position, orbitPivot);
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        const float minH = 0.5f;
        float maxPitch = asinf(fmaxf(-minH / fmaxf(orbitDist, 0.001f), -1.0f));
        if (pitch > maxPitch) pitch = maxPitch;
        look = Vector3{sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)};
        cam.position = Vector3Subtract(orbitPivot, Vector3Scale(look, orbitDist));
    }

    if (IsKeyDown(KEY_W))
        cam.position = Vector3Add(cam.position, Vector3Scale(forward, speed * dt));
    if (IsKeyDown(KEY_S))
        cam.position = Vector3Subtract(cam.position, Vector3Scale(forward, speed * dt));
    if (IsKeyDown(KEY_D))
        cam.position = Vector3Subtract(cam.position, Vector3Scale(right, speed * dt));
    if (IsKeyDown(KEY_A))
        cam.position = Vector3Add(cam.position, Vector3Scale(right, speed * dt));

    if (cam.position.y < 0.5f)
        cam.position.y = 0.5f;

    cam.target = Vector3Add(cam.position,
        Vector3{sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)});
}
