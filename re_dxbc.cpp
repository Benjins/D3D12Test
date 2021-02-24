
#include "re_dxbc.h"

#include <d3d12.h>
#include <d3dcompiler.h>

#include "shader_meta.h"

#include "string_stack_buffer.h"

static void SpitOutShaderInfoFor(const char* Name, const char* ShaderSource, D3DShaderType Type)
{
	ID3DBlob* CodeBlob = nullptr;
	ID3DBlob* ErrorBlob = nullptr;

	HRESULT hr = D3DCompile(ShaderSource, strlen(ShaderSource), "<code_shader>", nullptr, nullptr, "Main", GetTargetForShaderType(Type), 0, 0, &CodeBlob, &ErrorBlob);
	if (!SUCCEEDED(hr))
	{
		OutputDebugStringA(ShaderSource);
		OutputDebugStringA((char*)ErrorBlob->GetBufferPointer());
	}
	ASSERT(SUCCEEDED(hr));

	ID3DBlob* DisAsmBlob = nullptr;
	D3DDisassemble(CodeBlob->GetBufferPointer(), CodeBlob->GetBufferSize(), 0, nullptr, &DisAsmBlob);

	// Spit out shader source
	WriteStringToFile(StringStackBuffer<256>("../dxbc_re/%s_source.hlsl", Name).buffer, ShaderSource);

	// Spit out shader bytecode
	WriteDataToFile(StringStackBuffer<256>("../dxbc_re/%s_bytecode.bin", Name).buffer, CodeBlob->GetBufferPointer(), CodeBlob->GetBufferSize());

	// Spit out shader disassembly
	WriteDataToFile(StringStackBuffer<256>("../dxbc_re/%s_disasm.txt", Name).buffer, DisAsmBlob->GetBufferPointer(), DisAsmBlob->GetBufferSize());
}

void SpitOutShaderInfo()
{
	//{
	//	const char* VSPlain = "float4 Main(float4 pos : POSITION) : SV_POSITION { return pos; }";
	//	SpitOutShaderInfoFor("vs_plain", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "float4 Main(float4 pos : POSITION) : SV_POSITION { return pos * float4(1,2,3,4); }";
	//	SpitOutShaderInfoFor("vs_plain_mul", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "float4 Main(float4 pos : POSITION) : SV_POSITION { return pos + float4(1,2,3,4); }";
	//	SpitOutShaderInfoFor("vs_plain_add", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "float4 Main(float4 pos : POSITION, float2 uvs : TEXCOORD) : SV_POSITION { return pos + float4(uvs.xyxy); }";
	//	SpitOutShaderInfoFor("vs_plain_tex1", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "float4 Main(float4 pos : POSITION, float2 uvs : TEXCOORD) : SV_POSITION { return pos + float4(uvs.xy, 0, 1); }";
	//	SpitOutShaderInfoFor("vs_plain_tex2", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "struct PSInput { float4 pos : SV_POSITION; float2 uvs : TEXCOORD; }; PSInput Main(float4 pos : POSITION, float2 uvs : TEXCOORD) { PSInput res; res.pos = pos; res.uvs = uvs; return res; }";
	//	SpitOutShaderInfoFor("vs_plain_tex3", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "struct PSInput { float4 pos : SV_POSITION; }; cbuffer cbv1 { float4 f; }  PSInput Main(float4 pos : POSITION) { PSInput res; res.pos = pos + f; return res; }";
	//	SpitOutShaderInfoFor("vs_plain_cbv1", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "struct PSInput { float4 pos : SV_POSITION; }; cbuffer cbv1 { float4 f1; }; cbuffer cbv2 { float4 f2; };  PSInput Main(float4 pos : POSITION) { PSInput res; res.pos = pos * f1 + f2; return res; }";
	//	SpitOutShaderInfoFor("vs_plain_cbv2", VSPlain, D3DShaderType::Vertex);
	//}
	//
	//{
	//	const char* VSPlain = "struct PSInput { float4 pos : SV_POSITION; }; cbuffer cbv1 { float4 f1; }; cbuffer cbv2 { float4 f2; float4 f3; };  PSInput Main(float4 pos : POSITION) { PSInput res; res.pos = (pos + f3) * f1 + f2; return res; }";
	//	SpitOutShaderInfoFor("vs_plain_cbv3", VSPlain, D3DShaderType::Vertex);
	//}

	{
		const char* VSPlain = "struct PSInput { float4 pos : SV_POSITION; };\nTexture2D tex_6;\nSamplerState sampler_6;\nPSInput Main(float4 pos : POSITION) {\nPSInput res;\nres.pos = pos + tex_6.SampleLevel(sampler_6, pos.xy, 0);\nreturn res;\n}";
		SpitOutShaderInfoFor("vs_plain_tex_sample", VSPlain, D3DShaderType::Vertex);
	}
}

