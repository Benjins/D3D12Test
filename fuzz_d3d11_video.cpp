
#include "fuzz_d3d11_video.h"




void SetSeedOnVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, uint64 Seed)
{
	Fuzzer->RNGState.seed(Seed);
	Fuzzer->InitialFuzzSeed = Seed;
}


void DoIterationsWithVideoFuzzer(D3D11VideoFuzzingState* Fuzzer, int32 NumIterations)
{
	HRESULT hr;

	DXGI_FORMAT OutputFormat = DXGI_FORMAT_NV12;// DXGI_FORMAT_R8G8B8A8_UNORM;

	const int32 VideoWidth = 512;
	const int32 VideoHeight = 512;


	uint32 DecoderProfileCount = Fuzzer->VideoDevice->GetVideoDecoderProfileCount();
	std::vector<GUID> DecoderProfiles;
	DecoderProfiles.reserve(DecoderProfileCount);
	for (uint32 i = 0; i < DecoderProfileCount; i++)
	{
		GUID DecoderProfile;
		Fuzzer->VideoDevice->GetVideoDecoderProfile(i, &DecoderProfile);
		DecoderProfiles.push_back(DecoderProfile);
	}

	for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
	{

		ID3D11VideoDecoder* VideoDecoder = nullptr;
		
		D3D11_VIDEO_DECODER_DESC VideoDesc = {};
		VideoDesc.Guid = DecoderProfiles[Fuzzer->GetIntInRange(0, DecoderProfiles.size() - 1)];
		VideoDesc.OutputFormat = OutputFormat;
		VideoDesc.SampleWidth = VideoWidth;
		VideoDesc.SampleHeight = VideoHeight;

		uint32 ConfigCount = 0;
		Fuzzer->VideoDevice->GetVideoDecoderConfigCount(&VideoDesc, &ConfigCount);

		ASSERT(ConfigCount > 0);

		D3D11_VIDEO_DECODER_CONFIG VideoConfig = {};
		Fuzzer->VideoDevice->GetVideoDecoderConfig(&VideoDesc, 0, &VideoConfig);

		hr = Fuzzer->VideoDevice->CreateVideoDecoder(&VideoDesc, &VideoConfig, &VideoDecoder);
		ASSERT(SUCCEEDED(hr));

		ID3D11Texture2D* Texture = nullptr;
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		Fuzzer->D3DDevice->CreateTexture2D(&TextureDesc, NULL, &Texture);

		ID3D11VideoDecoderOutputView* DecoderOutputView = nullptr;
		Fuzzer->VideoDevice->CreateVideoDecoderOutputView(NULL, NULL, &DecoderOutputView);

		Fuzzer->VideoContext->DecoderBeginFrame(VideoDecoder, DecoderOutputView, 0, NULL);

		std::vector<D3D11_VIDEO_DECODER_BUFFER_DESC> VideoBufferDescs;
		{
			D3D11_VIDEO_DECODER_BUFFER_DESC DecoderBufferDesc = {};
			//DecoderBufferDesc.BufferType
		}

		Fuzzer->VideoContext->SubmitDecoderBuffers(VideoDecoder, VideoBufferDescs.size(), VideoBufferDescs.data());

		Fuzzer->VideoContext->DecoderEndFrame(VideoDecoder);
		
		//Fuzzer->VideoContext->DecoderBeginFrame




	}
}


