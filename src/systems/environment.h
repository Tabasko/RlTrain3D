#pragma once
#include "../ecs/isystem.h"

#define MAP_SIZE 1024

// Terrain, ground plane, and ambient environment rendering.
class EnvironmentSystem : public ISystem {
public:
    void Init()    override;
    void Update()  override;
    void Draw3D()  override;
    void Destroy() override;
};

extern EnvironmentSystem environment_system;
