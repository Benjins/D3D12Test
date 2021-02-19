
#pragma once

#include "basics.h"

#include "fuzz_basic.h"

#include <mutex>

#include "d3d_resource_mgr.h"

struct ID3D12Device;

struct D3DDrawingFuzzingPersistentState
{
	ResourceLifecycleManager ResourceMgr;
	CommandListReclaimer CmdListMgr;
	ID3D12CommandQueue* CmdQueue = nullptr;
	ID3D12Fence* ExecFence = nullptr;

	ID3D12Resource* RTReadback = nullptr;

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

	// If true, the pixel shader will always output 1 for its alpha. Can make readbacks easier to visualise,
	// at the cost of some blending stressing and other code paths
	byte ForcePixelOutputAlphaToOne = 0;

	// If true, we won't use a random blend state, and will use the default non-blended one
	byte DisableBlendingState = 0;

	// Can trip a bug in WARP (see repro case 1)
	byte AllowConservativeRasterization = 0;

	// CBVs less likely to contain garbage, have actual floats rather than random bytes
	byte CBVUploadRandomFloatData = 1;

	// If there are data races in command list execution, this can avoid them while still allowing some threading
	byte LockMutexAroundExecCmdList = 0;

	// If true, we copy the rendered images to a readback texture and spit them out to a file
	byte ShouldReadbackImage = 0;

	// If true, each case will clear the render target before rendering. Technically a modicum slower,
	// but probably good overall since it makes readbacks mroe meaningful and exercises a bit more code
	byte ShouldClearRTVBeforeCase = 1;

	// We spit these out to a folder render_output, with a filename "{Prepend}{InitialFuzzSeed}{Append}.png"
	const char* ReadbackImageNamePrepend = "image_";
	const char* ReadbackImageNameAppend = "";

	// The dimensions of the render target that we use
	int32 RTWidth = 512;
	int32 RTHeight = 512;

	// 0 = do not delete resources (though they will be re-used once safe)
	// 1 = delete all resources once used, do not re-use them
	// anywhere in b/w 0 and 1 is the chance that a living resource will be destroyed at each iteration
	// By default (0.1), 10% of resources will be destroyed each iteration, the others have a chance to be re-used
	float ResourceDeletionChance = 0.1f;

	// Same as above, but for heaps instead of resources
	float HeapDeletionChance = 0.1f;

	// The chance that a given resource (right now only immutable textures) will be a placed resource instead of a committed one
	float PlacedResourceChance = 0.3f;

};

struct ShaderFuzzingState : FuzzBasicState {
	ID3D12Device* D3DDevice = nullptr;

	D3DDrawingFuzzingPersistentState* D3DPersist = nullptr;

	ShaderFuzzConfig* Config = nullptr;
};


void SetupFuzzPersistState(D3DDrawingFuzzingPersistentState* Persist, ShaderFuzzConfig* Config, ID3D12Device* Device);

void SetSeedOnFuzzer(ShaderFuzzingState* Fuzzer, uint64_t Seed);
void DoIterationsWithFuzzer(ShaderFuzzingState* Fuzzer, int32_t NumIterations);

