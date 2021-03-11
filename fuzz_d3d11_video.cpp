
#define _CRT_SECURE_NO_WARNINGS

#include "fuzz_d3d11_video.h"

#include "string_stack_buffer.h"

#include <dxva.h>
#include <dxgi1_2.h>

//#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void SetSeedOnVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, uint64 Seed)
{
	Fuzzer->RNGState.seed(Seed);
	Fuzzer->InitialFuzzSeed = Seed;
}

void ReadFileIntoVec(const char* FileName, std::vector<byte>* OutData)
{
	FILE* f = NULL;
	fopen_s(&f, FileName, "rb");

	fseek(f, 0, SEEK_END);
	OutData->resize(ftell(f));
	fseek(f, 0, SEEK_SET);

	fread(OutData->data(), 1, OutData->size(), f);

	fclose(f);
}

#define MY_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

MY_DEFINE_GUID(MY_D3D11_DECODER_PROFILE_H264_VLD_NOFGT, 0x1b81be68, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
   DEFINE_GUID(   D3D11_DECODER_PROFILE_H264_VLD_NOFGT, 0x1b81be68, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);


//void LogToFile(const char* LogMsg, ...)
//{
//	static FILE* f = fopen("err.log", "w");
//	
//	va_list vl;
//	va_start(vl, LogMsg);
//	
//	vfprintf(f, LogMsg, vl);
//	fprintf(f, "\n");
//	fflush(f);
//	
//	va_end(vl);
//}

void DoIterationsWithVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, int32 NumIterations)
{
	HRESULT hr;

	DXGI_FORMAT OutputFormat = DXGI_FORMAT_NV12;
	const int32 VideoWidth = 1280;
	const int32 VideoHeight = 720;

	ID3D11VideoDecoder* VideoDecoder = nullptr;

	D3D11_VIDEO_DECODER_DESC VideoDesc = {};
	VideoDesc.Guid = MY_D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
	VideoDesc.OutputFormat = OutputFormat;
	VideoDesc.SampleWidth = VideoWidth;
	VideoDesc.SampleHeight = VideoHeight;

	//std::lock_guard<std::mutex> Lock(*Fuzzer->Persistent->DecodingMutex);

	uint32 ConfigCount = 0;
	Fuzzer->Persistent->VideoDevice->GetVideoDecoderConfigCount(&VideoDesc, &ConfigCount);

	ASSERT(ConfigCount > 0);

	D3D11_VIDEO_DECODER_CONFIG VideoConfig = {};
	Fuzzer->Persistent->VideoDevice->GetVideoDecoderConfig(&VideoDesc, 0, &VideoConfig);

	// This is just what we expect, I guess we could iterate over configs to find one that does this, idk
	ASSERT(VideoConfig.ConfigBitstreamRaw == 2);

	hr = Fuzzer->Persistent->VideoDevice->CreateVideoDecoder(&VideoDesc, &VideoConfig, &VideoDecoder);
	ASSERT(SUCCEEDED(hr));
	ASSERT(VideoDecoder != nullptr);

	ID3D11VideoProcessor* VideoProcessor = nullptr;
	hr = Fuzzer->Persistent->VideoDevice->CreateVideoProcessor(Fuzzer->Persistent->ProcessorEnumerator, 0, &VideoProcessor);
	ASSERT(SUCCEEDED(hr));
	ASSERT(VideoProcessor != nullptr);

	int32 NumFrames = Fuzzer->Persistent->ExampleVidData.size();

	{
		D3D11_TEXTURE2D_DESC Desc = {};
		Fuzzer->Persistent->OutputTexture->GetDesc(&Desc);
		ASSERT(Fuzzer->Persistent->DecoderOutputViews.size() == Desc.ArraySize);
		ASSERT(Fuzzer->Persistent->DecoderOutputViews.size() == Fuzzer->Persistent->FinalInputViews.size());
	}

	for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
	{
		//if (Iteration == 1)
		//{
		//	SetSeedOnVideoFuzzer(Fuzzer, 243314340147589534LLU);
		//}

		for (int32 i = 0; i < NumFrames; i++)
		{
			int32 OutputIndex = (NumFrames * Iteration + i) % Fuzzer->Persistent->DecoderOutputViews.size();

			//LogToFile("Begining of frame %d", i);
			hr = Fuzzer->Persistent->VideoContext->DecoderBeginFrame(VideoDecoder, Fuzzer->Persistent->DecoderOutputViews[OutputIndex], 0, nullptr);
			//LogToFile("After DecoderBeginFrame %d", i);
			// TODO: Handle E_PENDING and D3DERR_WASSTILLDRAWING
			ASSERT(hr != E_PENDING);
			//ASSERT(hr != D3DERR_WASSTILLDRAWING);
			ASSERT(SUCCEEDED(hr));

			std::vector<D3D11_VIDEO_DECODER_BUFFER_DESC> VideoBufferDescs;

			auto SetBuffer = [](ID3D11VideoContext* VideoContext, ID3D11VideoDecoder* VideoDecoder, D3D11_VIDEO_DECODER_BUFFER_TYPE BufferType, const std::vector<byte>& Data, std::vector<D3D11_VIDEO_DECODER_BUFFER_DESC>* OutVideoBufferDescs) {
				void* pBuffer = nullptr;
				UINT uSize = 0;
				VideoContext->GetDecoderBuffer(VideoDecoder, BufferType, &uSize, &pBuffer);
				ASSERT(Data.size() <= uSize);
				memcpy(pBuffer, Data.data(), Data.size());
				VideoContext->ReleaseDecoderBuffer(VideoDecoder, BufferType);

				D3D11_VIDEO_DECODER_BUFFER_DESC DecoderBufferDesc = {};
				DecoderBufferDesc.BufferType = BufferType;
				DecoderBufferDesc.DataOffset = 0;
				DecoderBufferDesc.DataSize = Data.size();
				OutVideoBufferDescs->push_back(DecoderBufferDesc);
			};

			DXVA_Slice_H264_Short SliceInfo = {};
			ASSERT(sizeof(SliceInfo) == Fuzzer->Persistent->ExampleVidData[i].SliceControl.size());
			memcpy(&SliceInfo, Fuzzer->Persistent->ExampleVidData[i].SliceControl.data(), Fuzzer->Persistent->ExampleVidData[i].SliceControl.size());

			// TODO: Why plus 1? Did I save it out incorrectly
			SliceInfo.SliceBytesInBuffer++;
			memcpy(Fuzzer->Persistent->ExampleVidData[i].SliceControl.data(), &SliceInfo, sizeof(SliceInfo));

			SetBuffer(Fuzzer->Persistent->VideoContext, VideoDecoder, D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, Fuzzer->Persistent->ExampleVidData[i].PictureParams, &VideoBufferDescs);
			//LogToFile("After PictureParams %d", i);
			SetBuffer(Fuzzer->Persistent->VideoContext, VideoDecoder, D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX, Fuzzer->Persistent->ExampleVidData[i].QMatrix, &VideoBufferDescs);
			//LogToFile("After QMatrix %d", i);
			SetBuffer(Fuzzer->Persistent->VideoContext, VideoDecoder, D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, Fuzzer->Persistent->ExampleVidData[i].SliceControl, &VideoBufferDescs);
			//LogToFile("After SliceControl %d", i);

			{
				auto BitStream = Fuzzer->Persistent->ExampleVidData[i].Bitstream;

				BitStream.resize(SliceInfo.SliceBytesInBuffer, 0);

				int32 NumCorruptions = 0;// Fuzzer->GetIntInRange(1, 20);
				for (int32 i = 0; i < NumCorruptions; i++)
				{
					// Byte corruption
					BitStream[Fuzzer->GetIntInRange(4, BitStream.size() - 1)] = (byte)Fuzzer->GetIntInRange(0, 255);
				}

				int32 BitFlips = 0;// Fuzzer->GetIntInRange(0, 10);
				for (int32 i = 0; i < BitFlips; i++)
				{
					// Bit flip
					BitStream[Fuzzer->GetIntInRange(4, BitStream.size() - 1)] ^= (byte)(1 << Fuzzer->GetIntInRange(0, 7));
				}

				// Whole-buffer garbage replacement
				//for (int32 i = 4; i < BitStream.size(); i++)
				//{
				//	BitStream[i] = (byte)Fuzzer->GetIntInRange(0, 255);
				//}

				const int32 BitStreamDataLen = BitStream.size() - 4;
				BitStream[0] = 0;
				BitStream[1] = 0;
				BitStream[2] = (byte)(BitStreamDataLen / 256);
				BitStream[3] = (byte)(BitStreamDataLen % 256);

				int32 PaddedEndLen = ((BitStream.size() + 127) / 128) * 128;
				BitStream.resize(PaddedEndLen, 0);

				//WriteDataToFile(StringStackBuffer<256>("../bitstream_unmod/frame_%02d.bin", i).buffer, BitStream.data(), BitStream.size());

				SetBuffer(Fuzzer->Persistent->VideoContext, VideoDecoder, D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, BitStream, &VideoBufferDescs);
			}

			//LogToFile("After BitStream %d", i);

			hr = Fuzzer->Persistent->VideoContext->SubmitDecoderBuffers(VideoDecoder, VideoBufferDescs.size(), VideoBufferDescs.data());
			//LogToFile("After SubmitDecoderBuffers %d", i);
			ASSERT(SUCCEEDED(hr));

			hr = Fuzzer->Persistent->VideoContext->DecoderEndFrame(VideoDecoder);
			//LogToFile("After DecoderEndFrame %d", i);
			ASSERT(SUCCEEDED(hr));

			D3D11_VIDEO_PROCESSOR_STREAM streams = {};
			streams.Enable = true;
			streams.pInputSurface = Fuzzer->Persistent->FinalInputViews[OutputIndex];
			Fuzzer->Persistent->VideoContext->VideoProcessorBlt(VideoProcessor, Fuzzer->Persistent->FinalOutputView, 0, 1, &streams);
			//LogToFile("After VideoProcessorBlt %d", i);

			const bool ShouldDoReadbackAndWriteToFile = false;

			if (ShouldDoReadbackAndWriteToFile)
			{
				Fuzzer->Persistent->D3DContext->CopyResource(Fuzzer->Persistent->FinalOutputTextureCopy, Fuzzer->Persistent->FinalOutputTexture);
			}

			ID3D11Query* FlushingQuery = nullptr;
			D3D11_QUERY_DESC FlushingQueryDesc = {};
			FlushingQueryDesc.Query = D3D11_QUERY_EVENT;
			Fuzzer->Persistent->D3DDevice->CreateQuery(&FlushingQueryDesc, &FlushingQuery);
			ASSERT(FlushingQuery != nullptr);
			Fuzzer->Persistent->D3DContext->End(FlushingQuery);

			//LogToFile("Before Flush %d", i);
			Fuzzer->Persistent->D3DContext->Flush();
			//LogToFile("After Flush %d", i);

			int32 DataOut = 0;
			while (Fuzzer->Persistent->D3DContext->GetData(FlushingQuery, &DataOut, sizeof(DataOut), 0) != S_OK)
			{
				Sleep(1);
			}

			FlushingQuery->Release();
			//LogToFile("After Flush/finish %d", i);

			if (ShouldDoReadbackAndWriteToFile)
			{
				D3D11_MAPPED_SUBRESOURCE MappedSubresource = {};
				hr = Fuzzer->Persistent->D3DContext->Map(Fuzzer->Persistent->FinalOutputTextureCopy, 0, D3D11_MAP_READ, 0, &MappedSubresource);
				ASSERT(SUCCEEDED(hr));
			
				stbi_write_png(StringStackBuffer<256>("../vid_decode_out/case_%llu_frame_%02d.png", Fuzzer->InitialFuzzSeed, i).buffer, VideoWidth, VideoHeight, 4, MappedSubresource.pData, MappedSubresource.RowPitch);
			
				Fuzzer->Persistent->D3DContext->Unmap(Fuzzer->Persistent->FinalOutputTextureCopy, 0);
			}
		}
	}

	//FinalOutputTextureCopy->Release();
	//FinalOutputView->Release();
	//for (auto* FinalInputView : FinalInputViews)
	//{
	//	FinalInputView->Release();
	//}
	//for (auto* DecoderOutputView : DecoderOutputViews)
	//{
	//	DecoderOutputView->Release();
	//}
	//OutputTexture->Release();
	//FinalOutputTexture->Release();
	//VideoProcessor->Release();
	//ProcessorEnumerator->Release();

	// Doing either of these releases causes the bug to repro
	// NOTE: Actually it still repros without either of them
	//VideoDecoder->Release();
	//VideoProcessor->Release();

	// This sleep seems to make it less likely to repro, but it still happens
	//Sleep(1000);

	VideoDecoder->Release();
	VideoProcessor->Release();
}


void SetupVideoFuzzerPersistentState(D3D11VideoFuzzingPersistentState* Persistent, IDXGIAdapter* ChosenAdapter) {
	HRESULT hr;
	
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
	UINT Flags = 0;
	Flags |= D3D11_CREATE_DEVICE_DEBUG;
	hr = D3D11CreateDevice(ChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &DeviceContext);

	ASSERT(SUCCEEDED(hr));

	ID3D11VideoDevice* VideoDevice = nullptr;
	hr = Device->QueryInterface(IID_PPV_ARGS(&VideoDevice));
	ASSERT(SUCCEEDED(hr));

	ID3D11VideoContext* VideoContext = nullptr;
	hr = DeviceContext->QueryInterface(IID_PPV_ARGS(&VideoContext));
	ASSERT(SUCCEEDED(hr));

	Persistent->D3DContext = DeviceContext;

	Persistent->D3DDevice = Device;
	Persistent->VideoDevice = VideoDevice;
	Persistent->VideoContext = VideoContext;

	//Persistent->VideoDecoder = VideoDecoder;

	auto ReadExampleVidData = [&](int64 Timestamp) {
		DebugExampleVideoData VidData;

		ReadFileIntoVec(StringStackBuffer<256>("../example_vid_data_buffers/buff_%lld_pic_params.bin", Timestamp).buffer, &VidData.PictureParams);
		ReadFileIntoVec(StringStackBuffer<256>("../example_vid_data_buffers/buff_%lld_qmatrix.bin", Timestamp).buffer, &VidData.QMatrix);
		ReadFileIntoVec(StringStackBuffer<256>("../example_vid_data_buffers/buff_%lld_slice_control.bin", Timestamp).buffer, &VidData.SliceControl);
		ReadFileIntoVec(StringStackBuffer<256>("../example_vid_data_buffers/buff_%lld_bitstream.bin", Timestamp).buffer, &VidData.Bitstream);

		Persistent->ExampleVidData.push_back(VidData);
	};

	const static int64 ExampleVideoTimestamps[] = {
		667332, 1000998, 1334664, 1668332, 2001996, 2335662,
		23690286, 24023953, 24357618, 24691284, 25024952,
		25358616, 25692282, 26025950, 26359614, 26693280 };
	const uint32 NumFrames = ARRAY_COUNTOF(ExampleVideoTimestamps);
	for (int32 i = 0; i < NumFrames; i++)
	{
		ReadExampleVidData(ExampleVideoTimestamps[i]);
	}

	DXGI_FORMAT OutputFormat = DXGI_FORMAT_NV12;
	DXGI_FORMAT FinalOutputFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

	// Raging loop's video that I'm using as an example is this dimension
	const int32 VideoWidth = 1280;
	const int32 VideoHeight = 720;

	const int32 NumOutputFrames = 30;

	ID3D11Texture2D* OutputTexture = nullptr;
	{
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Format = OutputFormat;
		TextureDesc.ArraySize = NumOutputFrames;
		TextureDesc.MipLevels = 1;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.Width = VideoWidth;
		TextureDesc.Height = VideoHeight;
		TextureDesc.BindFlags = D3D11_BIND_DECODER;
		hr = Device->CreateTexture2D(&TextureDesc, nullptr, &OutputTexture);
		ASSERT(SUCCEEDED(hr));
		ASSERT(OutputTexture != nullptr);
	}

	Persistent->OutputTexture = OutputTexture;


	//std::vector<ID3D11VideoDecoderOutputView*> DecoderOutputViews;
	for (int32 i = 0; i < NumOutputFrames; i++)
	{
		D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC DecoderOutputViewDesc = {};
		DecoderOutputViewDesc.DecodeProfile = MY_D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
		DecoderOutputViewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
		// WARNING: Change this to 0 if you want to hardlock the system
		// ask me how I know
		DecoderOutputViewDesc.Texture2D.ArraySlice = i;

		ID3D11VideoDecoderOutputView* DecoderOutputView = nullptr;
		hr = VideoDevice->CreateVideoDecoderOutputView(OutputTexture, &DecoderOutputViewDesc, &DecoderOutputView);
		ASSERT(SUCCEEDED(hr));
		ASSERT(DecoderOutputView != nullptr);

		Persistent->DecoderOutputViews.push_back(DecoderOutputView);
	}

	ID3D11Texture2D* FinalOutputTexture = nullptr;
	{
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Format = FinalOutputFormat;
		TextureDesc.ArraySize = 1;
		TextureDesc.MipLevels = 1;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.Width = VideoWidth;
		TextureDesc.Height = VideoHeight;
		TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		hr = Device->CreateTexture2D(&TextureDesc, nullptr, &FinalOutputTexture);
		ASSERT(SUCCEEDED(hr));
		ASSERT(FinalOutputTexture != nullptr);
	}

	ID3D11Texture2D* FinalOutputTextureCopy = nullptr;
	{
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Format = FinalOutputFormat;
		TextureDesc.ArraySize = 1;
		TextureDesc.MipLevels = 1;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.Width = VideoWidth;
		TextureDesc.Height = VideoHeight;
		TextureDesc.Usage = D3D11_USAGE_STAGING;
		TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		hr = Device->CreateTexture2D(&TextureDesc, nullptr, &FinalOutputTextureCopy);
		ASSERT(SUCCEEDED(hr));
		ASSERT(FinalOutputTextureCopy != nullptr);
	}

	Persistent->FinalOutputTexture = FinalOutputTexture;
	Persistent->FinalOutputTextureCopy = FinalOutputTextureCopy;

	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc = {};
	ContentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

	ContentDesc.InputFrameRate.Denominator = 1;
	ContentDesc.InputFrameRate.Numerator = 30;
	ContentDesc.InputWidth = VideoWidth;
	ContentDesc.InputHeight = VideoHeight;

	ContentDesc.OutputFrameRate.Denominator = 1;
	ContentDesc.OutputFrameRate.Numerator = 30;
	ContentDesc.OutputWidth = VideoWidth;
	ContentDesc.OutputHeight = VideoHeight;

	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	ID3D11VideoProcessorEnumerator* ProcessorEnumerator = nullptr;
	hr = VideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &ProcessorEnumerator);
	ASSERT(SUCCEEDED(hr));
	ASSERT(ProcessorEnumerator != nullptr);

	//ID3D11VideoProcessor* VideoProcessor = nullptr;
	//hr = VideoDevice->CreateVideoProcessor(ProcessorEnumerator, 0, &VideoProcessor);
	//ASSERT(SUCCEEDED(hr));
	//ASSERT(VideoProcessor != nullptr);

	Persistent->ProcessorEnumerator = ProcessorEnumerator;
	//Persistent->VideoProcessor = VideoProcessor;

	ID3D11VideoProcessorOutputView* FinalOutputView = nullptr;
	{
		D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC FinalOutputViewDesc = {};
		FinalOutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
		FinalOutputViewDesc.Texture2D.MipSlice = 0;
		hr = VideoDevice->CreateVideoProcessorOutputView(FinalOutputTexture, ProcessorEnumerator, &FinalOutputViewDesc, &FinalOutputView);
		ASSERT(SUCCEEDED(hr));
		ASSERT(FinalOutputView != nullptr);
	}

	Persistent->FinalOutputView = FinalOutputView;

	for (int32 i = 0; i < NumOutputFrames; i++)
	{
		D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC FinalInputViewDesc = {};
		FinalInputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
		FinalInputViewDesc.Texture2D.MipSlice = 0;
		FinalInputViewDesc.Texture2D.ArraySlice = i;
		ID3D11VideoProcessorInputView* FinalInputView = nullptr;
		hr = VideoDevice->CreateVideoProcessorInputView(OutputTexture, ProcessorEnumerator, &FinalInputViewDesc, &FinalInputView);
		ASSERT(SUCCEEDED(hr));
		ASSERT(FinalInputView != nullptr);
		Persistent->FinalInputViews.push_back(FinalInputView);
	}
}

