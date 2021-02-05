
#include <stdio.h>

#include <stdint.h>

#include <vector>
#include <thread>
#include <mutex>

#include <Windows.h>

#include <assert.h>

#include <d3d11.h>
#include <d3d12.h>

#include <d3dcompiler.h>

//#include "d3dx12.h"

#include "basics.h"

#include <dxgi1_2.h>

#include "d3d12_ext.h"

#include "fuzz_d3d11_video.h"
#include "fuzz_reserved_resources.h"
#include "fuzz_shader_compiler.h"
#include "fuzz_dxbc.h"
#include "d3d_resource_mgr.h"

#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "d3dcompiler.lib")


LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

#define NUM_BACKBUFFERS 3

static_assert(NUM_BACKBUFFERS < sizeof(uint32) * 8, "Uhhh.....why?");

struct D3D12System {
	HWND Window = nullptr;
	ID3D12Device* Device = nullptr;
	ID3D12CommandQueue* DirectCommandQueue = nullptr;
	IDXGISwapChain1* Swapchain = nullptr;
	ID3D12RootSignature* RootSignature = nullptr;

	CommandListReclaimer CmdListReclaimer;

	ResourceLifecycleManager ResMgr;

	ID3D12Resource* BackbufferResources[NUM_BACKBUFFERS] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[NUM_BACKBUFFERS] = {};
	uint32 RTVHandlesCreated = 0;

	ID3D12DescriptorHeap* TextureSRVHeap = nullptr;

	ID3D12Fence* FrameFence = nullptr;
	uint64 FrameFenceValue = 0;
};

void InitRendering(D3D12System* System);

void DoRendering(D3D12System* System);

// Code from https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/UWP/D3D12HelloWorld/src/HelloTriangle/shaders.hlsl
// Copyright (c) Microsoft. All rights reserved. MIT Licensed.
const char* VertexShaderCode =
"struct VSInput\n"
"{\n"
"	float4 position : POSITION;\n"
"	float4 color : COLOR;\n"
"};\n"

"struct PSInput\n"
"{\n"
"	float4 position : SV_POSITION;\n"
"	float4 color : COLOR;\n"
"};\n"
"\n"
"cbuffer MyBuffer1 : register(b0)\n"
"{\n"
"	float4 constant1;\n"
"};\n"
"\n"
"PSInput MainVS(VSInput input)\n"
"{\n"
"	PSInput result;\n"
"\n"
"	result.position = input.position;\n"
"	result.color = input.color;\n"
"\n"
"	return result;\n"
"}\n";


// Texture2D MyTexture : register(t0);
// SamplerState MySampler : register(s0);

const char* PixelShaderCode =
"struct PSInput\n"
"{\n"
"	float4 position : SV_POSITION;\n"
"	float4 color : COLOR;\n"
"};\n"
"cbuffer MyBuffer1 : register(b0)\n"
"{\n"
"	float4 constant1;\n"
"};\n"
"\n"
"Texture2D MyTexture : register(t0);\n"
"SamplerState MySampler : register(s0);\n"
"\n"
"cbuffer MyOtherBuffer1 : register(b1)\n"
"{\n"
"	float4 otherConstant1;\n"
"	float4 otherConstant2;\n"
"};\n"
"\n"
"float4 MainPS(PSInput input) : SV_TARGET\n"
"{\n"
//"	return input.color * otherConstant1 * otherConstant2 + constant1;\n"
"	float4 Col = float4(1.0, 0.1, 0.1, 0.0);\n"
"	Col = MyTexture.SampleLevel(MySampler, input.color.gb + float2(otherConstant1.x + constant1.x, otherConstant2.y), 0);\n"
"	return Col;\n"
"}\n";

D3D12_SHADER_BYTECODE VertexShaderByteCode;
D3D12_SHADER_BYTECODE PixelShaderByteCode;

#include "shader_meta.h"

ShaderMetadata VertexShaderMeta;
ShaderMetadata PixelShaderMeta;



static bool shouldQuit = false;

int WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCommand) {

	ID3D12Debug1* D3D12DebugLayer = nullptr;
	D3D12GetDebugInterface(IID_PPV_ARGS(&D3D12DebugLayer));
	D3D12DebugLayer->EnableDebugLayer();
	//D3D12DebugLayer->SetEnableGPUBasedValidation(true);
	//D3D12DebugLayer->SetEnableSynchronizedCommandQueueValidation(true);

	IDXGIFactory2* DXGIFactory = nullptr;

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&DXGIFactory));
	ASSERT(SUCCEEDED(hr));

	int ChosenAdapterIndex = 2;
	IDXGIAdapter* ChosenAdapter = nullptr;

	{
		IDXGIAdapter* Adapter = nullptr;
		for (int AdapterIndex = 0; true; AdapterIndex++) {
			hr = DXGIFactory->EnumAdapters(AdapterIndex, &Adapter);
			if (!SUCCEEDED(hr)) {
				break;
			}

			if (AdapterIndex == ChosenAdapterIndex) {
				ChosenAdapter = Adapter;
			}

			DXGI_ADAPTER_DESC AdapterDesc = {};
			Adapter->GetDesc(&AdapterDesc);

			OutputDebugStringW(L"\nAdapter: ");
			OutputDebugStringW(AdapterDesc.Description);
			char OtherStuff[1024] = {};
			snprintf(OtherStuff, sizeof(OtherStuff), "\nvendor = %X device = %X\nDedicated vid mem: %lld  Dedicated system mem: %lld  Shared system mem: %lld\n",
				AdapterDesc.VendorId, AdapterDesc.DeviceId,
				AdapterDesc.DedicatedVideoMemory, AdapterDesc.DedicatedSystemMemory, AdapterDesc.SharedSystemMemory);
			OutputDebugStringA(OtherStuff);
		}
	}

	ASSERT(ChosenAdapter != nullptr);

	{
		DXGI_ADAPTER_DESC ChosenAdapterDesc = {};
		ChosenAdapter->GetDesc(&ChosenAdapterDesc);

		OutputDebugStringW(L"\nChosen Adapter: ");
		OutputDebugStringW(ChosenAdapterDesc.Description);
		OutputDebugStringW(L"\n");
	}


	if (0)
	{
		ID3D11Device* Device = nullptr;
		ID3D11DeviceContext* DeviceContext = nullptr;
		UINT Flags = 0;
		Flags |= D3D11_CREATE_DEVICE_DEBUG;
		HRESULT hr = D3D11CreateDevice(ChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &DeviceContext);

		ASSERT(SUCCEEDED(hr));

		ID3D11VideoDevice* VideoDevice = nullptr;
		hr = Device->QueryInterface(IID_PPV_ARGS(&VideoDevice));
		ASSERT(SUCCEEDED(hr));

		ID3D11VideoContext* VideoContext = nullptr;
		hr = DeviceContext->QueryInterface(IID_PPV_ARGS(&VideoContext));
		ASSERT(SUCCEEDED(hr));

		D3D11VideoFuzzingState Fuzzer;
		Fuzzer.D3DDevice = Device;
		Fuzzer.VideoDevice = VideoDevice;
		Fuzzer.VideoContext = VideoContext;

		SetSeedOnVideoFuzzer(&Fuzzer, 0);
		DoIterationsWithVideoFuzzer(&Fuzzer, 1);

		return 0;
	}


	ID3D12Device* Device = nullptr;
	ASSERT(SUCCEEDED(D3D12CreateDevice(ChosenAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device))));


	//if (0)
	{
		D3DReservedResourceFuzzingPersistentState Persistent;
		SetupPersistentOnReservedResourceFuzzer(&Persistent, Device);

		ReservedResourceFuzzingState Fuzzer;
		Fuzzer.Persistent = &Persistent;
		Fuzzer.D3DDevice = Device;

		SetSeedOnReservedResourceFuzzer(&Fuzzer, 0);
		DoIterationsWithReservedResourceFuzzer(&Fuzzer, 1);

		return 0;
	}


	if (0)
	{
		// Resources I guess?

		//ResourceLifecycleManager ResourceMgr;
		//ResourceMgr.D3DDevice = Device;


		return 0;
	}

	if (0)
	{
		const char* ExampleShaderFilename = "../example_shaders/9795564935892538031_pixel.dxbc";

		void* FileData = nullptr;
		int32 FileSize = 0;
		ReadDataFromFile(ExampleShaderFilename, &FileData, &FileSize);

		ParseDXBCCode((byte*)FileData, FileSize);

		return 0;
	}

	//if (0)
	{
		bool bIsSingleThreaded = false;

		ShaderFuzzConfig ShaderConfig;
		ShaderConfig.EnsureBetterPixelCoverage = 1;
		ShaderConfig.CBVUploadRandomFloatData = 0;
		ShaderConfig.ResourceDeletionChance = 0.9f;
		{
			DXGI_ADAPTER_DESC Desc = {};
			ChosenAdapter->GetDesc(&Desc);
			ShaderConfig.AllowConservativeRasterization = (Desc.VendorId != 0x1414 || Desc.DeviceId != 0x8C);
			ShaderConfig.LockMutexAroundExecCmdList = (Desc.VendorId == 0x1414 && Desc.DeviceId == 0x8C && !bIsSingleThreaded);
		}

		if (bIsSingleThreaded)
		{
			LARGE_INTEGER PerfFreq;
			QueryPerformanceFrequency(&PerfFreq);
			
			LARGE_INTEGER PerfStart;
			QueryPerformanceCounter(&PerfStart);
			
			const int32 TestCases = 1000;

			D3DDrawingFuzzingPersistentState PersistState;
			PersistState.ResourceMgr.D3DDevice = Device;
			SetupFuzzPersistState(&PersistState, Device);
			
			uint64 DebugTestCases[] = {
				// Intel
				14908923361117291,
				14908923367228386,
				14908923369325538
			};


			// Single threaded
			//for (int32 i = 0; i < ARRAY_COUNTOF(DebugTestCases); i++)
			for (int32 i = 0; i < TestCases; i++)
			{
				ShaderFuzzingState Fuzzer;
				Fuzzer.D3DDevice = Device;
				Fuzzer.D3DPersist = &PersistState;
				Fuzzer.Config = &ShaderConfig;
			
				//LOG("Doing round %d of fuzzing (%llu)...", i, DebugTestCases[i]);
				//SetSeedOnFuzzer(&Fuzzer, DebugTestCases[i]);
				LOG("Doing round %d of fuzzing...", i);
				SetSeedOnFuzzer(&Fuzzer, i);
				DoIterationsWithFuzzer(&Fuzzer, 1);
			}
			
			LARGE_INTEGER PerfEnd;
			QueryPerformanceCounter(&PerfEnd);
			
			double ElapsedTimeSeconds = (PerfEnd.QuadPart - PerfStart.QuadPart);
			ElapsedTimeSeconds = ElapsedTimeSeconds / PerfFreq.QuadPart;
			LOG("Ran %d test cases in %3.2f seconds, or %3.2f ms/case", TestCases, ElapsedTimeSeconds, (ElapsedTimeSeconds / TestCases) * 1000.0f);
		}
		else
		{
			const int32 ThreadCount = 6;
			std::vector<std::thread> FuzzThreads;

			std::mutex DebugMutex;

			uint64 StartingTime = time(NULL);
			LOG("Starting time: %llu", StartingTime);

			for (int32 ThreadIdx = 0; ThreadIdx < ThreadCount; ThreadIdx++)
			{
				FuzzThreads.emplace_back([Device = Device, TIdx = ThreadIdx, ConfigPtr = &ShaderConfig, StartingTime = StartingTime, MutexPtr = &DebugMutex]() {
					D3DDrawingFuzzingPersistentState PersistState;
					PersistState.ResourceMgr.D3DDevice = Device;
					PersistState.ExecuteCommandListMutex = MutexPtr;
					SetupFuzzPersistState(&PersistState, Device);

					for (int32 i = 0; i < 128 * 1000; i++)
					{
						ShaderFuzzingState Fuzzer;
						Fuzzer.D3DDevice = Device;
						Fuzzer.D3DPersist = &PersistState;
						Fuzzer.Config = ConfigPtr;
		
						uint64 InitialFuzzSeed = 0;
		
						// If we want to have different fuzzing each process run. Good once a fuzzer is established.
						InitialFuzzSeed += StartingTime * 0x8D3F77LLU;
		
						InitialFuzzSeed += (TIdx * 1024LLU * 1024LLU);
						InitialFuzzSeed += i;
		
						// In theory can cause contention maybe or slow things down? Idk, can remove this
						LOG("Fuzing with seed %llu", InitialFuzzSeed);
					
						SetSeedOnFuzzer(&Fuzzer, InitialFuzzSeed);
						DoIterationsWithFuzzer(&Fuzzer, 1);
		
						// Helpful if we want some output to know that it's going but don't want spam
						//if (i % 100 == 0)
						//{
						//	LOG("Thread %d run %d finished", TIdx, i);
						//}
					}
				});
			}
		
			for (auto& Thread : FuzzThreads)
			{
				Thread.join();
			}
		}
	
		return 0;
	}

	return 0;
}

