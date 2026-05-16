#pragma once
#include "../ecs/isystem.h"

// Junction placement, rendering, and switch toggling.
class JunctionSystem : public ISystem {
public:
    void Init()    override;
    void Update()  override;
    void Draw3D()  override;
    void Draw2D()  override;
    void Destroy() override;
};

extern JunctionSystem junction_system;
