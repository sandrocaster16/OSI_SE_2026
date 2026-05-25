#include <sys/mman.h>

void* custom_malloc(size_t size);
void custom_free(void* ptr);
void* custom_calloc(size_t nmemb, size_t size);
void* custom_realloc(void* ptr, size_t size);