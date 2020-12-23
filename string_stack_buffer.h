
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

template<int capacity>
struct StringStackBuffer {
	char buffer[capacity];
	int length;

	StringStackBuffer() {
		buffer[0] = '\0';
		length = 0;
	}

	StringStackBuffer(const char* format, ...) {
		buffer[0] = '\0';
		length = 0;
		va_list varArgs;
		va_start(varArgs, format);
		length += vsnprintf(buffer, capacity, format, varArgs);
		va_end(varArgs);
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
