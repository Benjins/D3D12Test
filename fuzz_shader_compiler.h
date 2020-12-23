
#include <stdint.h>

#include <random>

struct ID3D12Device;

struct ShaderFuzzingState {
	ID3D12Device* D3DDevice = nullptr;
	std::mt19937_64 RNGState;

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


void SetSeedOnFuzzer(ShaderFuzzingState* Fuzzer, uint64_t Seed);
void DoIterationsWithFuzzer(ShaderFuzzingState* Fuzzer, int32_t NumIterations);

