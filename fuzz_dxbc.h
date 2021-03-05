#pragma once

#include "basics.h"

#include "fuzz_basic.h"

#include "shader_meta.h"

void ParseDXBCCode(byte* Code, int32 Length);



struct D3DOpcodeState
{

	//
	// These are set before generating the bytecode, they set the parameters of what we do
	uint32 NumTempRegisters = 0;
	int32 DataOpcodesToEmit = 0;
	D3DShaderType ShaderType = D3DShaderType::Vertex;
	int32 NumTextures = 0;
	int32 NumSamplers = 0;
	std::vector<int32> CBVSizes;
	std::vector<ShaderSemantic> InputSemantics;
	std::vector<ShaderSemantic> OutputSemantics;
	//

	std::vector<uint32> Opcodes;
};


struct FuzzDXBCState : FuzzBasicState
{
	ID3DBlob* VSBlob = nullptr;
	ID3DBlob* PSBlob = nullptr;

	// TODO: Config options
};

void GenerateBytecodeOpcodes(FuzzDXBCState* DXBCState, D3DOpcodeState* Bytecode);

void GenerateShaderDXBC(FuzzDXBCState* Bytecode);
