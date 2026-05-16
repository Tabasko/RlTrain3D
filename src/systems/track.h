#pragma once
#include "../types.h"
#include "../ecs/isystem.h"
#include <stdio.h>

// Tile-based track placement, GPU instancing, and routing graph.
class TrackSystem : public ISystem {
public:
    void Init()    override;
    void Update()  override;
    void Draw3D()  override;
    void Draw2D()  override;
    void Destroy() override;
    void Save(FILE *f);
    void Load(FILE *f);
};

extern TrackSystem track_system;
