#include <stdint.h>

#define ASSERT(cond) do { if (!(cond)) { char output[1024] = {}; snprintf(output, sizeof(output), "[%s:%d] Assertion failed '%s'\n", __FILE__, __LINE__, #cond); OutputDebugStringA(output); DebugBreak(); } } while(0)

#define LOG(fmt, ...) do { char output[2048] = {}; snprintf(output, sizeof(output), fmt "\n", ## __VA_ARGS__); OutputDebugStringA(output); } while(0)


using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
using int64 = int64_t;

