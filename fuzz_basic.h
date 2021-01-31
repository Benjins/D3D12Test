
#pragma once

#include <random>

#include "basics.h"


struct FuzzBasicState {
	std::mt19937_64 RNGState;

	uint64 InitialFuzzSeed = 0;

	// NOTE: It's inclusive
	int GetIntInRange(int min, int max)
	{
		std::uniform_int_distribution<int> Dist(min, max);

		return Dist(RNGState);
	}

	float GetFloatInRange(float min, float max)
	{
		std::uniform_real_distribution<float> Dist(min, max);

		return Dist(RNGState);
	}

	float GetFloat01()
	{
		return GetFloatInRange(0.0f, 1.0f);
	}
};


inline void SetRandomBytes(FuzzBasicState* Fuzzer, void* Buffer, int32 Size)
{
	byte* ByteBuffer = (byte*)Buffer;

	for (int32 i = 0; i < Size; i++)
	{
		ByteBuffer[i] = (byte)Fuzzer->GetIntInRange(0, 255);
	}
}




