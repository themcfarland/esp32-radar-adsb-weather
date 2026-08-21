#pragma once

#include <esp_heap_caps.h>
#include <stddef.h>

// ArduinoJson allocator that keeps large JSON DOM buffers out of scarce
// internal SRAM. This board has 8 MB OPI PSRAM and the network/TLS stack needs
// internal heap for sockets and mbedTLS handshakes.
struct PsramAllocator {
  void* allocate(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  void deallocate(void* pointer) { heap_caps_free(pointer); }

  void* reallocate(void* pointer, size_t newSize) {
    return heap_caps_realloc(pointer, newSize,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};
