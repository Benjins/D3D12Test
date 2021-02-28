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
	"SV_TARGET",
};

static_assert(ARRAY_COUNTOF(ShaderSemanticNames) == (int32)ShaderSemantic::Count, "sadfassdgf");


ShaderSemantic GetSemanticFromSemanticName(const char* Name)
{
	for (int32 i = 0; i < (int32)ShaderSemantic::Count; i++)
	{
		if (strcmp(Name, ShaderSemanticNames[i]) == 0)
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
	UINT CompilerFlags = 0;// D3DCOMPILE_DEBUG;
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

		OutMetadata->NumCBVs = 0;
		OutMetadata->NumSRVs = 0;
		OutMetadata->NumStaticSamplers = 0;

		assert(ShaderDesc.BoundResources < MAX_BOUND_RESOURCES);

		for (int32 i = 0; i < ShaderDesc.BoundResources; i++)
		{
			D3D12_SHADER_INPUT_BIND_DESC BoundResourceDesc;
			ShaderReflection->GetResourceBindingDesc(i, &BoundResourceDesc);

			if (BoundResourceDesc.Type == D3D_SIT_TEXTURE)
			{
				OutMetadata->NumSRVs++;
			}
			else if (BoundResourceDesc.Type == D3D_SIT_SAMPLER)
			{
				OutMetadata->NumStaticSamplers++;
			}
			else if (BoundResourceDesc.Type == D3D_SIT_CBUFFER)
			{
				OutMetadata->NumCBVs++;
			}
		}

		ASSERT(OutMetadata->NumCBVs <= MAX_CBV_COUNT);

		for (int32 CBVIndex = 0; CBVIndex < OutMetadata->NumCBVs; CBVIndex++)
		{
			auto* CBVReflection = ShaderReflection->GetConstantBufferByIndex(CBVIndex);
			D3D12_SHADER_BUFFER_DESC CBVDesc = {};
			CBVReflection->GetDesc(&CBVDesc);

			OutMetadata->CBVSizes[CBVIndex] = CBVDesc.Size;
		}

		ShaderReflection->Release();

		return ByteCode;
	}
	else {
		LOG("Compile of '%s' failed, hr = 0x%08X, err msg = '%s'", ShaderSourceName, hr, (ErrorMsg && ErrorMsg->GetBufferPointer()) ? (const char*)ErrorMsg->GetBufferPointer() : "<NONE_GIVEN>");
		LOG("Dumping shader source now");
		OutputDebugStringA(ShaderCode);
		ASSERT(false && "Fix the damn shaders");
		return nullptr;
	}
}

