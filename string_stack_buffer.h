
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

//template<int capacity>
struct StringBuffer {
	char* buffer = nullptr;
	int capacity = 0;
	int length = 0;

	explicit StringBuffer(int _capacity) {
		capacity = _capacity;
		buffer = (char*)malloc(capacity);
		buffer[0] = '\0';
		length = 0;
	}

	~StringBuffer()
	{
		free(buffer);
		buffer = nullptr;
	}

	void Append(const char* str) {
		length += snprintf(&buffer[length], capacity - length, "%s", str);
	}

	void AppendFormat(const char* format, ...) {
		va_list varArgs;
		va_start(varArgs, format);
		length += vsnprintf(&buffer[length], capacity - length, format, varArgs);
		va_end(varArgs);
	}
};
