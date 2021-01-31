

#pragma once

#include "fuzz_basic.h"

#include "basics.h"

#include <d3d11.h>

struct D3D11VideoFuzzingState : FuzzBasicState {
	ID3D11Device* D3DDevice = nullptr;
	ID3D11VideoDevice* VideoDevice = nullptr;
	ID3D11VideoContext* VideoContext = nullptr;
};




void SetSeedOnVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, uint64 Seed);
void DoIterationsWithVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, int32 NumIterations);


