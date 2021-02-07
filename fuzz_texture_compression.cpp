
#include "fuzz_texture_compression.h"

#include "d3d12_ext.h"

void SetupPersistentOnTextureCompressionFuzzer(D3DTextureCompressionFuzzingPersistentState* Persist, ID3D12Device* Device)
{
	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HRESULT hr = Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&Persist->CmdQueue));
	ASSERT(SUCCEEDED(hr));

	hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Persist->ExecFence));
	ASSERT(SUCCEEDED(hr));
}

void SetSeedOnTextureCompressionFuzzer(TextureCompressionFuzzingState* Fuzzer, uint64 Seed)
{
	Fuzzer->InitialFuzzSeed = Seed;
	Fuzzer->RNGState.seed(Seed);
}

void DoIterationsWithTextureCompressionFuzzer(TextureCompressionFuzzingState* Fuzzer, int32 NumIterations)
{
	HRESULT hr;
	for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
	{
		const int32 TextureWidth = 64;
		const int32 TextureHeight = 64;
		const DXGI_FORMAT CompressedTextureFormat = DXGI_FORMAT_BC3_UNORM;
		const DXGI_FORMAT UncompressedTextureFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

		// TODO?????
		const int32 BufferSize = 4096 + 1024;


		ID3D12Resource* UploadBuffer = nullptr;
		{
			D3D12_HEAP_PROPERTIES HeapProps = {};
			HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC UploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);
			hr = Fuzzer->D3DDevice->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &UploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&UploadBuffer));
			ASSERT(SUCCEEDED(hr));

			void* pPixelData = nullptr;
			D3D12_RANGE readRange = {};
			HRESULT hr = UploadBuffer->Map(0, &readRange, &pPixelData);
			ASSERT(SUCCEEDED(hr));

			memset(pPixelData, 0, BufferSize);

			UploadBuffer->Unmap(0, nullptr);
		}

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
		uint64 RowPitch = 0;

		ID3D12Resource* TextureResource = nullptr;
		{
			D3D12_HEAP_PROPERTIES HeapProps = {};
			HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC TextureResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(CompressedTextureFormat, TextureWidth, TextureHeight, 1, 1);
			hr = Fuzzer->D3DDevice->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &TextureResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&TextureResource));
			ASSERT(SUCCEEDED(hr));

			uint32 UnusedNumRows;
			uint64 UnusedTotal;
			Fuzzer->D3DDevice->GetCopyableFootprints(&TextureResourceDesc, 0, 1, 0, &Layout, &UnusedNumRows, &RowPitch, &UnusedTotal);
		}

		ID3D12Resource* RenderTextureResource = nullptr;
		{
			D3D12_HEAP_PROPERTIES HeapProps = {};
			HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC RenderTextureResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(UncompressedTextureFormat, TextureWidth, TextureHeight, 1, 1);
			//RenderTextureResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			hr = Fuzzer->D3DDevice->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &RenderTextureResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&RenderTextureResource));
			ASSERT(SUCCEEDED(hr));
		}

		ID3D12CommandAllocator* CommandAllocator = nullptr;
		Fuzzer->D3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator));
		ASSERT(SUCCEEDED(hr));
		ASSERT(CommandAllocator != nullptr);

		ID3D12GraphicsCommandList* CommandList = nullptr;
		hr = Fuzzer->D3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, 0, IID_PPV_ARGS(&CommandList));
		ASSERT(SUCCEEDED(hr));
		ASSERT(CommandList != nullptr);

		{
			D3D12_TEXTURE_COPY_LOCATION CopyLocSrc = {}, CopyLocDst = {};
			CopyLocSrc.pResource = UploadBuffer;
			CopyLocSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			CopyLocSrc.PlacedFootprint.Offset = 0;
			CopyLocSrc.PlacedFootprint.Footprint.Width = TextureWidth;
			CopyLocSrc.PlacedFootprint.Footprint.Height = TextureHeight;
			CopyLocSrc.PlacedFootprint.Footprint.Depth = 1;
			CopyLocSrc.PlacedFootprint.Footprint.RowPitch = RowPitch;
			CopyLocSrc.PlacedFootprint.Footprint.Format = CompressedTextureFormat;

			CopyLocDst.pResource = TextureResource;
			CopyLocDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			CopyLocDst.SubresourceIndex = 0;

			CommandList->CopyTextureRegion(&CopyLocDst, 0, 0, 0, &CopyLocSrc, nullptr);
		}

		{
			D3D12_RESOURCE_BARRIER Barrier = {};
			Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			Barrier.Transition.pResource = TextureResource;
			Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			CommandList->ResourceBarrier(1, &Barrier);
		}

		{
			CommandList->CopyResource(RenderTextureResource, TextureResource);
		}

		CommandList->Close();

		ID3D12CommandList* CommandLists[] = { CommandList };
		Fuzzer->Persistent->CmdQueue->ExecuteCommandLists(1, CommandLists);
		uint64 ValueSignaled = Fuzzer->Persistent->ExecFenceToSignal;
		Fuzzer->Persistent->CmdQueue->Signal(Fuzzer->Persistent->ExecFence, ValueSignaled);
		Fuzzer->Persistent->ExecFenceToSignal++;

		HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		Fuzzer->Persistent->ExecFence->SetEventOnCompletion(ValueSignaled, hEvent);
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);


		CommandList->Release();
		CommandAllocator->Reset();
		CommandAllocator->Release();

		UploadBuffer->Release();
		TextureResource->Release();
	}
}


