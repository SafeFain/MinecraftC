#include "core/AllocationBudget.h"
#define STBI_MALLOC(size) AllocationBudget::allocate(size, AllocationBudget::decoder)
#define STBI_REALLOC(pointer, size) AllocationBudget::resize(pointer, size, AllocationBudget::decoder)
#define STBI_FREE(pointer) AllocationBudget::release(pointer)
#define STB_IMAGE_IMPLEMENTATION
#if defined(_WIN32)
#  define STBI_WINDOWS_UTF8
#endif
#include <stb_image.h>
