//
// This is a D3D12 test app that should run and exit without errors
// However, on an NVIDIA GeForce RTX 2080 running driver 465.89 this triggers a D3D validation error saying:
// D3D12 ERROR: ID3D12Device::CreateShaderResourceView: Specified CPU descriptor handle ptr=0x0000000000000005 does not refer to a location in a descriptor heap.
// [ EXECUTION ERROR #646: INVALID_DESCRIPTOR_HANDLE]
//
// This happens when multiple threads are creating descriptor heaps, writing an SRV descriptor to them, and then destroying the descriptor heap
// It only repros with the D3D validation layer enabled, since the debug layer is what catches the problem
//
// It does not repro on Intel drivers, or the WARP adapter. It also does not repro if
// locks are put around creation/destruction of the descriptor heaps
//
// Tested on drivers 465.89, 461.92, 457.51, 419.67 with an RTX 2080, they all repro'd the issue
//


#include <stdio.h>

#include <stdint.h>

#include <vector>
#include <thread>
#include <limits>
#include <mutex>

#include <Windows.h>

#include <assert.h>

#include <d3d12.h>
#include <dxgi1_2.h>

struct CD3DX12_RESOURCE_DESC : public D3D12_RESOURCE_DESC
{
	CD3DX12_RESOURCE_DESC() = default;
	explicit CD3DX12_RESOURCE_DESC(const D3D12_RESOURCE_DESC& o) :
		D3D12_RESOURCE_DESC(o)
	{}
	CD3DX12_RESOURCE_DESC(
		D3D12_RESOURCE_DIMENSION dimension,
		UINT64 alignment,
		UINT64 width,
		UINT height,
		UINT16 depthOrArraySize,
		UINT16 mipLevels,
		DXGI_FORMAT format,
		UINT sampleCount,
		UINT sampleQuality,
		D3D12_TEXTURE_LAYOUT layout,
		D3D12_RESOURCE_FLAGS flags)
	{
		Dimension = dimension;
		Alignment = alignment;
		Width = width;
		Height = height;
		DepthOrArraySize = depthOrArraySize;
		MipLevels = mipLevels;
		Format = format;
		SampleDesc.Count = sampleCount;
		SampleDesc.Quality = sampleQuality;
		Layout = layout;
		Flags = flags;
	}
	static inline CD3DX12_RESOURCE_DESC Buffer(
		const D3D12_RESOURCE_ALLOCATION_INFO& resAllocInfo,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_BUFFER, resAllocInfo.Alignment, resAllocInfo.SizeInBytes,
			1, 1, 1, DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags);
	}
	static inline CD3DX12_RESOURCE_DESC Buffer(
		UINT64 width,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
		UINT64 alignment = 0)
	{
		return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_BUFFER, alignment, width, 1, 1, 1,
			DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags);
	}
	static inline CD3DX12_RESOURCE_DESC Tex1D(
		DXGI_FORMAT format,
		UINT64 width,
		UINT16 arraySize = 1,
		UINT16 mipLevels = 0,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		UINT64 alignment = 0)
	{
		return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE1D, alignment, width, 1, arraySize,
			mipLevels, format, 1, 0, layout, flags);
	}
	static inline CD3DX12_RESOURCE_DESC Tex2D(
		DXGI_FORMAT format,
		UINT64 width,
		UINT height,
		UINT16 arraySize = 1,
		UINT16 mipLevels = 0,
		UINT sampleCount = 1,
		UINT sampleQuality = 0,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		UINT64 alignment = 0)
	{
		return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE2D, alignment, width, height, arraySize,
			mipLevels, format, sampleCount, sampleQuality, layout, flags);
	}
	static inline CD3DX12_RESOURCE_DESC Tex3D(
		DXGI_FORMAT format,
		UINT64 width,
		UINT height,
		UINT16 depth,
		UINT16 mipLevels = 0,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		UINT64 alignment = 0)
	{
		return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE3D, alignment, width, height, depth,
			mipLevels, format, 1, 0, layout, flags);
	}
	inline UINT16 Depth() const
	{
		return (Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? DepthOrArraySize : 1);
	}
	inline UINT16 ArraySize() const
	{
		return (Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D ? DepthOrArraySize : 1);
	}
};

typedef int32_t int32;
typedef uint64_t uint64;

#define ASSERT assert

// Uncomment this line to use mutex locks around creating/destroying descriptor heaps, which prevents a repro
//#define WITH_MUTEX_LOCKS

#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "D3D12.lib")

int main(int argc, char** argv) {

	// Only repros with the debug layer on, since the issue is validation complaining that a descriptor heap pointer is invalid
	// However the debug layer does not give complaints except related to the issue,
	// and it does not repro if we add mutexes
	ID3D12Debug1* D3D12DebugLayer = nullptr;
	D3D12GetDebugInterface(IID_PPV_ARGS(&D3D12DebugLayer));
	D3D12DebugLayer->EnableDebugLayer();

	IDXGIFactory2* DXGIFactory = nullptr;

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&DXGIFactory));
	ASSERT(SUCCEEDED(hr));

	IDXGIAdapter* ChosenAdapter = nullptr;

	{
		IDXGIAdapter* Adapter = nullptr;
		for (int AdapterIndex = 0; true; AdapterIndex++) {
			hr = DXGIFactory->EnumAdapters(AdapterIndex, &Adapter);
			if (!SUCCEEDED(hr)) {
				break;
			}

			DXGI_ADAPTER_DESC AdapterDesc = {};
			Adapter->GetDesc(&AdapterDesc);

			// Get the adapter that's an Nvidia GPU
			// Tested on "NVIDIA GeForce RTX 2080", where AdapterDesc.DeviceId == 0x1E82
			if (AdapterDesc.VendorId == 0x10DE) {
				ChosenAdapter = Adapter;
			}

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
	hr = SUCCEEDED(D3D12CreateDevice(ChosenAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device)));
	ASSERT(hr);

	// Repros with as few as 2 threads, though more threads makes a repro more likely and happen faster
	const int ThreadCount = 8;
	const int IterationsPerThread = 10*1000*1000;
	std::vector<std::thread> RunnerThreads;

	std::mutex DebuggingMutex;

	for (int i = 0; i < ThreadCount; i++)
	{
		RunnerThreads.emplace_back([Device, IterationsPerThread, MutexPtr = &DebuggingMutex]() {
			HRESULT hr;
			const DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = Format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;

			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
			srvHeapDesc.NumDescriptors = 1;
			srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			// Make a texture just so we can refer to it when making an SRV descriptor
			ID3D12Resource* Resource = nullptr;
			{
				int TextureWidth = 128;
				int TextureHeight = 128;
				D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(Format, TextureWidth, TextureHeight, 1, 1);
				D3D12_HEAP_PROPERTIES HeapProps = {};
				HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
				hr = Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&Resource));
				ASSERT(SUCCEEDED(hr));
			}

			for (int Iteration = 0; Iteration < IterationsPerThread; Iteration++)
			{
				ID3D12DescriptorHeap* TextureSRVHeap = nullptr;

				{
					// Adding this lock, and the one below, prevents the repro from occuring
					// Having only one of these locks active does not prevent a repro, though it does make it less likely
#if defined(WITH_MUTEX_LOCKS)
					std::lock_guard<std::mutex> Lock(*MutexPtr);
#endif

					hr = Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&TextureSRVHeap));
					ASSERT(SUCCEEDED(hr));
				}

				D3D12_CPU_DESCRIPTOR_HANDLE SRVCPUDescriptor = TextureSRVHeap->GetCPUDescriptorHandleForHeapStart();
				Device->CreateShaderResourceView(Resource, &srvDesc, SRVCPUDescriptor);

				{
					// Adding this lock, and the one above, prevents the repro from occuring
					// Having only one of these locks active does not prevent a repro, though it does make it less likely
#if defined(WITH_MUTEX_LOCKS)
					std::lock_guard<std::mutex> Lock(*MutexPtr);
#endif
					TextureSRVHeap->Release();
				}
			}
		});
	}

	// Let threads run to completion
	for (auto& RunnerThread : RunnerThreads)
	{
		RunnerThread.join();
	}

	return 0;
}
