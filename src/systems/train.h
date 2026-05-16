#pragma once
#include "../ecs/isystem.h"
#include <stdio.h>

// Train placement, physics, pathfinding, and rendering.
class TrainSystem : public ISystem {
public:
    void Init()    override;
    void Update()  override;
    void Draw3D()  override;
    void Destroy() override;
    void Save(FILE *f);
    void Load(FILE *f);
};

extern TrainSystem train_system;
