
#pragma once

#include "basics.h"

#include <random>

#include "d3d_resource_mgr.h"

struct ID3D12Device;

struct D3DDrawingFuzzingPersistentState
{
	ResourceLifecycleManager ResourceMgr;
	CommandListReclaimer CmdListMgr;
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;

	// We have to start signaling with 1, since the initial value of the fence is 0
	int32 ExecFenceToSignal = 1;
};

struct ShaderFuzzConfig
{
	uint32 EnsureBetterPixelCoverage = 0;

	// Can trip a bug in WARP (see repro case 1)
	uint32 AllowConservativeRasterization = 0;
};

struct ShaderFuzzingState {
	ID3D12Device* D3DDevice = nullptr;
	std::mt19937_64 RNGState;

	uint64 InitialFuzzSeed = 0;

	D3DDrawingFuzzingPersistentState* D3DPersist = nullptr;

	ShaderFuzzConfig* Config = nullptr;

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

