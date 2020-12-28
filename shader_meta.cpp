#include "shader_meta.h"

#include "basics.h"

#include <string.h>
#include <assert.h>

static const char* ShaderSemanticNames[] = {
	"POSITION",
	"COLOR",
	"TEXCOORD",
	"NORMAL",
	"SV_POSITION",
};

static_assert(ARRAY_COUNTOF(ShaderSemanticNames) == (int32)ShaderSemantic::Count, "sadfassdgf");


ShaderSemantic GetSemanticFromSemanticName(const char* Name)
{
	for (int32 i = 0; i < (int32)ShaderSemantic::Count; i++)
	{
		if (strcmp(Name, ShaderSemanticNames[i]))
		{
			return (ShaderSemantic)i;
		}
	}

	assert(false);
	return ShaderSemantic::POSITION;
}

const char* GetSemanticNameFromSemantic(ShaderSemantic Value)
{
	return ShaderSemanticNames[(int32)Value];
}


const char* GetTargetForShaderType(D3DShaderType Type) {
	if (Type == D3DShaderType::Vertex) {
		return "vs_5_0";
	}
	else if (Type == D3DShaderType::Pixel) {
		return "ps_5_0";
	}
	else {
		assert(false && "Bad shader type!!!");
	}
}


ID3DBlob* CompileShaderCode(const char* ShaderCode, D3DShaderType ShaderType, const char* ShaderSourceName, const char* EntryPoint, ShaderMetadata* OutMetadata) {
	ID3DBlob* ByteCode = nullptr;
	ID3DBlob* ErrorMsg = nullptr;
	UINT CompilerFlags = D3DCOMPILE_DEBUG;
	HRESULT hr = D3DCompile(ShaderCode, strlen(ShaderCode), ShaderSourceName, nullptr, nullptr, EntryPoint, GetTargetForShaderType(ShaderType), CompilerFlags, 0, &ByteCode, &ErrorMsg);
	if (SUCCEEDED(hr)) {
		ID3D12ShaderReflection* ShaderReflection = nullptr;
		hr = D3DReflect(ByteCode->GetBufferPointer(), ByteCode->GetBufferSize(), IID_PPV_ARGS(&ShaderReflection));

		D3D12_SHADER_DESC ShaderDesc = {};
		hr = ShaderReflection->GetDesc(&ShaderDesc);

		D3D12_SIGNATURE_PARAMETER_DESC InputParamDescs[MAX_INPUT_PARAM_COUNT] = {};

		OutMetadata->NumParams = 0;

		assert(ShaderDesc.InputParameters < MAX_INPUT_PARAM_COUNT);

		for (int32 i = 0; i < ShaderDesc.InputParameters; i++)
		{
			hr = ShaderReflection->GetInputParameterDesc(i, &InputParamDescs[i]);

			ShaderInputParamMetadata ParamMeta = {};
			ParamMeta.Semantic = GetSemanticFromSemanticName(InputParamDescs[i].SemanticName);
			ParamMeta.ParamIndex = InputParamDescs[i].Register;

			OutMetadata->InputParamMetadata[OutMetadata->NumParams] = ParamMeta;
			OutMetadata->NumParams++;
		}

		D3D12_SHADER_INPUT_BIND_DESC BoundResourceDescs[MAX_BOUND_RESOURCES] = {};

		OutMetadata->NumCBVs = 0;
		OutMetadata->NumSRVs = 0;
		OutMetadata->NumStaticSamplers = 0;

		assert(ShaderDesc.BoundResources < MAX_BOUND_RESOURCES);

		for (int32 i = 0; i < ShaderDesc.BoundResources; i++)
		{
			ShaderReflection->GetResourceBindingDesc(i, &BoundResourceDescs[i]);

			if (BoundResourceDescs[i].Type == D3D_SIT_TEXTURE)
			{
				OutMetadata->NumSRVs++;
			}
			else if (BoundResourceDescs[i].Type == D3D_SIT_SAMPLER)
			{
				OutMetadata->NumStaticSamplers++;
			}
			else if (BoundResourceDescs[i].Type == D3D_SIT_CBUFFER)
			{
				//BoundResourceDescs[i].Space
				OutMetadata->NumCBVs++;
			}
		}

		return ByteCode;
	}
	else {
		LOG("Compile of '%s' failed, hr = 0x%08X, err msg = '%s'", ShaderSourceName, hr, (ErrorMsg && ErrorMsg->GetBufferPointer()) ? (const char*)ErrorMsg->GetBufferPointer() : "<NONE_GIVEN>");
		ASSERT(false && "Fix the damn shaders");
		return nullptr;
	}
}

