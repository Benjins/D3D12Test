

#pragma once

#include "fuzz_basic.h"

#include "basics.h"

#include <d3d11.h>

#include <mutex>

struct DebugExampleVideoData
{
	std::vector<byte> PictureParams;
	std::vector<byte> QMatrix;
	std::vector<byte> SliceControl;
	std::vector<byte> Bitstream;
};

struct D3D11VideoFuzzingPersistentState
{
	ID3D11Device* D3DDevice = nullptr;
	ID3D11DeviceContext* D3DContext = nullptr;
	ID3D11VideoDevice* VideoDevice = nullptr;
	ID3D11VideoContext* VideoContext = nullptr;

	ID3D11VideoProcessorEnumerator* ProcessorEnumerator = nullptr;
	//ID3D11VideoProcessor* VideoProcessor = nullptr;

	ID3D11Texture2D* OutputTexture = nullptr;
	std::vector<ID3D11VideoDecoderOutputView*> DecoderOutputViews;
	ID3D11VideoDecoder* VideoDecoder = nullptr;

	ID3D11Texture2D* FinalOutputTexture = nullptr;
	ID3D11Texture2D* FinalOutputTextureCopy = nullptr;

	ID3D11VideoProcessorOutputView* FinalOutputView = nullptr;
	std::vector<ID3D11VideoProcessorInputView*> FinalInputViews;

	std::vector<DebugExampleVideoData> ExampleVidData;

	//std::mutex* DecodingMutex = nullptr;
};

struct D3D11VideoFuzzingState : FuzzBasicState {
	D3D11VideoFuzzingPersistentState* Persistent = nullptr;
};




void SetSeedOnVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, uint64 Seed);
void DoIterationsWithVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, int32 NumIterations);

void SetupVideoFuzzerPersistentState(D3D11VideoFuzzingPersistentState* Persistent, IDXGIAdapter* ChosenAdapter);

