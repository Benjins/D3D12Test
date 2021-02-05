#include "fuzz_reserved_resources.h"

#include "d3d12_ext.h"

void SetSeedOnReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, uint64_t Seed)
{
	Fuzzer->InitialFuzzSeed = Seed;
	Fuzzer->RNGState.seed(Seed);
}

void DoIterationsWithReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, int32_t NumIterations)
{
	HRESULT hr;

	for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
	{
		int32 TextureWidth = 1024;
		int32 TextureHeight = 1024;
		DXGI_FORMAT TextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		int32 bpp = 4;

		D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(TextureFormat, TextureWidth, TextureHeight, 1, 1);
		TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

		//TextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		ID3D12Resource* ReservedResource = nullptr;
		hr = Fuzzer->D3DDevice->CreateReservedResource(&TextureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&ReservedResource));
		ASSERT(SUCCEEDED(hr));
		ASSERT(ReservedResource);


		UINT NumTilesForResource = 0;
		D3D12_PACKED_MIP_INFO PackedMipInfo = {};
		D3D12_TILE_SHAPE TileShape = {};
		UINT NumSubresourceTiling = 0;
		D3D12_SUBRESOURCE_TILING SubresourceTiling = {};
		Fuzzer->D3DDevice->GetResourceTiling(ReservedResource, &NumTilesForResource, &PackedMipInfo, &TileShape, &NumSubresourceTiling, 0, &SubresourceTiling);

		// Add some slack, because
		int32 NumTilesInHeap = NumTilesForResource + Fuzzer->GetIntInRange(0, NumTilesForResource);
		D3D12_HEAP_DESC TextureHeapDesc = {};
		TextureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		TextureHeapDesc.SizeInBytes = (uint64)NumTilesInHeap * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

		ID3D12Heap* TextureHeap = nullptr;
		Fuzzer->D3DDevice->CreateHeap(&TextureHeapDesc, IID_PPV_ARGS(&TextureHeap));

		int32 TextureWidthTiles = TextureWidth / TileShape.WidthInTexels;
		int32 TextureHeightTiles = TextureHeight / TileShape.HeightInTexels;
		int32 TextureDepthTiles = 1; // TODO: For volumetrics, this isn't 1 always


		for (int32 MappingIteration = 0; MappingIteration < 30; MappingIteration++)
		{
			D3D12_TILED_RESOURCE_COORDINATE Coords = {};
			Coords.X = Fuzzer->GetIntInRange(0, TextureWidthTiles - 1);
			Coords.Y = Fuzzer->GetIntInRange(0, TextureHeightTiles - 1);
			Coords.Z = Fuzzer->GetIntInRange(0, TextureDepthTiles - 1);


			D3D12_TILE_REGION_SIZE Sizes = {};
			Sizes.Width = Fuzzer->GetIntInRange(1, TextureWidthTiles - Coords.X);
			Sizes.Height = Fuzzer->GetIntInRange(1, TextureHeightTiles - Coords.Y);
			Sizes.Depth = Fuzzer->GetIntInRange(1, TextureDepthTiles - Coords.Z);


			D3D12_TILE_RANGE_FLAGS RangeFlags = D3D12_TILE_RANGE_FLAG_NONE;

			int32 TotalClaimedTiles = Sizes.Width * Sizes.Height * Sizes.Depth;

			UINT RangeHeapOffset = Fuzzer->GetIntInRange(0, NumTilesInHeap - TotalClaimedTiles);

			UINT RangeTileCount = TotalClaimedTiles;

			Fuzzer->Persistent->CmdQueue->UpdateTileMappings(ReservedResource, 1, &Coords, &Sizes, TextureHeap, 1, &RangeFlags, &RangeHeapOffset, &RangeTileCount, D3D12_TILE_MAPPING_FLAG_NONE);
		}
	}
}




void SetupPersistentOnReservedResourceFuzzer(D3DReservedResourceFuzzingPersistentState* Persist, ID3D12Device* Device)
{
	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HRESULT hr = Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&Persist->CmdQueue));
	ASSERT(SUCCEEDED(hr));

	hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Persist->ExecFence));
	ASSERT(SUCCEEDED(hr));
}




