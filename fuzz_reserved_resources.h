
#pragma once

#include "fuzz_basic.h"

#include "basics.h"

#include <d3d12.h>

struct D3DReservedResourceFuzzingPersistentState
{
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;

	// We have to start signaling with 1, since the initial value of the fence is 0
	int32 ExecFenceToSignal = 1;
};

struct ReservedResourceFuzzingState : FuzzBasicState {
	ID3D12Device* D3DDevice = nullptr;
	D3DReservedResourceFuzzingPersistentState* Persistent = nullptr;
};


void SetupPersistentOnReservedResourceFuzzer(D3DReservedResourceFuzzingPersistentState* Persist, ID3D12Device* Device);

void SetSeedOnReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, uint64_t Seed);
void DoIterationsWithReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, int32_t NumIterations);

