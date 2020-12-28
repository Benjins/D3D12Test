
#include <stdio.h>

#include <stdint.h>

#include <vector>
#include <thread>

#include <Windows.h>

#include <assert.h>

#include <d3d12.h>

#include <d3dcompiler.h>

//#include "d3dx12.h"

#include "basics.h"

#include <dxgi1_2.h>

#include "d3d12_ext.h"

#include "fuzz_shader_compiler.h"

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "d3dcompiler.lib")


LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

struct CommandListReclaimer
{
	struct CommandListToReclaim
	{
		ID3D12GraphicsCommandList* CmdList = nullptr;
		uint64 FenceValueToWaitOn = 0;
	};

	struct CommandAllocatorToReclaim
	{
		ID3D12CommandAllocator* CmdAllocator = nullptr;
		uint64 FenceValueToWaitOn = 0;
	};

	std::vector<CommandListToReclaim> CmdListPendingNextFence;
	std::vector<CommandListToReclaim> CmdListWaitingForFence;
	std::vector<CommandListToReclaim> CmdListNowAvailable;

	std::vector<CommandAllocatorToReclaim> CmdAllocPendingNextFence;
	std::vector<CommandAllocatorToReclaim> CmdAllocWaitingForFence;
	std::vector<CommandAllocatorToReclaim> CmdAllocNowAvailable;

	void NowDoneWithCommandList(ID3D12GraphicsCommandList* CmdList)
	{
		// TODO: Oh no
		//CmdList->AddRef();
		CmdListPendingNextFence.push_back({ CmdList, 0 });
	}

	void NowDoneWithCommandAllocator(ID3D12CommandAllocator* CmdList)
	{
		// TODO: Oh no
		//CmdList->AddRef();
		CmdAllocPendingNextFence.push_back({ CmdList, 0 });
	}

	void OnFrameFenceSignaled(uint64 SignaledValue)
	{
		for (auto Pending : CmdListPendingNextFence)
		{
			CmdListWaitingForFence.push_back({ Pending.CmdList, SignaledValue });
		}

		CmdListPendingNextFence.clear();

		for (auto Pending : CmdAllocPendingNextFence)
		{
			CmdAllocWaitingForFence.push_back({ Pending.CmdAllocator, SignaledValue });
		}

		CmdAllocPendingNextFence.clear();
	}

	void CheckIfFenceFinished(ID3D12Fence* FrameFence)
	{
		uint64 CompletedValue = FrameFence->GetCompletedValue();

		for (int32 i = 0; i < CmdListWaitingForFence.size(); i++)
		{
			if (CmdListWaitingForFence[i].FenceValueToWaitOn <= CompletedValue)
			{
				CmdListNowAvailable.push_back(CmdListWaitingForFence[i]);
				CmdListWaitingForFence[i] = CmdListWaitingForFence.back();
				CmdListWaitingForFence.pop_back();
				i--;
			}
		}

		for (int32 i = 0; i < CmdAllocWaitingForFence.size(); i++)
		{
			if (CmdAllocWaitingForFence[i].FenceValueToWaitOn <= CompletedValue)
			{
				CmdAllocNowAvailable.push_back(CmdAllocWaitingForFence[i]);
				CmdAllocWaitingForFence[i] = CmdAllocWaitingForFence.back();
				CmdAllocWaitingForFence.pop_back();
				i--;
			}
		}
	}

	//ID3D12CommandAllocator* CommandAllocator = nullptr;
	//ASSERT(SUCCEEDED(hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator))));

	ID3D12CommandAllocator* GetOpenCommandAllocator(struct D3D12System* System);
	ID3D12GraphicsCommandList* GetOpenCommandList(struct D3D12System* System, ID3D12CommandAllocator* CommandAllocator);
};


#define NUM_BACKBUFFERS 3

static_assert(NUM_BACKBUFFERS < sizeof(uint32) * 8, "Uhhh.....why?");

struct D3D12System {
	HWND Window = nullptr;
	ID3D12Device* Device = nullptr;
	ID3D12CommandQueue* DirectCommandQueue = nullptr;
	IDXGISwapChain1* Swapchain = nullptr;
	ID3D12RootSignature* RootSignature = nullptr;

	CommandListReclaimer CmdListReclaimer;

	ID3D12Resource* BackbufferResources[NUM_BACKBUFFERS] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[NUM_BACKBUFFERS] = {};
	uint32 RTVHandlesCreated = 0;

	ID3D12DescriptorHeap* TextureSRVHeap = nullptr;

	ID3D12Fence* FrameFence = nullptr;
	uint64 FrameFenceValue = 0;
};

ID3D12CommandAllocator* CommandListReclaimer::GetOpenCommandAllocator(struct D3D12System* System)
{
	if (CmdAllocNowAvailable.size() > 0)
	{
		auto* CmdAlloc = CmdAllocNowAvailable.back().CmdAllocator;
		CmdAllocNowAvailable.pop_back();

		CmdAlloc->Reset();
		return CmdAlloc;
	}
	else
	{
		return nullptr;
	}
}

ID3D12GraphicsCommandList* CommandListReclaimer::GetOpenCommandList(struct D3D12System* System, ID3D12CommandAllocator* CommandAllocator)
{
	if (CmdListNowAvailable.size() > 0)
	{
		auto* CmdList = CmdListNowAvailable.back().CmdList;
		CmdListNowAvailable.pop_back();

		CmdList->Reset(CommandAllocator, nullptr);
		return CmdList;
	}
	else
	{
		return nullptr;
	}
}

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

	int ChosenAdapterIndex = 0;
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

	ID3D12Device* Device = nullptr;
	ASSERT(SUCCEEDED(D3D12CreateDevice(ChosenAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device))));

	//if (0)
	{
		LARGE_INTEGER PerfFreq;
		QueryPerformanceFrequency(&PerfFreq);
		
		LARGE_INTEGER PerfStart;
		QueryPerformanceCounter(&PerfStart);
		
		const int32 TestCases = 1000;
		
		// Single threaded
		for (int32 i = 0; i < TestCases; i++)
		{
			ShaderFuzzingState Fuzzer;
			Fuzzer.D3DDevice = Device;
		
			SetSeedOnFuzzer(&Fuzzer, i);
			//LOG("Doing round %d of fuzzing...", i);
			DoIterationsWithFuzzer(&Fuzzer, 1);
		}
		
		LARGE_INTEGER PerfEnd;
		QueryPerformanceCounter(&PerfEnd);
		
		double ElapsedTimeSeconds = (PerfEnd.QuadPart - PerfStart.QuadPart);
		ElapsedTimeSeconds = ElapsedTimeSeconds / PerfFreq.QuadPart;
		LOG("Ran %d test cases in %3.2f seconds, or %3.2f ms/case", TestCases, ElapsedTimeSeconds, (ElapsedTimeSeconds / TestCases) * 1000.0f);

		// 4 on my machine uses like 70% of my CPU
		//const int32 ThreadCount = 1;
		
		//const int32 ThreadCount = 4;
		//std::vector<std::thread> FuzzThreads;
		//
		//for (int32 ThreadIdx = 0; ThreadIdx < ThreadCount; ThreadIdx++)
		//{
		//	FuzzThreads.emplace_back([Device = Device, TIdx = ThreadIdx]() {
		//		for (int32 i = 0; i < 128 * 1000; i++)
		//		{
		//			ShaderFuzzingState Fuzzer;
		//			Fuzzer.D3DDevice = Device;
		//
		//			uint64 InitialFuzzSeed = 0;
		//
		//			// If we want to have different fuzzing each process run. Good once a fuzzer is established.
		//			InitialFuzzSeed += time(NULL) * 0x8D3F77LLU;
		//
		//			InitialFuzzSeed += (TIdx * 1024LLU * 1024LLU);
		//			InitialFuzzSeed += i;
		//
		//			// In theory can cause contention maybe or slow things down? Idk, can remove this
		//			LOG("Fuzing with seed %llu", InitialFuzzSeed);
		//			
		//			SetSeedOnFuzzer(&Fuzzer, InitialFuzzSeed);
		//			DoIterationsWithFuzzer(&Fuzzer, 1);
		//
		//			// Helpful if we want some output to know that it's going but don't want spam
		//			//if (i % 100 == 0)
		//			//{
		//			//	LOG("Thread %d run %d finished", TIdx, i);
		//			//}
		//		}
		//	});
		//}
		//
		//for (auto& Thread : FuzzThreads)
		//{
		//	Thread.join();
		//}
	
		return 0;
	}

	WNDCLASSA WindowClass = {};
	WindowClass.lpfnWndProc = WindowProc;
	WindowClass.lpszClassName = "D3D12Test";
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClassA(&WindowClass);

	HWND window = CreateWindowA("D3D12Test", "D3D12Test Window", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, 0, 0, instance, 0);
	ShowWindow(window, SW_SHOW);

	OutputDebugStringA("Blllaaah Yooooo.\n");


	ID3D12CommandQueue* CommandQueue = nullptr;
	
	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ASSERT(SUCCEEDED(Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&CommandQueue))));

	DXGI_SWAP_CHAIN_DESC1 SwapchainDesc = {};
	SwapchainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	SwapchainDesc.SampleDesc.Count = 1;
	SwapchainDesc.BufferCount = NUM_BACKBUFFERS;
	SwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;

	IDXGISwapChain1* SwapChain = nullptr;
	ASSERT(SUCCEEDED(hr = DXGIFactory->CreateSwapChainForHwnd(CommandQueue, window, &SwapchainDesc, nullptr, nullptr, &SwapChain)));

	D3D12System D3DSystem;
	D3DSystem.Device = Device;
	D3DSystem.DirectCommandQueue = CommandQueue;
	D3DSystem.Swapchain = SwapChain;
	D3DSystem.Window = window;

	// Yeah they're globals...should have a way to pass down "assets" as well I guess, or maybe make the renderer do it idk
	auto* VertexShaderByteCodeBlob = CompileShaderCode(VertexShaderCode, D3DShaderType::Vertex, "vertex.shader", "MainVS", &VertexShaderMeta);
	VertexShaderByteCode.pShaderBytecode = VertexShaderByteCodeBlob->GetBufferPointer();
	VertexShaderByteCode.BytecodeLength = VertexShaderByteCodeBlob->GetBufferSize();

	auto* PixelShaderByteCodeBlob = CompileShaderCode(PixelShaderCode, D3DShaderType::Pixel, "pixel.shader", "MainPS", &PixelShaderMeta);
	PixelShaderByteCode.pShaderBytecode = PixelShaderByteCodeBlob->GetBufferPointer();
	PixelShaderByteCode.BytecodeLength = PixelShaderByteCodeBlob->GetBufferSize();

	D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
	RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	std::vector<D3D12_ROOT_PARAMETER> RootParams;
	std::vector<D3D12_STATIC_SAMPLER_DESC> RootStaticSamplers;

	// TODO
	std::vector<D3D12_DESCRIPTOR_RANGE> DescriptorRanges;

	{
		int32 NumSRVs = max(PixelShaderMeta.NumSRVs, VertexShaderMeta.NumSRVs);
		int32 NumCBVs = max(PixelShaderMeta.NumCBVs, VertexShaderMeta.NumCBVs);

		int32 TotalRootParams = NumSRVs + NumCBVs;
		RootParams.resize(TotalRootParams);

		DescriptorRanges.resize(TotalRootParams);

		int32 RootParamIdx = 0;
		for (int32 SRVIdx = 0; SRVIdx < NumSRVs; SRVIdx++)
		{
			RootParams[RootParamIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			RootParams[RootParamIdx].DescriptorTable.NumDescriptorRanges = 1;
			
			D3D12_DESCRIPTOR_RANGE& pDescriptorRange = DescriptorRanges[SRVIdx];
			pDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			pDescriptorRange.BaseShaderRegister = SRVIdx;
			pDescriptorRange.NumDescriptors = 1;
			pDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

			RootParams[RootParamIdx].DescriptorTable.pDescriptorRanges = &pDescriptorRange;
			RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			RootParamIdx++;
		}

		for (int32 CBVIdx = 0; CBVIdx < NumCBVs; CBVIdx++)
		{
			RootParams[RootParamIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			RootParams[RootParamIdx].Descriptor.RegisterSpace = 0;
			RootParams[RootParamIdx].Descriptor.ShaderRegister = CBVIdx;
			RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			RootParamIdx++;
		}

		int32 TotalStaticSamplers = max(VertexShaderMeta.NumStaticSamplers, PixelShaderMeta.NumStaticSamplers);
		RootStaticSamplers.resize(TotalStaticSamplers);

		for (int32 i = 0; i < TotalStaticSamplers; i++)
		{
			RootStaticSamplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			RootStaticSamplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			RootStaticSamplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			RootStaticSamplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			RootStaticSamplers[i].MipLODBias = 0;
			RootStaticSamplers[i].MaxAnisotropy = 0;
			RootStaticSamplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			RootStaticSamplers[i].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			RootStaticSamplers[i].MinLOD = 0.0f;
			RootStaticSamplers[i].MaxLOD = D3D12_FLOAT32_MAX;
			RootStaticSamplers[i].ShaderRegister = 0;
			RootStaticSamplers[i].RegisterSpace = 0;
			RootStaticSamplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}
	}

	RootSigDesc.NumParameters = RootParams.size();
	RootSigDesc.pParameters = RootParams.data();

	RootSigDesc.NumStaticSamplers = RootStaticSamplers.size();
	RootSigDesc.pStaticSamplers = RootStaticSamplers.data();

	ID3DBlob* RootSigBlob = nullptr;
	ID3DBlob* RootSigErrorBlob = nullptr;

	hr = D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootSigBlob, &RootSigErrorBlob);

	if (!SUCCEEDED(hr))
	{
		const char* ErrStr = (const char*)RootSigErrorBlob->GetBufferPointer();
		int32 ErrStrLen = RootSigErrorBlob->GetBufferSize();
		LOG("Root Sig Err: '%.*s'", ErrStrLen, ErrStr);
	}

	ASSERT(SUCCEEDED(hr));

	ID3D12RootSignature* RootSig = nullptr;
	Device->CreateRootSignature(0, RootSigBlob->GetBufferPointer(), RootSigBlob->GetBufferSize(), IID_PPV_ARGS(&RootSig));

	D3DSystem.RootSignature = RootSig;

	InitRendering(&D3DSystem);

	
	while (!shouldQuit) {
		MSG message;
		while (PeekMessage(&message, window, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessage(&message);
		}

		DoRendering(&D3DSystem);
	}



	return 0;
}

// TODO: Put these on system somehow
ID3D12PipelineState* PSO = nullptr;
ID3D12Resource* TriangleVertData = nullptr;

ID3D12Resource* PSCBuffer = nullptr;

ID3D12Resource* TrianglePixelTexture = nullptr;
ID3D12Resource* TrianglePixelTextureUpload = nullptr;

D3D12_RASTERIZER_DESC GetDefaultRasterizerDesc() {
	D3D12_RASTERIZER_DESC Desc = {};
	Desc.FillMode = D3D12_FILL_MODE_SOLID;
	Desc.CullMode = D3D12_CULL_MODE_BACK;
	Desc.FrontCounterClockwise = FALSE;
	Desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	Desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	Desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	Desc.DepthClipEnable = TRUE;
	Desc.MultisampleEnable = FALSE;
	Desc.AntialiasedLineEnable = FALSE;
	Desc.ForcedSampleCount = 0;
	Desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	return Desc;
}

D3D12_BLEND_DESC GetDefaultBlendStateDesc() {
	D3D12_BLEND_DESC Desc = {};
	Desc.AlphaToCoverageEnable = FALSE;
	Desc.IndependentBlendEnable = FALSE;

	const D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
	{
		FALSE,FALSE,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL,
	};

	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		Desc.RenderTarget[i] = DefaultRenderTargetBlendDesc;
	}

	return Desc;
}

int GFrameCounter = 0;

struct Vec3f {
	float x;
	float y;
	float z;
};

struct RGBAColour {
	float r;
	float g;
	float b;
	float a;
};

struct Vertex {
	Vec3f pos;
	RGBAColour colour;
};

const Vertex triangleVerticesData[] = {
	{ { 0.0f, 0.8f, 0.9f },{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ { 0.8f, -0.8f, 0.2f },{ 0.0f, 2.0f, 0.0f, 1.0f } },
	{ { -0.8f, -0.8f, 0.0f },{ 0.0f, 0.0f, 3.0f, 1.0f } }
};

void DoRendering(D3D12System* System)
{
	HRESULT hr;

	ID3D12CommandAllocator* CommandAllocator = System->CmdListReclaimer.GetOpenCommandAllocator(System);
	if (CommandAllocator == nullptr)
	{
		ASSERT(SUCCEEDED(hr = System->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator))));
	}

	ID3D12GraphicsCommandList* CommandList = System->CmdListReclaimer.GetOpenCommandList(System, CommandAllocator);
	if (CommandList == nullptr)
	{
		ASSERT(SUCCEEDED(System->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, 0, IID_PPV_ARGS(&CommandList))));
	}

	// TODO: Miiiii....texture streaming....lmao except not at all streamed
	if (TrianglePixelTexture == nullptr)
	{
		D3D12_HEAP_PROPERTIES HeapProps = {};
		HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		const int32 TextureWidth = 512;
		const int32 TextureHeight = 512;

		D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, TextureWidth, TextureHeight, 1, 1);
		D3D12_RESOURCE_DESC UploadResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(TextureWidth * TextureHeight * 4); // 4bpp

		HRESULT hr = System->Device->CreateCommittedResource(
			&HeapProps,
			D3D12_HEAP_FLAG_NONE,
			&UploadResourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&TrianglePixelTextureUpload));

		ASSERT(SUCCEEDED(hr));

		HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		hr = System->Device->CreateCommittedResource(
			&HeapProps,
			D3D12_HEAP_FLAG_NONE,
			&ResourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&TrianglePixelTexture));

		void* pTexturePixelData = nullptr;
		D3D12_RANGE readRange = {};        // We do not intend to read from this resource on the CPU.
		hr = TrianglePixelTextureUpload->Map(0, &readRange, &pTexturePixelData);
		ASSERT(SUCCEEDED(hr));

		// TODO: Ignoring alignment, since we assume the rows are aligned to where they're contiguous (i.e. stride is expected)
		uint32* PixelDataAs4Byte = (uint32*)pTexturePixelData;
		for (int32 Idx = 0; Idx < TextureWidth * TextureHeight; Idx++)
		{
			int32 X = Idx % TextureWidth;
			int32 Y = Idx / TextureWidth;

			PixelDataAs4Byte[Idx] = 0xFF0000FF | ((X % 256) << 16) | ((Y % 256) << 8);
		}

		TrianglePixelTextureUpload->Unmap(0, nullptr);

		//CommandList->CopyResource(TrianglePixelTexture, TrianglePixelTextureUpload);
		D3D12_TEXTURE_COPY_LOCATION CopyLocSrc = {}, CopyLocDst = {};
		CopyLocSrc.pResource = TrianglePixelTextureUpload;
		CopyLocSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		CopyLocSrc.PlacedFootprint.Offset = 0;
		CopyLocSrc.PlacedFootprint.Footprint.Width = TextureWidth;
		CopyLocSrc.PlacedFootprint.Footprint.Height = TextureHeight;
		CopyLocSrc.PlacedFootprint.Footprint.Depth = 1;
		CopyLocSrc.PlacedFootprint.Footprint.RowPitch = TextureWidth * 4;
		CopyLocSrc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		CopyLocDst.pResource = TrianglePixelTexture;
		CopyLocDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		CopyLocDst.SubresourceIndex = 0;

		CommandList->CopyTextureRegion(&CopyLocDst, 0, 0, 0, &CopyLocSrc, nullptr);

		{
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = TrianglePixelTexture;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			CommandList->ResourceBarrier(1, &barrier);
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		//D3D12 ERROR : ID3D12Device::CreateShaderResourceView : The Dimensions of the View are invalid due to at least one of the following conditions.MostDetailedMip(value = 0) must be between 0 and MipLevels - 1 of the Texture Resource, 9, inclusively.With the current MostDetailedMip, MipLevels(value = 0) must be between 1 and 10, inclusively, or -1 to default to all mips from MostDetailedMip, in order that the View fit on the Texture.[STATE_CREATION ERROR #31: CREATESHADERRESOURCEVIEW_INVALIDDIMENSIONS]
		System->Device->CreateShaderResourceView(TrianglePixelTexture, &srvDesc, System->TextureSRVHeap->GetCPUDescriptorHandleForHeapStart());
	}
	
	UINT backBufferIndex = GFrameCounter % NUM_BACKBUFFERS;

	ID3D12Resource* BackbufferResource = System->BackbufferResources[backBufferIndex];
	if (BackbufferResource == nullptr)
	{
		System->Swapchain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&BackbufferResource));
		System->BackbufferResources[backBufferIndex] = BackbufferResource;
	}

	D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};

	//CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
	
	if ((System->RTVHandlesCreated & (1 << backBufferIndex)) == 0)
	{
		ID3D12DescriptorHeap* DescriptorHeap = nullptr;
		D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
		DescriptorHeapDesc.NumDescriptors = NUM_BACKBUFFERS;
		DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		// Ha ha, could totally pool these fml
		System->Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		System->Device->CreateRenderTargetView(BackbufferResource, nullptr, rtvHandle);

		System->RTVHandlesCreated |= (1 << backBufferIndex);
		System->RTVHandles[backBufferIndex] = rtvHandle;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = System->RTVHandles[backBufferIndex];

	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = BackbufferResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		CommandList->ResourceBarrier(1, &barrier);
	}

	//float colours[4] = {1, 1, 1, 1};
	float colours[4] = {0.1f, 0.1f, 0.1f, 0.0f};
	//colours[1] = (GFrameCounter % 100) * 0.01f;
	CommandList->ClearRenderTargetView(rtvHandle, colours, 0, nullptr);

	{
		void* cbBegin = nullptr;
		D3D12_RANGE readRange = {};        // We do not intend to read from this resource on the CPU.
		HRESULT hr = PSCBuffer->Map(0, &readRange, &cbBegin);
		ASSERT(SUCCEEDED(hr));

		float cbData[8] = {
			((GFrameCounter + 500) % 1000) / 1000.0f,
			1.0f,
			1.0f,
			1.0f,
			//------------------
			1.0f,
			((GFrameCounter + 200) % 1000) / 1000.0f,
			1.0f,
			1.0f,
		};

		memcpy(cbBegin, cbData, sizeof(cbData));
		PSCBuffer->Unmap(0, nullptr);
	}

	{
		CommandList->SetPipelineState(PSO);
		CommandList->SetGraphicsRootSignature(System->RootSignature);

		float ConstantValues[4] = {
			0.0f,//((GFrameCounter + 500) % 1000) / 1000.0f,
			0.0f,//((GFrameCounter + 500) %  900) /  900.0f,
			0.0f,//((GFrameCounter + 500) %  700) /  700.0f,
			0.0f,//1.0f
		};

		ID3D12DescriptorHeap* ppHeaps[] = { System->TextureSRVHeap };
		CommandList->SetDescriptorHeaps(1, ppHeaps);

		CommandList->SetGraphicsRootDescriptorTable(0, System->TextureSRVHeap->GetGPUDescriptorHandleForHeapStart());
		CommandList->SetGraphicsRootConstantBufferView(1, PSCBuffer->GetGPUVirtualAddress());
		//CommandList->SetGraphicsRootConstantBufferView(0, PSCBuffer->GetGPUVirtualAddress());
		CommandList->SetGraphicsRootConstantBufferView(2, PSCBuffer->GetGPUVirtualAddress());

		// D3D12 ERROR: ID3D12Device::CreateGraphicsPipelineState: Root Signature doesn't match Pixel Shader: 
		// A Shader is declaring a resource object as a texture using a register mapped to a root descriptor SRV (ShaderRegister=0, RegisterSpace=0).
		// SRV or UAV root descriptors can only be Raw or Structured buffers.

		D3D12_VIEWPORT Viewport = {};
		Viewport.MinDepth = 0;
		Viewport.MaxDepth = 1;
		Viewport.TopLeftX = 0;
		Viewport.TopLeftY = 0;
		Viewport.Width = 1200;
		Viewport.Height = 700;
		CommandList->RSSetViewports(1, &Viewport);

		D3D12_RECT ScissorRect = {};
		ScissorRect.left = 0;
		ScissorRect.right = 1200;
		ScissorRect.top = 0;
		ScissorRect.bottom = 700;
		CommandList->RSSetScissorRects(1, &ScissorRect);

		CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		D3D12_VERTEX_BUFFER_VIEW vtbView = {};
		vtbView.BufferLocation = TriangleVertData->GetGPUVirtualAddress();
		vtbView.SizeInBytes = sizeof(triangleVerticesData);
		vtbView.StrideInBytes = sizeof(Vertex);

		CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		CommandList->IASetVertexBuffers(0, 1, &vtbView);
		CommandList->DrawInstanced(3, 1, 0, 0);
	}

	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = BackbufferResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		CommandList->ResourceBarrier(1, &barrier);
	}

	// Submit command list to cmd queue...
	CommandList->Close();

	ID3D12CommandList* CommandLists[] = { CommandList };
	System->DirectCommandQueue->ExecuteCommandLists(1, CommandLists);

	System->FrameFenceValue++;
	System->DirectCommandQueue->Signal(System->FrameFence, System->FrameFenceValue);

	System->CmdListReclaimer.CheckIfFenceFinished(System->FrameFence);

	System->CmdListReclaimer.NowDoneWithCommandList(CommandList);
	System->CmdListReclaimer.NowDoneWithCommandAllocator(CommandAllocator);
	System->CmdListReclaimer.OnFrameFenceSignaled(System->FrameFenceValue);

	int SyncInterval = 1;
	System->Swapchain->Present(SyncInterval, 0);

	//LOG("We did it, swapchain just presented");

	GFrameCounter++;
}


void InitRendering(D3D12System* System)
{
	if (System->FrameFence == nullptr)
	{
		ASSERT(SUCCEEDED(System->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&System->FrameFence))));
	}

	if (PSO == nullptr)
	{
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.InputLayout = { inputElementDescs, (sizeof(inputElementDescs) / sizeof(inputElementDescs[0])) };
		PSODesc.pRootSignature = System->RootSignature;
		PSODesc.VS = VertexShaderByteCode;
		PSODesc.PS = PixelShaderByteCode;
		PSODesc.RasterizerState = GetDefaultRasterizerDesc();
		PSODesc.BlendState = GetDefaultBlendStateDesc();
		PSODesc.DepthStencilState.DepthEnable = FALSE;
		PSODesc.DepthStencilState.StencilEnable = FALSE;
		PSODesc.SampleMask = UINT_MAX;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
		PSODesc.SampleDesc.Count = 1;

		//ID3D12ShaderReflection

		System->Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO));
	}

	if (TriangleVertData == nullptr)
	{
		const int vertexBufferSize = sizeof(triangleVerticesData);

		// TODO: Using upload heap for GPU access is not ideal, we can get away with this because it's small, but ideally we'd want to copy it to an upload heap,
		// copy that to a default heap and use the default heap for GPU-side access
		D3D12_HEAP_PROPERTIES HeapProps = {};
		HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		HRESULT hr = System->Device->CreateCommittedResource(
			&HeapProps,
			D3D12_HEAP_FLAG_NONE,
			&ResourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&TriangleVertData));

		ASSERT(SUCCEEDED(hr));


		// Copy the triangle data to the vertex buffer.
		void* pVertexDataBegin = nullptr;
		D3D12_RANGE readRange = {};        // We do not intend to read from this resource on the CPU.
		hr = TriangleVertData->Map(0, &readRange, &pVertexDataBegin);
		ASSERT(SUCCEEDED(hr));

		memcpy(pVertexDataBegin, triangleVerticesData, sizeof(triangleVerticesData));
		TriangleVertData->Unmap(0, nullptr);
	}

	// TODO: Code dup with above, also same about using upload heap
	if (PSCBuffer == nullptr)
	{
		D3D12_HEAP_PROPERTIES HeapProps = {};
		HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(float) * 4 * 2);

		HRESULT hr = System->Device->CreateCommittedResource(
			&HeapProps,
			D3D12_HEAP_FLAG_NONE,
			&ResourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&PSCBuffer));

		ASSERT(SUCCEEDED(hr));
	}

	if (System->TextureSRVHeap == nullptr)
	{
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		HRESULT hr = System->Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&System->TextureSRVHeap));

		ASSERT(SUCCEEDED(hr));
	}
}

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
	LRESULT result = 0;

	switch (msg)
	{
	case WM_DESTROY:
	case WM_QUIT:
		shouldQuit = true;
	default: {
		result = DefWindowProc(window, msg, wparam, lparam);
	} break;
	}

	return result;
}



