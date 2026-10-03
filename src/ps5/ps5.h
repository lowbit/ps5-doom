#ifndef PS5_H
#define PS5_H

#include <stddef.h>
#include <stdint.h>

int ps5_user(void);
int ps5_frame(void);
int ps5_test_cpu_present(void);
void *ps5_gpu_memory(size_t bytes);
void ps5_cache_flush(const volatile void *address, size_t bytes);
void ps5_pad_open(void);
void ps5_pad_close(void);
void ps5_capture_serve(int port);
void ps5_capture_add(const char *name, const void *data, size_t size);
void ps5_capture_drain(uint32_t timeout_us);

#endif
