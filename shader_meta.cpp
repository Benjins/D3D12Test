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

