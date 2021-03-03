
#include <stdio.h>

#include <stdint.h>

#include <vector>
#include <thread>
#include <mutex>

#include <Windows.h>

#include <assert.h>

#include <d3d11.h>
#include <d3d12.h>

#include <d3dcompiler.h>

//#include "d3dx12.h"

#include "basics.h"

#include <dxgi1_2.h>

#include "d3d12_ext.h"

#include "fuzz_d3d11_video.h"
#include "fuzz_reserved_resources.h"
#include "fuzz_texture_compression.h"
#include "fuzz_shader_compiler.h"
#include "fuzz_dxbc.h"
#include "d3d_resource_mgr.h"

#include "re_dxbc.h"

#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "d3dcompiler.lib")

int WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCommand) {

	ID3D12Debug1* D3D12DebugLayer = nullptr;
	D3D12GetDebugInterface(IID_PPV_ARGS(&D3D12DebugLayer));
	D3D12DebugLayer->EnableDebugLayer();
	//D3D12DebugLayer->SetEnableGPUBasedValidation(true);
	//D3D12DebugLayer->SetEnableSynchronizedCommandQueueValidation(true);

	IDXGIFactory2* DXGIFactory = nullptr;

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&DXGIFactory));
	ASSERT(SUCCEEDED(hr));

	int ChosenAdapterIndex = 1;
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

	{
		DXGI_ADAPTER_DESC ChosenAdapterDesc = {};
		ChosenAdapter->GetDesc(&ChosenAdapterDesc);

		OutputDebugStringW(L"\nChosen Adapter: ");
		OutputDebugStringW(ChosenAdapterDesc.Description);
		OutputDebugStringW(L"\n");
	}


	if (0)
	{
		ID3D11Device* Device = nullptr;
		ID3D11DeviceContext* DeviceContext = nullptr;
		UINT Flags = 0;
		Flags |= D3D11_CREATE_DEVICE_DEBUG;
		HRESULT hr = D3D11CreateDevice(ChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &DeviceContext);

		ASSERT(SUCCEEDED(hr));

		ID3D11VideoDevice* VideoDevice = nullptr;
		hr = Device->QueryInterface(IID_PPV_ARGS(&VideoDevice));
		ASSERT(SUCCEEDED(hr));

		ID3D11VideoContext* VideoContext = nullptr;
		hr = DeviceContext->QueryInterface(IID_PPV_ARGS(&VideoContext));
		ASSERT(SUCCEEDED(hr));

		D3D11VideoFuzzingState Fuzzer;
		Fuzzer.D3DDevice = Device;
		Fuzzer.VideoDevice = VideoDevice;
		Fuzzer.VideoContext = VideoContext;

		SetSeedOnVideoFuzzer(&Fuzzer, 0);
		DoIterationsWithVideoFuzzer(&Fuzzer, 1);

		return 0;
	}


	ID3D12Device* Device = nullptr;
	ASSERT(SUCCEEDED(D3D12CreateDevice(ChosenAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device))));


	if (0)
	{

		bool bIsSingleThreaded = false;

		if (bIsSingleThreaded)
		{
			D3DTextureCompressionFuzzingPersistentState Persistent;
			SetupPersistentOnTextureCompressionFuzzer(&Persistent, Device);

			for (int32 i = 0; i < 10 * 1000; i++)
			{
				TextureCompressionFuzzingState Fuzzer;
				Fuzzer.Persistent = &Persistent;
				Fuzzer.D3DDevice = Device;

				LOG("Setting seed %d on fuzzer", i);
				SetSeedOnTextureCompressionFuzzer(&Fuzzer, i);
				DoIterationsWithTextureCompressionFuzzer(&Fuzzer, 1);
			}
		}
		else
		{
			const int32 ThreadCount = 16;
			std::vector<std::thread> FuzzThreads;
			uint64 StartingTime = time(NULL);
			LOG("Starting time: %llu", StartingTime);

			for (int32 ThreadIdx = 0; ThreadIdx < ThreadCount; ThreadIdx++)
			{
				FuzzThreads.emplace_back([Device = Device, TIdx = ThreadIdx, StartingTime = StartingTime]() {
					D3DTextureCompressionFuzzingPersistentState Persistent;
					SetupPersistentOnTextureCompressionFuzzer(&Persistent, Device);

					for (int32 i = 0; i < 1024 * 1024; i++)
					{
						uint64 InitialFuzzSeed = 0;

						// If we want to have different fuzzing each process run. Good once a fuzzer is established.
						InitialFuzzSeed += StartingTime * 0x8FD3F77LLU;

						InitialFuzzSeed += (TIdx * 1024LLU * 1024LLU);
						InitialFuzzSeed += i;

						// In theory can cause contention maybe or slow things down? Idk, can remove this
						LOG("Fuzing with seed %llu", InitialFuzzSeed);

						TextureCompressionFuzzingState Fuzzer;
						Fuzzer.Persistent = &Persistent;
						Fuzzer.D3DDevice = Device;
						SetSeedOnTextureCompressionFuzzer(&Fuzzer, InitialFuzzSeed);
						DoIterationsWithTextureCompressionFuzzer(&Fuzzer, 1);
					}
				});
			}

			for (auto& Thread : FuzzThreads)
			{
				Thread.join();
			}
		}

		return 0;
	}


	if (0)
	{
		bool bIsSingleThreaded = false;

		if (bIsSingleThreaded)
		{
			D3DReservedResourceFuzzingPersistentState Persistent;
			SetupPersistentOnReservedResourceFuzzer(&Persistent, Device);

			for (int32 i = 0; i < 10*1000; i++)
			{
				ReservedResourceFuzzingState Fuzzer;
				Fuzzer.Persistent = &Persistent;
				Fuzzer.D3DDevice = Device;

				LOG("Setting seed %d on fuzzer", i);
				SetSeedOnReservedResourceFuzzer(&Fuzzer, i);
				DoIterationsWithReservedResourceFuzzer(&Fuzzer, 1);
			}
		}
		else
		{
			const int32 ThreadCount = 3;
			std::vector<std::thread> FuzzThreads;
			uint64 StartingTime = time(NULL);
			LOG("Starting time: %llu", StartingTime);

			for (int32 ThreadIdx = 0; ThreadIdx < ThreadCount; ThreadIdx++)
			{
				FuzzThreads.emplace_back([Device = Device, TIdx = ThreadIdx, StartingTime = StartingTime]() {
					D3DReservedResourceFuzzingPersistentState PersistState;
					SetupPersistentOnReservedResourceFuzzer(&PersistState, Device);

					for (int32 i = 0; i < 128 * 1000; i++)
					{
						ReservedResourceFuzzingState Fuzzer;
						Fuzzer.D3DDevice = Device;
						Fuzzer.Persistent = &PersistState;

						uint64 InitialFuzzSeed = 0;

						// If we want to have different fuzzing each process run. Good once a fuzzer is established.
						InitialFuzzSeed += StartingTime * 0x8D3F77LLU;

						InitialFuzzSeed += (TIdx * 1024LLU * 1024LLU);
						InitialFuzzSeed += i;

						// In theory can cause contention maybe or slow things down? Idk, can remove this
						LOG("Fuzing with seed %llu", InitialFuzzSeed);

						SetSeedOnReservedResourceFuzzer(&Fuzzer, InitialFuzzSeed);
						DoIterationsWithReservedResourceFuzzer(&Fuzzer, 1);

						// Helpful if we want some output to know that it's going but don't want spam
						//if (i % 100 == 0)
						//{
						//	LOG("Thread %d run %d finished", TIdx, i);
						//}
					}
				});
			}

			for (auto& Thread : FuzzThreads)
			{
				Thread.join();
			}
		}

		return 0;
	}

	//if (0)
	{
		//const char* ExampleShaderFilename = "example_shaders/9795564935892538031_pixel.dxbc";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_add_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_mul_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_cbv1_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_tex1_bytecode.bin";
		const char* ExampleShaderFilename = "dxbc_re/vs_plain_tex_sample_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_tex_sample_2_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/ps_plain_bytecode.bin";
		//const char* ExampleShaderFilename = "manual_bytecode/raw_reinsert_01.bin";
		//const char* ExampleShaderFilename = "dxbc_re/vs_plain_tex_sample_cbv_bytecode.bin";
		//const char* ExampleShaderFilename = "dxbc_re/ps_plain_add_coords_bytecode.bin";

		{
			FuzzDXBCState DXBCFuzzer;
			GenerateShaderDXBC(&DXBCFuzzer);
		
			//ParseDXBCCode((byte*)FileData, FileSize);
		}


		//void* FileData = nullptr;
		//int32 FileSize = 0;
		//ReadDataFromFile(ExampleShaderFilename, &FileData, &FileSize);
		//
		//ParseDXBCCode((byte*)FileData, FileSize);

		//ID3DBlob* Disasm = nullptr;
		//HRESULT hr = D3DDisassemble(FileData, FileSize, 0, nullptr, &Disasm);
		//ASSERT(SUCCEEDED(hr));
		//
		//WriteDataToFile("manual_bytecode/raw_reinsert_01_disasm.txt", Disasm->GetBufferPointer(), Disasm->GetBufferSize());


		return 0;
	}

	if (0)
	{
		

		SpitOutShaderInfo();

		return 0;
	}


	//if (0)
	{
		bool bIsSingleThreaded = true;

		ShaderFuzzConfig ShaderConfig;
		ShaderConfig.EnsureBetterPixelCoverage = 1;
		ShaderConfig.ForcePixelOutputAlphaToOne = 1;
		ShaderConfig.DisableBlendingState = 1;
		ShaderConfig.CBVUploadRandomFloatData = 1;
		ShaderConfig.ResourceDeletionChance = 0.9f;
		ShaderConfig.HeapDeletionChance = 0;// 0.4f;
		ShaderConfig.PlacedResourceChance = 0;// 0.3f;

		ShaderConfig.ShouldReadbackImage = true;
		ShaderConfig.ReadbackImageNamePrepend = "image_case_dxbc_fuzz_";

		{
			DXGI_ADAPTER_DESC Desc = {};
			ChosenAdapter->GetDesc(&Desc);
			if (Desc.VendorId == 0x10DE)
			{
				ShaderConfig.ReadbackImageNameAppend = "_nvidia";
			}
			else if (Desc.VendorId == 0x8086)
			{
				ShaderConfig.ReadbackImageNameAppend = "_intel";
			}
			else if (Desc.VendorId == 0x1414)
			{
				ShaderConfig.ReadbackImageNameAppend = "_warp";
			}
			else
			{
				ASSERT(false && "I don't have any AMD cards to test on, feel free to write this code lol");
			}
		}

		{
			DXGI_ADAPTER_DESC Desc = {};
			ChosenAdapter->GetDesc(&Desc);
			ShaderConfig.AllowConservativeRasterization = false;// (Desc.VendorId != 0x1414 || Desc.DeviceId != 0x8C);
			ShaderConfig.LockMutexAroundExecCmdList = (Desc.VendorId == 0x1414 && Desc.DeviceId == 0x8C && !bIsSingleThreaded);
		}

		if (bIsSingleThreaded)
		{
			LARGE_INTEGER PerfFreq;
			QueryPerformanceFrequency(&PerfFreq);
			
			LARGE_INTEGER PerfStart;
			QueryPerformanceCounter(&PerfStart);
			
			const int32 TestCases = 1;// 100;

			D3DDrawingFuzzingPersistentState PersistState;
			PersistState.ResourceMgr.D3DDevice = Device;
			SetupFuzzPersistState(&PersistState, &ShaderConfig, Device);

			// Single threaded
			for (int32 i = 0; i < TestCases; i++)
			{
				ShaderFuzzingState Fuzzer;
				Fuzzer.D3DDevice = Device;
				Fuzzer.D3DPersist = &PersistState;
				Fuzzer.Config = &ShaderConfig;

				LOG("Doing round %d of fuzzing...", i);
				SetSeedOnFuzzer(&Fuzzer, i);
				DoIterationsWithFuzzer(&Fuzzer, 1);
			}
			
			LARGE_INTEGER PerfEnd;
			QueryPerformanceCounter(&PerfEnd);
			
			double ElapsedTimeSeconds = (PerfEnd.QuadPart - PerfStart.QuadPart);
			ElapsedTimeSeconds = ElapsedTimeSeconds / PerfFreq.QuadPart;
			LOG("Ran %d test cases in %3.2f seconds, or %3.2f ms/case", TestCases, ElapsedTimeSeconds, (ElapsedTimeSeconds / TestCases) * 1000.0f);
		}
		else
		{
			const int32 ThreadCount = 6;
			std::vector<std::thread> FuzzThreads;

			std::mutex DebugMutex;

			uint64 StartingTime = time(NULL);
			LOG("Starting time: %llu", StartingTime);

			for (int32 ThreadIdx = 0; ThreadIdx < ThreadCount; ThreadIdx++)
			{
				FuzzThreads.emplace_back([Device = Device, TIdx = ThreadIdx, ConfigPtr = &ShaderConfig, StartingTime = StartingTime, MutexPtr = &DebugMutex]() {
					D3DDrawingFuzzingPersistentState PersistState;
					PersistState.ResourceMgr.D3DDevice = Device;
					PersistState.ExecuteCommandListMutex = MutexPtr;
					SetupFuzzPersistState(&PersistState, ConfigPtr, Device);

					for (int32 i = 0; i < 128 * 1000; i++)
					{
						ShaderFuzzingState Fuzzer;
						Fuzzer.D3DDevice = Device;
						Fuzzer.D3DPersist = &PersistState;
						Fuzzer.Config = ConfigPtr;
		
						uint64 InitialFuzzSeed = 0;
		
						// If we want to have different fuzzing each process run. Good once a fuzzer is established.
						InitialFuzzSeed += StartingTime * 0x8D3F77LLU;
		
						InitialFuzzSeed += (TIdx * 1024LLU * 1024LLU);
						InitialFuzzSeed += i;
		
						// In theory can cause contention maybe or slow things down? Idk, can remove this
						LOG("Fuzing with seed %llu", InitialFuzzSeed);
					
						SetSeedOnFuzzer(&Fuzzer, InitialFuzzSeed);
						DoIterationsWithFuzzer(&Fuzzer, 1);
		
						// Helpful if we want some output to know that it's going but don't want spam
						//if (i % 100 == 0)
						//{
						//	LOG("Thread %d run %d finished", TIdx, i);
						//}
					}
				});
			}
		
			for (auto& Thread : FuzzThreads)
			{
				Thread.join();
			}
		}
	
		return 0;
	}

	return 0;
}

