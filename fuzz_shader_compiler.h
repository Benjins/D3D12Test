
#pragma once

#include <stdint.h>

#include <random>

#include "d3d_resource_mgr.h"

struct ID3D12Device;

struct D3DDrawingFuzzingPersistentState
{
	ResourceLifecycleManager ResourceMgr;
	CommandListReclaimer CmdListMgr;
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;
	int32 ExecFenceToSignal = 0;
};

struct ShaderFuzzingState {
	ID3D12Device* D3DDevice = nullptr;
	std::mt19937_64 RNGState;

	D3DDrawingFuzzingPersistentState* D3DPersist = nullptr;

	// NOTE: It's inclusive
	int GetIntInRange(int min, int max)
	{
		std::uniform_int_distribution<int> Dist(min, max);

		return Dist(RNGState);
	}

	float GetFloatInRange(float min, float max)
	{
		std::uniform_real_distribution<float> Dist(min, max);

		return Dist(RNGState);
	}

	float GetFloat01()
	{
		return GetFloatInRange(0.0f, 1.0f);
	}
};


void SetupFuzzPersistState(D3DDrawingFuzzingPersistentState* Persist, ID3D12Device* Device);

void SetSeedOnFuzzer(ShaderFuzzingState* Fuzzer, uint64_t Seed);
void DoIterationsWithFuzzer(ShaderFuzzingState* Fuzzer, int32_t NumIterations);

