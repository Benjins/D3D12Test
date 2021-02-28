#pragma once

#include "basics.h"

#include "fuzz_basic.h"

#include "shader_meta.h"

void ParseDXBCCode(byte* Code, int32 Length);



struct D3DOpcodeState
{
	uint32 NumTempRegisters = 0;
	uint32 TempRegisterClobberMask = 0;
	uint32 NextWrittenTempRegister = 0;

	D3DShaderType ShaderType = D3DShaderType::Vertex;

	//
	// These are set before generating the bytecode, they set the parameters of what we do
	int32 NumTextures = 0;
	int32 NumSamplers = 0;
	std::vector<int32> CBVSizes;
	std::vector<ShaderSemantic> InputSemantics;
	std::vector<ShaderSemantic> OutputSemantics;
	//

	std::vector<uint32> Opcodes;
};


struct FuzzDXBCState
{
	ID3DBlob* VSBlob = nullptr;
	ID3DBlob* PSBlob = nullptr;
};

void GenerateBytecodeOpcodes(FuzzDXBCState* DXBCState, D3DOpcodeState* Bytecode);

void GenerateShaderDXBC(FuzzDXBCState* Bytecode);
