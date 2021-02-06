#include "fuzz_reserved_resources.h"

#include "d3d12_ext.h"

void SetSeedOnReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, uint64_t Seed)
{
	Fuzzer->InitialFuzzSeed = Seed;
	Fuzzer->RNGState.seed(Seed);
}

// TODO: Put in some utils header
// TODO: Would bits make more sense for when it's not a byte-aligned format?
int32 GetBytesPerPixelForFormat(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 12;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        return 8;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_YUY2:
        return 4;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        return 3;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return 2;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
        return 1;

    default:
        return 0;
    }
}

void DoIterationsWithReservedResourceFuzzer(ReservedResourceFuzzingState* Fuzzer, int32_t NumIterations)
{
	HRESULT hr;

	for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
	{
		DXGI_FORMAT PossibleFormats[] = {
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_R32G32_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_B5G6R5_UNORM
		};

		int32 TextureWidth = 1 << Fuzzer->GetIntInRange(8, 10);
		int32 TextureHeight = 1 << Fuzzer->GetIntInRange(8, 10);
		int32 TextureDepth = 1;
		
		DXGI_FORMAT TextureFormat = PossibleFormats[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(PossibleFormats) - 1)];

		int32 bpp = GetBytesPerPixelForFormat(TextureFormat);

		D3D12_RESOURCE_DESC TextureDesc = {};
		if (Fuzzer->GetFloat01() < 0.5f)
		{
			TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(TextureFormat, TextureWidth, TextureHeight, 1, 1);
		}
		else
		{
			TextureWidth = 1 << Fuzzer->GetIntInRange(6, 8);
			TextureHeight = 1 << Fuzzer->GetIntInRange(6, 8);
			TextureDepth = 1 << Fuzzer->GetIntInRange(5, 9);
			TextureDesc = CD3DX12_RESOURCE_DESC::Tex3D(TextureFormat, TextureWidth, TextureHeight, TextureDepth, 1);
		}
		TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
		TextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

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
		ASSERT(NumTilesForResource > 0);
		// Don't support this yet lol
		ASSERT(PackedMipInfo.NumPackedMips == 0);

		// Add some slack, because
		int32 NumTilesInHeap = NumTilesForResource + Fuzzer->GetIntInRange(0, 16);
		D3D12_HEAP_DESC TextureHeapDesc = {};
		TextureHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
		//TextureHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
		TextureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		TextureHeapDesc.SizeInBytes = (uint64)NumTilesInHeap * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

		ID3D12Heap* TextureHeap = nullptr;
		Fuzzer->D3DDevice->CreateHeap(&TextureHeapDesc, IID_PPV_ARGS(&TextureHeap));

		int32 TextureWidthTiles = (TextureWidth + TileShape.WidthInTexels - 1) / TileShape.WidthInTexels;
		int32 TextureHeightTiles = (TextureHeight + TileShape.HeightInTexels - 1) / TileShape.HeightInTexels;
		int32 TextureDepthTiles = (TextureDepth + TileShape.DepthInTexels - 1) / TileShape.DepthInTexels;


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
			Sizes.UseBox = true;
			Sizes.NumTiles = Sizes.Width * Sizes.Height * Sizes.Depth;


			D3D12_TILE_RANGE_FLAGS RangeFlags = D3D12_TILE_RANGE_FLAG_NONE;

			int32 TotalClaimedTiles = Sizes.Width * Sizes.Height * Sizes.Depth;

			UINT RangeHeapOffset = Fuzzer->GetIntInRange(0, NumTilesInHeap - TotalClaimedTiles);

			UINT RangeTileCount = TotalClaimedTiles;

			Fuzzer->Persistent->CmdQueue->UpdateTileMappings(ReservedResource, 1, &Coords, &Sizes, TextureHeap, 1, &RangeFlags, &RangeHeapOffset, &RangeTileCount, D3D12_TILE_MAPPING_FLAG_NONE);
		}

		uint64 ValueToSignal = Fuzzer->Persistent->ExecFenceToSignal;

		Fuzzer->Persistent->CmdQueue->Signal(Fuzzer->Persistent->ExecFence, ValueToSignal);

		Fuzzer->Persistent->DeferCleanupResourceandHeap(ReservedResource, TextureHeap, ValueToSignal);

		uint64 LastCompletedFence = Fuzzer->Persistent->ExecFence->GetCompletedValue();
		Fuzzer->Persistent->CheckCleanup(LastCompletedFence);

		Fuzzer->Persistent->ExecFenceToSignal++;
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




