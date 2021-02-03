//
// This is a test app for a simple D3D12 app that should work, but gets a DXGI_ERROR_DEVICE_HUNG error on Intel. It works on Microsoft's WARP and my Nvidia device
//
// The timeline of events for the repro as I understand it :
// -Create resources for frame 0
// - Draw frame 0
// - Wait on the CPU for the GPU to finish frame 0, so that no outstanding command lists refer to the resources created
// - Begin creating resources for frame 1
// - Delete one of the resources created for frame 0 (all other resources are will leak since we don't care about them)
// - Draw frame 1
// - Wait on the CPU for the GPU to finish frame 1
// - Begin creating resources for frame 2
// - Observe crash
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
typedef uint64_t uint64;

#define ASSERT assert


#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern const char* VertexShaderCode;
extern const char* PixelShaderCode;

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
	// However the debug layer is what gives us DXGI_ERROR_DEVICE_HUNG
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

			// Get the Intel integrated adapter
			// Tested on "Intel(R) HD Graphics 630", where AdapterDesc.DeviceId == 0x591B
			if (AdapterDesc.VendorId == 0x8086) {
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


	ID3DBlob* VSByteCode = nullptr;
	ID3DBlob* PSByteCode = nullptr;
	ID3DBlob* ErrorMsg = nullptr;
	UINT CompilerFlags = 0;
	hr = D3DCompile(VertexShaderCode, strlen(VertexShaderCode), "<VS_SOURCE>", nullptr, nullptr, "Main", "vs_5_0", CompilerFlags, 0, &VSByteCode, &ErrorMsg);
	if (ErrorMsg)
	{
		const char* ErrMsgStr = (const char*)ErrorMsg->GetBufferPointer();
		OutputDebugStringA(ErrMsgStr);
	}
	ASSERT(SUCCEEDED(hr));

	hr = D3DCompile(PixelShaderCode, strlen(PixelShaderCode), "<PS_SOURCE>", nullptr, nullptr, "Main", "ps_5_0", CompilerFlags, 0, &PSByteCode, &ErrorMsg);
	if (ErrorMsg)
	{
		const char* ErrMsgStr = (const char*)ErrorMsg->GetBufferPointer();
		OutputDebugStringA(ErrMsgStr);
	}
	ASSERT(SUCCEEDED(hr));


	D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
	RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	std::vector<D3D12_ROOT_PARAMETER> RootParams;

	RootParams.resize(1);
	{
		RootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		RootParams[0].Descriptor.RegisterSpace = 0;
		RootParams[0].Descriptor.ShaderRegister = 0;
		RootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	}

	RootSigDesc.NumParameters = RootParams.size();
	RootSigDesc.pParameters = RootParams.data();

	RootSigDesc.NumStaticSamplers = 0;
	RootSigDesc.pStaticSamplers = nullptr;

	ID3DBlob* RootSigBlob = nullptr;
	ID3DBlob* RootSigErrorBlob = nullptr;

	hr = D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootSigBlob, &RootSigErrorBlob);
	ASSERT(SUCCEEDED(hr));

	ID3D12RootSignature* RootSig = nullptr;
	Device->CreateRootSignature(0, RootSigBlob->GetBufferPointer(), RootSigBlob->GetBufferSize(), IID_PPV_ARGS(&RootSig));


	ID3D12PipelineState* PSO = nullptr;
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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

	hr = Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO));
	ASSERT(SUCCEEDED(hr));
	ASSERT(PSO != nullptr);

	// If the command queue is separate for each frame, the bug does not repro. If it's shared then it does repro
	ID3D12CommandQueue* CommandQueue = nullptr;

	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&CommandQueue));
	ASSERT(SUCCEEDED(hr));

	ID3D12Fence* ExecFence = nullptr;
	hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ExecFence));
	ASSERT(SUCCEEDED(hr));

	ID3D12Resource* ResourceToDelete = nullptr;

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

	D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
	DescriptorHeapDesc.NumDescriptors = 1;
	DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	ID3D12DescriptorHeap* DescriptorHeap = nullptr;
	Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	Device->CreateRenderTargetView(BackBufferResource, nullptr, rtvHandle);

	// At the end of the second frame is when the device removal occurs, so the start of the third frame is when problems start
	for (int32 Frame = 0; Frame < 3; Frame++)
	{
		ID3D12CommandAllocator* CommandAllocator = nullptr;
		hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator));
		ASSERT(SUCCEEDED(hr));

		ID3D12GraphicsCommandList* CommandList = nullptr;
		hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, 0, IID_PPV_ARGS(&CommandList));
		ASSERT(SUCCEEDED(hr));

		CommandList->SetPipelineState(PSO);
		CommandList->SetGraphicsRootSignature(RootSig);

		CommandList->RSSetViewports(1, &Viewport);
		CommandList->RSSetScissorRects(1, &ScissorRect);
		CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		{
			// The actual size needed is 16, but just in case allocate extra space so that we are sure we don't overread in the shader
			const int32 CBVSize = 256;
			D3D12_RESOURCE_DESC CBVResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(CBVSize);

			ID3D12Resource* CBVResource = nullptr;
			{
				D3D12_HEAP_PROPERTIES Props = {};
				Props.Type = D3D12_HEAP_TYPE_UPLOAD;
				const D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
				hr = Device->CreateCommittedResource(&Props, D3D12_HEAP_FLAG_NONE, &CBVResourceDesc, InitialState, nullptr, IID_PPV_ARGS(&CBVResource));
			}

			// Zero it out, though the issue repros whether or not we do this
			{
				void* pVertData = nullptr;
				D3D12_RANGE readRange = {};
				HRESULT hr = CBVResource->Map(0, &readRange, &pVertData);
				ASSERT(SUCCEEDED(hr));

				memset(pVertData, 0, CBVSize);

				CBVResource->Unmap(0, nullptr);
			}

			CommandList->SetGraphicsRootConstantBufferView(0, CBVResource->GetGPUVirtualAddress());

			if (Frame == 0)
			{
				ResourceToDelete = CBVResource;
			}
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

			// This is the earliest place we can put the deletion. It can occur anywhere from here to submitting the second command list (Frame == 1)
			// If we delete the resource from frame 0 before we create the Vertex buffer on frame 1, then the issue does not repro
			if (Frame == 1)
			{
				ASSERT(ResourceToDelete != nullptr);

				// Commenting this out allows the app to finish all 3 frames (or possibly more if you change it)
				ResourceToDelete->Release();

				ResourceToDelete = nullptr;
			}

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

				pFloatData[8] = 0.1f;
				pFloatData[9] = 0.1f;
				pFloatData[10] = 1.0f;
				pFloatData[11] = 1.0f;
			}

			VertexBufferRes->Unmap(0, nullptr);

			D3D12_VERTEX_BUFFER_VIEW vtbView = {};
			vtbView.BufferLocation = VertexBufferRes->GetGPUVirtualAddress();
			vtbView.SizeInBytes = BufferSize;
			vtbView.StrideInBytes = 16;

			const int32 VBSlot = 0;

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(VBSlot, 1, &vtbView);
		}

		CommandList->DrawInstanced(VertexCount, 1, 0, 0);

		CommandList->Close();

		ID3D12CommandList* CommandLists[] = { CommandList };
		CommandQueue->ExecuteCommandLists(1, CommandLists);

		// Signal 1,2,3...etc. because the fence's initial value is 0
		uint64 ValueSignaled = (uint64)Frame + 1LLU;
		CommandQueue->Signal(ExecFence, ValueSignaled);

		// Synchronously wait. Since the resource we will delete is created and only used in frame 0,
		// it should be safe to delete after this finishes and we go into the next loop
		HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		ExecFence->SetEventOnCompletion(ValueSignaled, hEvent);
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);

		// We can also perform a sleep to make sure the pending command list is done,
		// this still repros the bug
		//Sleep(1000);
	}

	return 0;
}


const char* VertexShaderCode =
"cbuffer root_cbv_27109_143_30000 {\n"
"\tfloat4 root_cbv_27109_143_30000_var0;\n"
"};\n"
"struct VSInput {\n"
"\tfloat4 iaparam_22085_39525_28570 : POSITION;\n"
"};\n"
"\n"
"struct PSInput {\n"
"\tfloat4 param_1534_18439_23636 : SV_POSITION;\n"
"};\n"
"\n"
"PSInput Main(VSInput input)\n"
"{\n"
"\tPSInput result;\n"
"\t\n"
"\tresult.param_1534_18439_23636 = input.iaparam_22085_39525_28570\n"
"\t\t+ root_cbv_27109_143_30000_var0\n"
";\n"
"\treturn result;\n"
"}\n"
;

const char* PixelShaderCode =
"struct PSInput {\n"
"\tfloat4 param_1534_18439_23636 : SV_POSITION;\n"
"};\n"
"\n"
"float4 Main(PSInput input) : SV_TARGET\n"
"{\n"
"\treturn float4(1,1,1,1);"
"}\n"
;





