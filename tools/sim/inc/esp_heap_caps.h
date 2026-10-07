#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_INTERNAL 0
#define heap_caps_malloc(n,c) malloc(n)
#define heap_caps_calloc(a,b,c) calloc(a,b)
#define heap_caps_get_free_size(c) 1000000
#define heap_caps_get_largest_free_block(c) 1000000
