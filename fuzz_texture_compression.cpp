
#include "fuzz_texture_compression.h"

#include "shader_meta.h"

#include "d3d12_ext.h"


//#define WITH_PIPELINE_STATS_QUERY

static D3D12_RASTERIZER_DESC GetDefaultRasterizerDesc()
{
	D3D12_RASTERIZER_DESC Desc = {};
	Desc.FillMode = D3D12_FILL_MODE_SOLID;
	Desc.CullMode = D3D12_CULL_MODE_NONE;
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

static D3D12_BLEND_DESC GetDefaultBlendStateDesc() {
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

const char* ShaderCode =
"struct PSInput { float4 pos : SV_POSITION; float4 uvs : TEXCOORD; };\n"
"PSInput VSMain(float4 position : POSITION, float4 uvs : TEXCOORD) {\n"
"\tPSInput res;\n"
"\tres.pos = position;\n"
"\tres.uvs = uvs;\n"
"\treturn res;\n"
"}\n"
"Texture2D MyTex;\n"
"SamplerState MyTexSampler;\n"
"float4 PSMain(PSInput input) : SV_TARGET {\n"
"\treturn MyTex.SampleLevel(MyTexSampler, input.uvs.xy, 0);\n"
"}";


void SetupPersistentOnTextureCompressionFuzzer(D3DTextureCompressionFuzzingPersistentState* Persist, ID3D12Device* Device)
{
	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HRESULT hr = Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&Persist->CmdQueue));
	ASSERT(SUCCEEDED(hr));

	hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Persist->ExecFence));
	ASSERT(SUCCEEDED(hr));

	{

		D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
		RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		std::vector<D3D12_ROOT_PARAMETER> RootParams;
		std::vector<D3D12_STATIC_SAMPLER_DESC> RootStaticSamplers;
		std::vector<D3D12_DESCRIPTOR_RANGE> DescriptorRanges;

		{
			DescriptorRanges.resize(1);

			D3D12_DESCRIPTOR_RANGE& pDescriptorRange = DescriptorRanges[0];
			pDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			pDescriptorRange.BaseShaderRegister = 0;
			pDescriptorRange.NumDescriptors = 1;
			pDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		{
			RootParams.resize(1);

			RootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			RootParams[0].DescriptorTable.NumDescriptorRanges = 1;
			RootParams[0].DescriptorTable.pDescriptorRanges = &DescriptorRanges[0];
			RootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}

		{
			RootStaticSamplers.resize(1);

			RootStaticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			RootStaticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			RootStaticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			RootStaticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			RootStaticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			RootStaticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
			RootStaticSamplers[0].ShaderRegister = 0;
			RootStaticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}

		RootSigDesc.NumParameters = RootParams.size();
		RootSigDesc.pParameters = RootParams.data();

		RootSigDesc.NumStaticSamplers = RootStaticSamplers.size();
		RootSigDesc.pStaticSamplers = RootStaticSamplers.data();

		ID3DBlob* RootSigBlob = nullptr;
		ID3DBlob* RootSigErrorBlob = nullptr;

		HRESULT hr = D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootSigBlob, &RootSigErrorBlob);
		ASSERT(SUCCEEDED(hr));
		hr = Device->CreateRootSignature(0, RootSigBlob->GetBufferPointer(), RootSigBlob->GetBufferSize(), IID_PPV_ARGS(&Persist->BlitRootSig));
		ASSERT(SUCCEEDED(hr));
	}


	{

		ShaderMetadata meta;
		ID3DBlob* VSBlob = CompileShaderCode(ShaderCode, D3DShaderType::Vertex, "<debug_vs_file>", "VSMain", &meta);
		ID3DBlob* PSBlob = CompileShaderCode(ShaderCode, D3DShaderType::Pixel, "<debug_ps_file>", "PSMain", &meta);

		D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_SHADER_BYTECODE VertexShaderByteCode;
		VertexShaderByteCode.pShaderBytecode = VSBlob->GetBufferPointer();
		VertexShaderByteCode.BytecodeLength = VSBlob->GetBufferSize();

		D3D12_SHADER_BYTECODE PixelShaderByteCode;
		PixelShaderByteCode.pShaderBytecode = PSBlob->GetBufferPointer();
		PixelShaderByteCode.BytecodeLength = PSBlob->GetBufferSize();

		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.InputLayout = { InputElementDesc, 2 };
		PSODesc.pRootSignature = Persist->BlitRootSig;
		PSODesc.VS = VertexShaderByteCode;
		PSODesc.PS = PixelShaderByteCode;
		PSODesc.RasterizerState = GetDefaultRasterizerDesc();
		PSODesc.BlendState = GetDefaultBlendStateDesc();
		PSODesc.DepthStencilState.DepthEnable = FALSE;
		PSODesc.DepthStencilState.StencilEnable = FALSE;
		PSODesc.SampleMask = UINT_MAX;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		PSODesc.SampleDesc.Count = 1;

		// Compile PSO
		Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&Persist->BlitPSO));

		ASSERT(Persist->BlitPSO != nullptr);
	}

	{
		const int32 VertCount = 6;

		HRESULT hr;

		D3D12_HEAP_PROPERTIES HeapProps = {};
		HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC VertResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(VertCount * 4 * sizeof(float));
		hr = Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &VertResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Persist->VertBufferPos));
		ASSERT(SUCCEEDED(hr));
		hr = Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &VertResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Persist->VertBufferUVs));
		ASSERT(SUCCEEDED(hr));

		{
			void* pVertexData = nullptr;
			D3D12_RANGE readRange = {};
			HRESULT hr = Persist->VertBufferPos->Map(0, &readRange, &pVertexData);
			ASSERT(SUCCEEDED(hr));

			float* VertData = (float*)pVertexData;

			VertData[0] = -1.0f; VertData[1] = -1.0f; VertData[2]  = 1.0f; VertData[3]  = 1.0f;
			VertData[4] =  1.0f; VertData[5] = -1.0f; VertData[6]  = 1.0f; VertData[7]  = 1.0f;
			VertData[8] =  1.0f; VertData[9] =  1.0f; VertData[10] = 1.0f; VertData[11] = 1.0f;

			VertData[12] = -1.0f; VertData[13] = -1.0f; VertData[14] = 1.0f; VertData[15] = 1.0f;
			VertData[16] = -1.0f; VertData[17] =  1.0f; VertData[18] = 1.0f; VertData[19] = 1.0f;
			VertData[20] =  1.0f; VertData[21] =  1.0f; VertData[22] = 1.0f; VertData[23] = 1.0f;

			Persist->VertBufferPos->Unmap(0, nullptr);
		}

		{
			void* pVertexData = nullptr;
			D3D12_RANGE readRange = {};
			HRESULT hr = Persist->VertBufferUVs->Map(0, &readRange, &pVertexData);
			ASSERT(SUCCEEDED(hr));

			float* VertData = (float*)pVertexData;

			VertData[0] = 0.0f; VertData[1] = 0.0f; VertData[2]  = 1.0f; VertData[3]  = 1.0f;
			VertData[4] = 1.0f; VertData[5] = 0.0f; VertData[6]  = 1.0f; VertData[7]  = 1.0f;
			VertData[8] = 1.0f; VertData[9] = 1.0f; VertData[10] = 1.0f; VertData[11] = 1.0f;

			VertData[12] = 0.0f; VertData[13] = 0.0f; VertData[14] = 1.0f; VertData[15] = 1.0f;
			VertData[16] = 0.0f; VertData[17] = 1.0f; VertData[18] = 1.0f; VertData[19] = 1.0f;
			VertData[20] = 1.0f; VertData[21] = 1.0f; VertData[22] = 1.0f; VertData[23] = 1.0f;

			Persist->VertBufferUVs->Unmap(0, nullptr);
		}
	}
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
		const DXGI_FORMAT UncompressedTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		// TODO?????
		const int32 BufferSize = 4096 + 1024;


		std::vector<ID3D12DescriptorHeap*> DescriptorHeaps;

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

			SetRandomBytes(Fuzzer, pPixelData, BufferSize);

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

			// TODO: Make SRV
		}

		ID3D12Resource* RenderTextureResource = nullptr;
		{
			D3D12_HEAP_PROPERTIES HeapProps = {};
			HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
			// Changing this format to CompressedTextureFormat does not validation issue...
			// So even though the validation implied you could go from DXGI_FORMAT_BC3_UNORM to DXGI_FORMAT_R32G32B32A32_FLOAT with a copy...it doesn't seem to be supported
			D3D12_RESOURCE_DESC RenderTextureResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(UncompressedTextureFormat, TextureWidth, TextureHeight, 1, 1);
			RenderTextureResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			hr = Fuzzer->D3DDevice->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &RenderTextureResourceDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&RenderTextureResource));
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
			Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			CommandList->ResourceBarrier(1, &Barrier);
		}

		{
			CommandList->SetPipelineState(Fuzzer->Persistent->BlitPSO);
			CommandList->SetGraphicsRootSignature(Fuzzer->Persistent->BlitRootSig);
		}

		{
			D3D12_VIEWPORT Viewport = {};
			Viewport.MinDepth = 0;
			Viewport.MaxDepth = 1;
			Viewport.TopLeftX = 0;
			Viewport.TopLeftY = 0;
			Viewport.Width = TextureWidth;
			Viewport.Height = TextureHeight;
			CommandList->RSSetViewports(1, &Viewport);

			D3D12_RECT ScissorRect = {};
			ScissorRect.left = 0;
			ScissorRect.right = TextureWidth;
			ScissorRect.top = 0;
			ScissorRect.bottom = TextureHeight;
			CommandList->RSSetScissorRects(1, &ScissorRect);

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
			DescriptorHeapDesc.NumDescriptors = 1;
			DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

			ID3D12DescriptorHeap* DescriptorHeap = nullptr;
			Fuzzer->D3DDevice->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			Fuzzer->D3DDevice->CreateRenderTargetView(RenderTextureResource, nullptr, rtvHandle);

			CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

			DescriptorHeaps.push_back(DescriptorHeap);
		}

		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = CompressedTextureFormat;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;

			ID3D12DescriptorHeap* TextureSRVHeap = nullptr;

			// TODO: Descriptor heap needs to go somewhere, maybe on texture?
			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
			srvHeapDesc.NumDescriptors = 1;
			srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			hr = Fuzzer->D3DDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&TextureSRVHeap));

			ASSERT(SUCCEEDED(hr));

			Fuzzer->D3DDevice->CreateShaderResourceView(TextureResource, &srvDesc, TextureSRVHeap->GetCPUDescriptorHandleForHeapStart());

			CommandList->SetDescriptorHeaps(1, &TextureSRVHeap);
			CommandList->SetGraphicsRootDescriptorTable(0, TextureSRVHeap->GetGPUDescriptorHandleForHeapStart());

			DescriptorHeaps.push_back(TextureSRVHeap);
		}

		const int32 VertexCount = 6;

		{
			D3D12_VERTEX_BUFFER_VIEW vtbView = {};
			vtbView.BufferLocation = Fuzzer->Persistent->VertBufferPos->GetGPUVirtualAddress();
			vtbView.SizeInBytes = VertexCount * 16;
			vtbView.StrideInBytes = 16;

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(0, 1, &vtbView);
		}

		{
			D3D12_VERTEX_BUFFER_VIEW vtbView = {};
			vtbView.BufferLocation = Fuzzer->Persistent->VertBufferUVs->GetGPUVirtualAddress();
			vtbView.SizeInBytes = VertexCount * 16;
			vtbView.StrideInBytes = 16;

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(1, 1, &vtbView);
		}

#if defined(WITH_PIPELINE_STATS_QUERY)
		ID3D12QueryHeap* QueryHeap = nullptr;

		D3D12_QUERY_HEAP_DESC QueryHeapDesc = {};
		QueryHeapDesc.Count = 1;
		QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
		Fuzzer->D3DDevice->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(&QueryHeap));

		ASSERT(QueryHeap != nullptr);

		CommandList->BeginQuery(QueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
#endif

		CommandList->DrawInstanced(VertexCount, 1, 0, 0);

#if defined(WITH_PIPELINE_STATS_QUERY)
		CommandList->EndQuery(QueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);

		ID3D12Resource* DestBuffer = nullptr;
		{
			D3D12_HEAP_PROPERTIES HeapProps = {};
			HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
			HeapProps.CreationNodeMask = 1;
			HeapProps.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC QueryBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));

			Fuzzer->D3DDevice->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &QueryBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&DestBuffer));
		}

		CommandList->ResolveQueryData(QueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, DestBuffer, 0);
#endif

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

#if defined(WITH_PIPELINE_STATS_QUERY)
		D3D12_RANGE PipelineRange;
		PipelineRange.Begin = 0;
		PipelineRange.End = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
		D3D12_QUERY_DATA_PIPELINE_STATISTICS* StatisticsPtr = nullptr;
		DestBuffer->Map(0, &PipelineRange, reinterpret_cast<void**>(&StatisticsPtr));

		D3D12_QUERY_DATA_PIPELINE_STATISTICS PipelineStats = *StatisticsPtr;

		LOG("IA Verts: %llu VS calls: %llu PS calls: %llu", PipelineStats.IAVertices, PipelineStats.VSInvocations, PipelineStats.PSInvocations);

		DestBuffer->Release();
#endif

		CommandList->Release();
		CommandAllocator->Reset();
		CommandAllocator->Release();

		for (auto* DescriptorHeap : DescriptorHeaps)
		{
			DescriptorHeap->Release();
		}

		UploadBuffer->Release();
		TextureResource->Release();
		RenderTextureResource->Release();
	}
}


