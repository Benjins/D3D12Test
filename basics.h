#pragma once


#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define ASSERT(cond) do { if (!(cond)) { char output[4096] = {}; snprintf(output, sizeof(output), "[%s:%d] Assertion failed '%s'\n", __FILE__, __LINE__, #cond); OutputDebugStringA(output); DebugBreak(); } } while(0)

#define LOG(fmt, ...) do { char output[2048] = {}; snprintf(output, sizeof(output), fmt "\n", ## __VA_ARGS__); OutputDebugStringA(output); } while(0)


#define ARRAY_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
using int64 = int64_t;
using uint16 = uint16_t;
using int16 = int16_t;
using byte = uint8_t;



inline void WriteDataToFile(const char* Filename, const void* Data, int32 Size)
{
	FILE* f = NULL;
	fopen_s(&f, Filename, "wb");

	fwrite(Data, 1, Size, f);

	fclose(f);
}

inline void ReadDataFromFile(const char* Filename, void** OutData, int32* OutSize)
{
	FILE* f = NULL;
	fopen_s(&f, Filename, "rb");

	fseek(f, 0, SEEK_END);
	int32 TotalSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	void* FileData = malloc(TotalSize);

	fread(FileData, 1, TotalSize, f);

	*OutData = FileData;
	*OutSize = TotalSize;

	fclose(f);
}
