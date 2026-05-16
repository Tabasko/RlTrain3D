#pragma once
#include "../types.h"
#include "../ecs/isystem.h"

class SignalSystem : public ISystem {
  public:
      void Init()    override; // LoadModel("assets/KS-Sig.glb")
      void Update()  override; // placement input + collision checks
      void Draw3D()  override; // iterate gs.ecs.signals, DrawModel at each pos
      void Draw2D()  override {};
      void Destroy() override; // UnloadModel
  private:
      Model s_model;
      bool  s_model_ok = false;
  };

extern SignalSystem signal_system;