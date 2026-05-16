#pragma once
#include "../ecs/isystem.h"
#include "../types.h"
#include "raylib.h"
#include <stdio.h>

// One placed instance of any prop.
typedef struct {
    PropType type;
    Vector3  position;
    float    rotation;       // current rotor angle in radians
    float    rotation_speed; // radians per second (randomised per instance)
} PropInstance;

// Prop placement, animation, and rendering.
class PropsSystem : public ISystem {
public:
    void Init()    override;
    void Update()  override;
    void Draw3D()  override;
    void Destroy() override;
    void Save(FILE *f);
    void Load(FILE *f);
};

extern PropsSystem props_system;
