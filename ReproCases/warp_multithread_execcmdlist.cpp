//
// Repro case for multi-threaded WARP access violation
// Does not repro every time, but I see it more than half the time
// Tested on my machine that is running Windows 10 Version 20H2, OS Build 19042.746
// If multiple threads call ExecuteCommandLists() at the same time, then later on an access violation occurs inside WARP
// This occurs even if the calls are to different command queues
// It can be mitigated by using a mutex to prevent simultaneous calls to ExecuteCommandLists
// It does not occur when running with debug validation, however if validation is turned on it does not throw any errors
// It seems to require some amount of resource churn
// It seems to require some variation in the PSO's, and blending enabled
//

#include <stdio.h>

#include <stdint.h>

#include <random>
#include <vector>
#include <thread>
#include <limits>
#include <mutex>

#include <Windows.h>

#include <assert.h>

#include <d3d12.h>

#include <d3dcompiler.h>

#include <dxgi1_2.h>

#define ARRAY_COUNTOF(x) (sizeof(x) / sizeof((x)[0]))

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

#define ASSERT(x) do { if (!(x)) { __debugbreak(); } } while(0)


#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "d3dcompiler.lib")

const char* VertexShaderCode =
"struct PSInput { float4 pos : SV_POSITION; };\n"
"PSInput VSMain(float4 position : POSITION) {\n"
"	PSInput res;\n"
"	res.pos = position;\n"
"	return res;"
"}";


const char* PixelShaderCode =
"struct PSInput { float4 pos : SV_POSITION; };\n"
"float4 PSMain(PSInput input) : SV_TARGET {\n"
"	return float4(1.0, 1.0, 1.0, 1.0);\n"
"}";


D3D12_RASTERIZER_DESC GetDefaultRasterizerDesc() {
	D3D12_RASTERIZER_DESC Desc = {};
	Desc.FillMode = D3D12_FILL_MODE_SOLID;
	Desc.CullMode = D3D12_CULL_MODE_NONE;
	Desc.FrontCounterClockwise = FALSE;
	Desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	Desc.DepthBiasClamp = 0;
	Desc.SlopeScaledDepthBias = 0;
	Desc.DepthClipEnable = TRUE;
	Desc.MultisampleEnable = FALSE;
	Desc.AntialiasedLineEnable = FALSE;
	Desc.ForcedSampleCount = 0;
	Desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	return Desc;
}

D3D12_BLEND_DESC GetDefaultBlendStateDesc(std::mt19937_64* RNG) {
	D3D12_BLEND_DESC Desc = {};
	Desc.AlphaToCoverageEnable = FALSE;
	Desc.IndependentBlendEnable = FALSE;

	D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
	{
		FALSE,FALSE,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL,
	};

	const static D3D12_BLEND BlendValuesSrcCol[] = {
		D3D12_BLEND_SRC_COLOR,
		D3D12_BLEND_INV_SRC_COLOR,
		D3D12_BLEND_BLEND_FACTOR,
		D3D12_BLEND_INV_BLEND_FACTOR,
	};

	DefaultRenderTargetBlendDesc.BlendEnable = TRUE;
	DefaultRenderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_SUBTRACT;
	DefaultRenderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_SUBTRACT;

	std::uniform_int_distribution<int> BlendValues(0, ARRAY_COUNTOF(BlendValuesSrcCol) - 1);

	DefaultRenderTargetBlendDesc.SrcBlend = BlendValuesSrcCol[BlendValues(*RNG)];
	DefaultRenderTargetBlendDesc.DestBlend = D3D12_BLEND_DEST_ALPHA;

	DefaultRenderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	DefaultRenderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;


	DefaultRenderTargetBlendDesc.LogicOpEnable = FALSE;
	DefaultRenderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		Desc.RenderTarget[i] = DefaultRenderTargetBlendDesc;
	}

	return Desc;
}

std::mutex ExecCmdListMutex;


int main(int argc, char** argv) {

	// Only repros without the debug layer enabled,
	// however the debug layer doesn't complain if it's enabled
	// But if it is enabled, the bug does not repro
	//ID3D12Debug1* D3D12DebugLayer = nullptr;
	//D3D12GetDebugInterface(IID_PPV_ARGS(&D3D12DebugLayer));
	//D3D12DebugLayer->EnableDebugLayer();

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

			// Get the WARP adapter
			if (AdapterDesc.VendorId == 0x1414 && AdapterDesc.DeviceId == 0x8C) {
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

	std::vector<std::thread> CmdThreads;

	// I have seen it repro with as few as 2 threads, but 6+ tends to be more reliable
	const int32 NumThreads = 6;
	const int32 NumIterations = 1000;

	for (int32 TIdx = 0; TIdx < NumThreads; TIdx++)
	{
		CmdThreads.emplace_back([Device = Device, NumIterations = NumIterations]() {

			HRESULT hr;

			ID3DBlob* VSByteCode = nullptr;
			ID3DBlob* PSByteCode = nullptr;
			ID3DBlob* ErrorMsg = nullptr;
			UINT CompilerFlags = 0;
			hr = D3DCompile(VertexShaderCode, strlen(VertexShaderCode), "<VS_SOURCE>", nullptr, nullptr, "VSMain", "vs_5_0", CompilerFlags, 0, &VSByteCode, &ErrorMsg);
			if (ErrorMsg)
			{
				const char* ErrMsgStr = (const char*)ErrorMsg->GetBufferPointer();
				OutputDebugStringA(ErrMsgStr);
			}
			ASSERT(SUCCEEDED(hr));

			hr = D3DCompile(PixelShaderCode, strlen(PixelShaderCode), "<PS_SOURCE>", nullptr, nullptr, "PSMain", "ps_5_0", CompilerFlags, 0, &PSByteCode, &ErrorMsg);
			if (ErrorMsg)
			{
				const char* ErrMsgStr = (const char*)ErrorMsg->GetBufferPointer();
				OutputDebugStringA(ErrMsgStr);
			}
			ASSERT(SUCCEEDED(hr));


			D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
			RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

			ID3DBlob* RootSigBlob = nullptr;
			ID3DBlob* RootSigErrorBlob = nullptr;

			hr = D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootSigBlob, &RootSigErrorBlob);
			ASSERT(SUCCEEDED(hr));

			ID3D12RootSignature* RootSig = nullptr;
			Device->CreateRootSignature(0, RootSigBlob->GetBufferPointer(), RootSigBlob->GetBufferSize(), IID_PPV_ARGS(&RootSig));

			D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			};

			D3D12_SHADER_BYTECODE VertexShaderByteCode;
			VertexShaderByteCode.pShaderBytecode = VSByteCode->GetBufferPointer();
			VertexShaderByteCode.BytecodeLength = VSByteCode->GetBufferSize();

			D3D12_SHADER_BYTECODE PixelShaderByteCode;
			PixelShaderByteCode.pShaderBytecode = PSByteCode->GetBufferPointer();
			PixelShaderByteCode.BytecodeLength = PSByteCode->GetBufferSize();

			ID3D12CommandQueue* CommandQueue = nullptr;
			D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
			CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			hr = Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&CommandQueue));
			ASSERT(SUCCEEDED(hr));

			const int32 RTWidth = 128;
			const int32 RTHeight = 128;

			ID3D12Resource* BackBufferResource = nullptr;
			{
				D3D12_RESOURCE_DESC BackBufferDesc = {};
				BackBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				BackBufferDesc.Width = RTWidth;
				BackBufferDesc.Height = RTHeight;
				BackBufferDesc.DepthOrArraySize = 1;
				BackBufferDesc.SampleDesc.Count = 1;
				BackBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
				BackBufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

				D3D12_HEAP_PROPERTIES Props = {};
				Props.Type = D3D12_HEAP_TYPE_DEFAULT;
				const D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
				hr = Device->CreateCommittedResource(&Props, D3D12_HEAP_FLAG_NONE, &BackBufferDesc, InitialState, nullptr, IID_PPV_ARGS(&BackBufferResource));
				ASSERT(SUCCEEDED(hr));
			}

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
			DescriptorHeapDesc.NumDescriptors = 1;
			DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

			ID3D12DescriptorHeap* DescriptorHeap = nullptr;
			hr = Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));
			ASSERT(SUCCEEDED(hr));

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			Device->CreateRenderTargetView(BackBufferResource, nullptr, rtvHandle);

			ID3D12CommandAllocator* CommandAllocator = nullptr;
			hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator));
			ASSERT(SUCCEEDED(hr));

			D3D12_VERTEX_BUFFER_VIEW vtbView = {};

			int32 VertexCount = 3;
			{
				int32 BufferSize = VertexCount * 16;
				D3D12_RESOURCE_DESC VertResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);

				ID3D12Resource* VertexBufferRes = nullptr;

				D3D12_HEAP_PROPERTIES Props = {};
				Props.Type = D3D12_HEAP_TYPE_UPLOAD;
				const D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
				hr = Device->CreateCommittedResource(&Props, D3D12_HEAP_FLAG_NONE, &VertResourceDesc, InitialState, nullptr, IID_PPV_ARGS(&VertexBufferRes));
				ASSERT(SUCCEEDED(hr));

				void* pVertData = nullptr;
				D3D12_RANGE readRange = {};
				HRESULT hr = VertexBufferRes->Map(0, &readRange, &pVertData);
				ASSERT(SUCCEEDED(hr));

				float* pFloatData = (float*)pVertData;
				pFloatData[0] = 0.1f;
				pFloatData[1] = 0.1f;
				pFloatData[2] = 1.0f;
				pFloatData[3] = 1.0f;

				pFloatData[4] = 0.5f;
				pFloatData[5] = 0.5f;
				pFloatData[6] = 1.0f;
				pFloatData[7] = 1.0f;

				pFloatData[8] = 0.5f;
				pFloatData[9] = 0.1f;
				pFloatData[10] = 1.0f;
				pFloatData[11] = 1.0f;

				VertexBufferRes->Unmap(0, nullptr);

				vtbView.BufferLocation = VertexBufferRes->GetGPUVirtualAddress();
				vtbView.SizeInBytes = BufferSize;
				vtbView.StrideInBytes = 16;
			}

			D3D12_VIEWPORT Viewport = {};
			Viewport.MinDepth = 0;
			Viewport.MaxDepth = 1;
			Viewport.TopLeftX = 0;
			Viewport.TopLeftY = 0;
			Viewport.Width = RTWidth;
			Viewport.Height = RTHeight;

			D3D12_RECT ScissorRect = {};
			ScissorRect.left = 0;
			ScissorRect.right = RTWidth;
			ScissorRect.top = 0;
			ScissorRect.bottom = RTHeight;

			// We need to randomise some aspects of the blend state for PSO creation,
			// however the initial seed isn't too important (and can be the same across threads)
			std::mt19937_64 RNGState;
			RNGState.seed(0);

			D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
			PSODesc.InputLayout = { inputElementDescs, (sizeof(inputElementDescs) / sizeof(inputElementDescs[0])) };
			PSODesc.pRootSignature = RootSig;
			PSODesc.VS = VertexShaderByteCode;
			PSODesc.PS = PixelShaderByteCode;
			PSODesc.RasterizerState = GetDefaultRasterizerDesc();
			PSODesc.DepthStencilState.DepthEnable = FALSE;
			PSODesc.DepthStencilState.StencilEnable = FALSE;
			PSODesc.SampleMask = UINT_MAX;
			PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			PSODesc.NumRenderTargets = 1;
			PSODesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
			PSODesc.SampleDesc.Count = 1;

			for (int32 i = 0; i < NumIterations; i++)
			{
				// Make a slightly different PSO. For some reason, using the same PSO description does not repro nearly as much
				PSODesc.BlendState = GetDefaultBlendStateDesc(&RNGState);

				ID3D12PipelineState* PSO = nullptr;
				Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO));

				ID3D12GraphicsCommandList* CommandList = nullptr;
				hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, 0, IID_PPV_ARGS(&CommandList));
				ASSERT(SUCCEEDED(hr));

				CommandList->SetPipelineState(PSO);
				CommandList->SetGraphicsRootSignature(RootSig);

				CommandList->RSSetViewports(1, &Viewport);
				CommandList->RSSetScissorRects(1, &ScissorRect);
				CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

				CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				CommandList->IASetVertexBuffers(0, 1, &vtbView);

				CommandList->DrawInstanced(VertexCount, 1, 0, 0);

				CommandList->Close();

				ID3D12CommandList* CommandLists[] = { CommandList };
				{
					// Uncomment this to avoid parallel calls to ExecuteCommandLists and prevent the bug from repro'ing
					//std::lock_guard<std::mutex> Lock(ExecCmdListMutex);
					CommandQueue->ExecuteCommandLists(1, CommandLists);
				}
			}
		});
	}

	for (auto& t : CmdThreads)
	{
		t.join();
	}

	return 0;
}