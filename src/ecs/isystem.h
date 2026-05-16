#pragma once
#include <stdio.h>

// Lifecycle interface every simulation system must implement.
// main.cpp drives all systems through this interface; concrete types are not
// referenced there.
struct ISystem {
    virtual void Init()    = 0;  // load GPU resources; called once after InitWindow
    virtual void Update()  = 0;  // advance simulation state; called once per frame
    virtual void Draw3D()  = 0;  // draw world geometry; called inside BeginMode3D
    virtual void Draw2D()  {}    // draw screen-space overlays; default is a no-op
    virtual void Destroy() = 0;  // release GPU resources; called before CloseWindow
    virtual ~ISystem()     = default;
};
