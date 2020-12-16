
#include <stdio.h>

#include <Windows.h>

#include <d3d12.h>

#include <d3dcompiler.h>

//#include "d3dx12.h"


#include <dxgi1_2.h>

#include "d3d12_ext.h"

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#define ASSERT(cond) do { if (!(cond)) { char output[1024] = {}; snprintf(output, sizeof(output), "[%s:%d] Assertion failed '%s'\n", __FILE__, __LINE__, #cond); OutputDebugStringA(output); DebugBreak(); } } while(0)

#define LOG(fmt, ...) do { char output[2048] = {}; snprintf(output, sizeof(output), fmt "\n", ## __VA_ARGS__); OutputDebugStringA(output); } while(0)

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

#include <stdint.h>

#include <vector>

using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
using int64 = int64_t;

struct CommandListReclaimer
{
	struct CommandListToReclaim
	{
		ID3D12GraphicsCommandList* CmdList = nullptr;
		uint64 FenceValueToWaitOn = 0;
	};

	std::vector<CommandListToReclaim> PendingNextFence;
	std::vector<CommandListToReclaim> WaitingForFence;
	std::vector<CommandListToReclaim> NowAvailable;

	void NowDoneWithCommandList(ID3D12GraphicsCommandList* CmdList)
	{
		// TODO: Oh no
		//CmdList->AddRef();
		PendingNextFence.push_back({ CmdList, 0 });
	}

	void OnFrameFenceSignaled(uint64 SignaledValue)
	{
		for (auto Pending : PendingNextFence)
		{
			WaitingForFence.push_back({ Pending.CmdList, SignaledValue });
		}

		PendingNextFence.clear();
	}

	void CheckIfFenceFinished(ID3D12Fence* FrameFence)
	{
		uint64 CompletedValue = FrameFence->GetCompletedValue();

		for (int32 i = 0; i < WaitingForFence.size(); i++)
		{
			if (WaitingForFence[i].FenceValueToWaitOn <= CompletedValue)
			{
				NowAvailable.push_back(WaitingForFence[i]);
				WaitingForFence[i] = WaitingForFence.back();
				WaitingForFence.pop_back();
				i--;
			}
		}
	}

	ID3D12GraphicsCommandList* GetOpenCommandList(struct D3D12System* System);
};


struct D3D12System {
	HWND Window = nullptr;
	ID3D12Device* Device = nullptr;
	ID3D12CommandQueue* DirectCommandQueue = nullptr;
	IDXGISwapChain1* Swapchain = nullptr;
	ID3D12CommandAllocator* CommandAllocator = nullptr;
	ID3D12RootSignature* RootSignature = nullptr;

	CommandListReclaimer CmdListReclaimer;

	ID3D12Fence* FrameFence = nullptr;
	uint64 FrameFenceValue = 0;
};

ID3D12GraphicsCommandList* CommandListReclaimer::GetOpenCommandList(D3D12System* System)
{
	if (NowAvailable.size() > 0)
	{
		auto* CmdList = NowAvailable.back().CmdList;
		NowAvailable.pop_back();

		CmdList->Reset(System->CommandAllocator, nullptr);
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
"struct PSInput\n"
"{\n"
"	float4 position : SV_POSITION;\n"
"	float4 color : COLOR;\n"
"};\n"
"\n"
"PSInput MainVS(float4 position : POSITION, float4 color : COLOR)\n"
"{\n"
"	PSInput result;\n"
"\n"
"	result.position = position;\n"
"	result.color = color;\n"
"\n"
"	return result;\n"
"}\n";


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
"cbuffer MyOtherBuffer1 : register(b1)\n"
"{\n"
"	float4 otherConstant1;\n"
"	float4 otherConstant2;\n"
"};\n"
"\n"
"float4 MainPS(PSInput input) : SV_TARGET\n"
"{\n"
"	return input.color * otherConstant1 * otherConstant2 + constant1;\n"
"}\n";

D3D12_SHADER_BYTECODE VertexShaderByteCode;
D3D12_SHADER_BYTECODE PixelShaderByteCode;

enum struct D3DShaderType {
	Vertex,
	Pixel
};

const char* GetTargetForShaderType(D3DShaderType Type) {
	if (Type == D3DShaderType::Vertex) {
		return "vs_5_0";
	}
	else if (Type == D3DShaderType::Pixel) {
		return "ps_5_0";
	}
	else {
		ASSERT(false && "Bad shader type!!!");
	}
}

D3D12_SHADER_BYTECODE CompileShaderCode(const char* ShaderCode, D3DShaderType ShaderType, const char* ShaderSourceName, const char* EntryPoint) {
	ID3DBlob* ByteCode = nullptr;
	ID3DBlob* ErrorMsg = nullptr;
	UINT CompilerFlags = D3DCOMPILE_DEBUG;
	HRESULT hr = D3DCompile(ShaderCode, strlen(ShaderCode), ShaderSourceName, nullptr, nullptr, EntryPoint, GetTargetForShaderType(ShaderType), CompilerFlags, 0, &ByteCode, &ErrorMsg);
	if (SUCCEEDED(hr)) {
		D3D12_SHADER_BYTECODE ByteCodeObj;
		ByteCodeObj.pShaderBytecode = ByteCode->GetBufferPointer();
		ByteCodeObj.BytecodeLength = ByteCode->GetBufferSize();
		return ByteCodeObj;
	}
	else {
		LOG("Compile of '%s' failed, hr = 0x%08X, err msg = '%s'", ShaderSourceName, hr, (ErrorMsg && ErrorMsg->GetBufferPointer()) ? (const char*)ErrorMsg->GetBufferPointer() : "<NONE_GIVEN>");
		ASSERT(false && "Fix the damn shaders");
		return D3D12_SHADER_BYTECODE();
	}
}


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

	WNDCLASSA WindowClass = {};
	WindowClass.lpfnWndProc = WindowProc;
	WindowClass.lpszClassName = "D3D12Test";
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClassA(&WindowClass);

	HWND window = CreateWindowA("D3D12Test", "D3D12Test Window", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, 0, 0, instance, 0);
	ShowWindow(window, SW_SHOW);

	OutputDebugStringA("Blllaaah Yooooo.\n");


	ID3D12Device* Device = nullptr;
	ASSERT(SUCCEEDED(D3D12CreateDevice(ChosenAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device))));

	ID3D12CommandQueue* CommandQueue = nullptr;
	
	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ASSERT(SUCCEEDED(Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&CommandQueue))));

	DXGI_SWAP_CHAIN_DESC1 SwapchainDesc = {};
	SwapchainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	SwapchainDesc.SampleDesc.Count = 1;
	SwapchainDesc.BufferCount = 3;
	SwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;

	IDXGISwapChain1* SwapChain = nullptr;
	ASSERT(SUCCEEDED(hr = DXGIFactory->CreateSwapChainForHwnd(CommandQueue, window, &SwapchainDesc, nullptr, nullptr, &SwapChain)));

	ID3D12CommandAllocator* CommandAllocator = nullptr;
	ASSERT(SUCCEEDED(hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator))));

	D3D12System D3DSystem;
	D3DSystem.Device = Device;
	D3DSystem.DirectCommandQueue = CommandQueue;
	D3DSystem.Swapchain = SwapChain;
	D3DSystem.Window = window;
	D3DSystem.CommandAllocator = CommandAllocator;

	D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
	RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	const int NumRootParams = 2;

	RootSigDesc.NumParameters = NumRootParams;
	auto* RootParams = new D3D12_ROOT_PARAMETER[NumRootParams];

	RootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	RootParams[0].Constants.Num32BitValues = 4;
	RootParams[0].Constants.RegisterSpace = 0;
	RootParams[0].Constants.ShaderRegister = 0;
	RootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	RootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	RootParams[1].Descriptor.RegisterSpace = 0;
	RootParams[1].Descriptor.ShaderRegister = 1;
	RootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	RootSigDesc.pParameters = RootParams;

	ID3DBlob* RootSigBlob = nullptr;
	ID3DBlob* RootSigErrorBlob = nullptr;

	D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootSigBlob, &RootSigErrorBlob);

	ID3D12RootSignature* RootSig = nullptr;
	Device->CreateRootSignature(0, RootSigBlob->GetBufferPointer(), RootSigBlob->GetBufferSize(), IID_PPV_ARGS(&RootSig));

	D3DSystem.RootSignature = RootSig;

	// Yeah they're globals...should have a way to pass down "assets" as well I guess, or maybe make the renderer do it idk
	VertexShaderByteCode = CompileShaderCode(VertexShaderCode, D3DShaderType::Vertex, "vertex.shader", "MainVS");
	PixelShaderByteCode = CompileShaderCode(PixelShaderCode, D3DShaderType::Pixel, "pixel.shader", "MainPS");

	InitRendering(&D3DSystem);

	bool shouldQuit = false;
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
	{ { 0.0f, 0.8f, 0.0f },{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ { 0.8f, -0.8f, 0.0f },{ 0.0f, 1.0f, 0.0f, 1.0f } },
	{ { -0.8f, -0.8f, 0.0f },{ 0.0f, 0.0f, 1.0f, 1.0f } }
};

void DoRendering(D3D12System* System)
{
	ID3D12GraphicsCommandList* CommandList = System->CmdListReclaimer.GetOpenCommandList(System);
	if (CommandList == nullptr)
	{
		ASSERT(SUCCEEDED(System->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, System->CommandAllocator, 0, IID_PPV_ARGS(&CommandList))));
	}

	// Fill up command list...
	//CommandList->clea

	//IDXGI
	//IDXGISwapChain3* SwapChain3;
	//System->Swapchain->QueryInterface(&spSwapChain3);
	// spSwapChain3->GetCurrentBackBufferIndex();
	// TODO: Get actual to-be-presented index
	UINT backBufferIndex = GFrameCounter % 3;

	ID3D12Resource* BackbufferResource = nullptr;
	System->Swapchain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&BackbufferResource));

	D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};

	//CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
	
	ID3D12DescriptorHeap* DescriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
	DescriptorHeapDesc.NumDescriptors = 3;
	DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	System->Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	System->Device->CreateRenderTargetView(BackbufferResource, nullptr, rtvHandle);

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

		CommandList->SetGraphicsRoot32BitConstants(0, 4, ConstantValues, 0);
		CommandList->SetGraphicsRootConstantBufferView(1, PSCBuffer->GetGPUVirtualAddress());

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
}

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
	LRESULT result = 0;

	switch (msg)
	{
	default: {
		result = DefWindowProc(window, msg, wparam, lparam);
	} break;
	}

	return result;
}



