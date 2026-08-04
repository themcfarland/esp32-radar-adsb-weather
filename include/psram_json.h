#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ArduinoJson allocator backed by the board's 8 MB OPI PSRAM.  Large HTTP
// responses therefore do not consume the internal DRAM needed by the RGB
// bounce buffers, Wi-Fi stack and LVGL task.
struct PsramJsonAllocator {
  void* allocate(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  void deallocate(void* pointer) {
    heap_caps_free(pointer);
  }

  void* reallocate(void* pointer, size_t newSize) {
    return heap_caps_realloc(pointer, newSize,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

using PsramJsonDocument = BasicJsonDocument<PsramJsonAllocator>;
