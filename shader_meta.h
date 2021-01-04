#pragma once

#include <d3dcompiler.h>
#include <d3d12.h>

#include "basics.h"


#define MAX_INPUT_PARAM_COUNT 16
#define MAX_CBV_COUNT 64
#define MAX_BOUND_RESOURCES 64


enum struct ShaderSemantic
{
	IA_FIRST,
	INTER_FIRST = IA_FIRST,
	
	POSITION = IA_FIRST,
	COLOR,
	TEXCOORD,
	NORMAL,
	
	IA_LAST = NORMAL,

	SV_POSITION,
	INTER_LAST = SV_POSITION,
	
	
	Count
};

ShaderSemantic GetSemanticFromSemanticName(const char* Name);
const char* GetSemanticNameFromSemantic(ShaderSemantic Value);

struct ShaderInputParamMetadata
{
	ShaderSemantic Semantic = ShaderSemantic::POSITION;
	int32 SemanticIndex = 0;
	int32 ParamIndex = 0;
};

struct ShaderMetadata
{
	//int32 NumInlineConstants = 0;
	int32 NumCBVs = 0;
	int32 CBVSizes[MAX_CBV_COUNT] = {};

	int32 NumSRVs = 0;
	int32 NumStaticSamplers = 0;

	int32 NumParams = 0;
	ShaderInputParamMetadata InputParamMetadata[MAX_INPUT_PARAM_COUNT] = {};
};

enum struct D3DShaderType {
	Vertex,
	Pixel
};

const char* GetTargetForShaderType(D3DShaderType Type);


ID3DBlob* CompileShaderCode(const char* ShaderCode, D3DShaderType ShaderType, const char* ShaderSourceName, const char* EntryPoint, ShaderMetadata* OutMetadata);
