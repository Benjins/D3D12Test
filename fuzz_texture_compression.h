
#pragma once

#include "fuzz_basic.h"

#include "basics.h"

#include <d3d12.h>

struct D3DTextureCompressionFuzzingPersistentState {
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;

	// We have to start signaling with 1, since the initial value of the fence is 0
	int32 ExecFenceToSignal = 1;

	//ID3D12PipelineState* BlitPSO = nullptr;
};

struct TextureCompressionFuzzingState : FuzzBasicState {
	ID3D12Device* D3DDevice = nullptr;
	D3DTextureCompressionFuzzingPersistentState* Persistent = nullptr;
};


void SetupPersistentOnTextureCompressionFuzzer(D3DTextureCompressionFuzzingPersistentState* Persist, ID3D12Device* Device);

void SetSeedOnTextureCompressionFuzzer(TextureCompressionFuzzingState* Fuzzer, uint64 Seed);
void DoIterationsWithTextureCompressionFuzzer(TextureCompressionFuzzingState* Fuzzer, int32 NumIterations);

