
#include <stdio.h>

#include <stdint.h>

#include <vector>
#include <thread>
#include <limits>
#include <mutex>

#include <Windows.h>

#include <assert.h>

#include <d3d12.h>

#include <d3dcompiler.h>

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

#define ASSERT assert


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


inline void ReadStringFromFile(const char* Filename, char** OutStr)
{
	FILE* f = NULL;
	fopen_s(&f, Filename, "rb");

	fseek(f, 0, SEEK_END);
	int32 TotalSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* FileData = (char*)malloc(TotalSize + 1);

	fread(FileData, 1, TotalSize, f);

	FileData[TotalSize] = '\0';

	*OutStr = FileData;

	fclose(f);
}


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

	// Turning off conservative rasterization will stop the bug from repro'ing
	Desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
	//Desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

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

int main(int argc, char** argv) {

	// Repros with or without debug layer,
	// however the debug layer doesn't complain if it's enabled
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
	ASSERT(SUCCEEDED(D3D12CreateDevice(ChosenAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device))));


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


	ID3D12PipelineState* PSO = nullptr;
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

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
	PSODesc.InputLayout = { inputElementDescs, (sizeof(inputElementDescs) / sizeof(inputElementDescs[0])) };
	PSODesc.pRootSignature = RootSig;
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

	Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO));

	ID3D12CommandQueue* CommandQueue = nullptr;

	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ASSERT(SUCCEEDED(Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&CommandQueue))));

	{

		ID3D12CommandAllocator* CommandAllocator = nullptr;
		ASSERT(SUCCEEDED(hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator))));

		ID3D12GraphicsCommandList* CommandList = nullptr;
		ASSERT(SUCCEEDED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, 0, IID_PPV_ARGS(&CommandList))));

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

		CommandList->SetPipelineState(PSO);
		CommandList->SetGraphicsRootSignature(RootSig);

		{
			D3D12_VIEWPORT Viewport = {};
			Viewport.MinDepth = 0;
			Viewport.MaxDepth = 1;
			Viewport.TopLeftX = 0;
			Viewport.TopLeftY = 0;
			Viewport.Width = RTWidth;
			Viewport.Height = RTHeight;
			CommandList->RSSetViewports(1, &Viewport);

			D3D12_RECT ScissorRect = {};
			ScissorRect.left = 0;
			ScissorRect.right = RTWidth;
			ScissorRect.top = 0;
			ScissorRect.bottom = RTHeight;
			CommandList->RSSetScissorRects(1, &ScissorRect);

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
			DescriptorHeapDesc.NumDescriptors = 1;
			DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

			ID3D12DescriptorHeap* DescriptorHeap = nullptr;
			Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			Device->CreateRenderTargetView(BackBufferResource, nullptr, rtvHandle);

			CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		}

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

			{
				float* pFloatData = (float*)pVertData;
				pFloatData[0] = 1.0;
				pFloatData[1] = 1.0;
				pFloatData[2] = 1.0f;
				pFloatData[3] = 1.0f;

				pFloatData[4] = 0.1f;
				pFloatData[5] = 0.1f;
				pFloatData[6] = 1.0f;
				pFloatData[7] = 1.0f;

				pFloatData[8]  = 0.1f;
				pFloatData[9]  = 0.1f;
				pFloatData[10] = 1.0f;
				pFloatData[11] = 1.0f;
			}

			VertexBufferRes->Unmap(0, nullptr);

			D3D12_VERTEX_BUFFER_VIEW vtbView = {};
			vtbView.BufferLocation = VertexBufferRes->GetGPUVirtualAddress();
			vtbView.SizeInBytes = BufferSize;
			vtbView.StrideInBytes = 16;

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(0, 1, &vtbView);
		}
		
		CommandList->DrawInstanced(VertexCount, 1, 0, 0);

		CommandList->Close();

		ID3D12CommandList* CommandLists[] = { CommandList };
		CommandQueue->ExecuteCommandLists(1, CommandLists);

		// Since executing a command list will be asynchronous on another thread,
		// wait a second so that the async work can hit the bug
		Sleep(1000);
	}


	return 0;
}