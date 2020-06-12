
#include <stdio.h>

#include <Windows.h>

#include <d3d12.h>

//#include "d3dx12.h"


#include <dxgi1_2.h>


#pragma comment(lib, "D3D12.lib")

#define ASSERT(cond) do { if (!(cond)) { char output[1024] = {}; snprintf(output, sizeof(output), "[%s:%d] Assertion failed '%s'\n", __FILE__, __LINE__, #cond); OutputDebugStringA(output); DebugBreak(); } } while(0)

#define LOG(fmt, ...) do { char output[2048] = {}; snprintf(output, sizeof(output), fmt "\n", ## __VA_ARGS__); OutputDebugStringA(output); } while(0)

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);


struct D3D12System {
	HWND Window = nullptr;
	ID3D12Device* Device = nullptr;
	ID3D12CommandQueue* DirectCommandQueue = nullptr;
	IDXGISwapChain1* Swapchain = nullptr;
	ID3D12CommandAllocator* CommandAllocator = nullptr;
};

void DoRendering(D3D12System* System);

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


ID3D12PipelineState* PSO = nullptr;

int GFrameCounter = 0;

void DoRendering(D3D12System* System)
{
	ID3D12GraphicsCommandList* CommandList = nullptr;
	ASSERT(SUCCEEDED(System->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, System->CommandAllocator, 0, IID_PPV_ARGS(&CommandList))));

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

	float colours[4] = {1, 1, 1, 1};
	colours[1] = (GFrameCounter % 100) * 0.01f;
	CommandList->ClearRenderTargetView(rtvHandle, colours, 0, nullptr);

	//if (PSO == nullptr)
	//{
	//	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
	//	PSODesc.NumRenderTargets = 1;
	//	PSODesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	//	System->Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO));
	//}

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

	int SyncInterval = 1;
	System->Swapchain->Present(SyncInterval, 0);

	LOG("We did it, swapchain just presented");

	CommandList->Release();

	GFrameCounter++;
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



