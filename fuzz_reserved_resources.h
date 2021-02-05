
#pragma once

#include "fuzz_basic.h"

#include "basics.h"

#include <d3d12.h>

struct D3DReservedResourceFuzzingPersistentState {
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;

	// We have to start signaling with 1, since the initial value of the fence is 0
	int32 ExecFenceToSignal = 1;

	struct HeapAndResourceToDelete {
		ID3D12Resource* Resource = nullptr;
		ID3D12Heap* Heap = nullptr;
		uint64 FenceValueToWaitOn = 0;
	};

	std::vector<HeapAndResourceToDelete> CleanupTodo;

	void DeferCleanupResourceandHeap(ID3D12Resource* Resource, ID3D12Heap* Heap, uint64 FenceValueToWaitOn)
	{
		HeapAndResourceToDelete Todo;
		Todo.Resource = Resource;
		Todo.Heap = Heap;
		Todo.FenceValueToWaitOn = FenceValueToWaitOn;
		CleanupTodo.push_back(Todo);
	}

	void CheckCleanup(uint64 LastCompletedFence)
	{
		for (int32 i = 0; i < CleanupTodo.size(); i++)
		{
			if (CleanupTodo[i].FenceValueToWaitOn <= LastCompletedFence)
			{
				CleanupTodo[i].Resource->Release();
				CleanupTodo[i].Heap->Release();

				CleanupTodo[i] = CleanupTodo.back();
				CleanupTodo.pop_back();
				i--;
			}
		}
	}
};

struct ReservedResourceFuzzingState : FuzzBasicState {
	ID3D12Device* D3DDevice = nullptr;
	D3DReservedResourceFuzzingPersistentState* Persistent = nullptr;
};


void SetupPersistentOnReservedResourceFuzzer(D3DReservedResourceFuzzingPersistentState* Persist, ID3D12Device* Device);

void SetSeedOnReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, uint64_t Seed);
void DoIterationsWithReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, int32_t NumIterations);

