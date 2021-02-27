#pragma once

#include "basics.h"

void ParseDXBCCode(byte* Code, int32 Length);


struct FuzzGenerateD3DBytecodeState
{
	uint32 NumTempRegisters = 0;
	uint32 TempRegisterClobberMask = 0;
	uint32 NextWrittenTempRegister = 0;

	std::vector<uint32> OutputBytecode;
};


void GenerateBytecodeOpcodes(FuzzGenerateD3DBytecodeState* Bytecode);
