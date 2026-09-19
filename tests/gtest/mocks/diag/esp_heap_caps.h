#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM 0x01
#define MALLOC_CAP_8BIT 0x02

void *heap_caps_calloc(size_t count, size_t size, unsigned int caps);
void heap_caps_free(void *ptr);