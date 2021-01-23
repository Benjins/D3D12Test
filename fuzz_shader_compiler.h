
#pragma once

#include "basics.h"

#include <random>
#include <mutex>

#include "d3d_resource_mgr.h"

struct ID3D12Device;

struct D3DDrawingFuzzingPersistentState
{
	ResourceLifecycleManager ResourceMgr;
	CommandListReclaimer CmdListMgr;
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;

	// Debugging tool if we find data races, so we can avoid them in the meantime
	// Must be shared across threads, so...yeah
	std::mutex* ExecuteCommandListMutex = nullptr;

	// We have to start signaling with 1, since the initial value of the fence is 0
	int32 ExecFenceToSignal = 1;
};

struct ShaderFuzzConfig
{
	// Make the vertex shader output more sensible position data
	byte EnsureBetterPixelCoverage = 0;

	// Can trip a bug in WARP (see repro case 1)
	byte AllowConservativeRasterization = 0;

	// CBVs less likely to contain garbage, have actual floats rather than random bytes
	byte CBVUploadRandomFloatData = 1;

	// If there are data races in command list execution, this can avoid them while still allowing some threading
	byte LockMutexAroundExecCmdList = 0;
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

