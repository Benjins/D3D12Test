
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
	{
		const char* VSPlain = "float4 Main(float4 pos : POSITION) : SV_POSITION { return pos; }";
		SpitOutShaderInfoFor("vs_plain", VSPlain, D3DShaderType::Vertex);
	}

	{
		const char* VSPlain = "float4 Main(float4 pos : POSITION) : SV_POSITION { return pos * float4(1,2,3,4); }";
		SpitOutShaderInfoFor("vs_plain_mul", VSPlain, D3DShaderType::Vertex);
	}

	{
		const char* VSPlain = "float4 Main(float4 pos : POSITION) : SV_POSITION { return pos + float4(1,2,3,4); }";
		SpitOutShaderInfoFor("vs_plain_add", VSPlain, D3DShaderType::Vertex);
	}

	{
		const char* VSPlain = "float4 Main(float4 pos : POSITION, float2 uvs : TEXCOORD) : SV_POSITION { return pos + float4(uvs.xyxy); }";
		SpitOutShaderInfoFor("vs_plain_tex1", VSPlain, D3DShaderType::Vertex);
	}

	{
		const char* VSPlain = "float4 Main(float4 pos : POSITION, float2 uvs : TEXCOORD) : SV_POSITION { return pos + float4(uvs.xy, 0, 1); }";
		SpitOutShaderInfoFor("vs_plain_tex2", VSPlain, D3DShaderType::Vertex);
	}

	{
		const char* VSPlain = "struct PSInput { float4 pos : SV_POSITION; float2 uvs : TEXCOORD; }; PSInput Main(float4 pos : POSITION, float2 uvs : TEXCOORD) { PSInput res; res.pos = pos; res.uvs = uvs; return res; }";
		SpitOutShaderInfoFor("vs_plain_tex3", VSPlain, D3DShaderType::Vertex);
	}
}

