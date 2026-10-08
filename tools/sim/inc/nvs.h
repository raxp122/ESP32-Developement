#pragma once
// nel simulatore (e nei test sul PC) la NVS non c'è: niente da leggere, scritture ignorate
#include <stdint.h>
#include <stddef.h>
typedef uint32_t nvs_handle_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#define NVS_READONLY 0
#define NVS_READWRITE 1
static inline int nvs_open(const char *n, int m, nvs_handle_t *h) { return -1; }
static inline int nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v) { return -1; }
static inline int nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) { return -1; }
static inline int nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *l) { return -1; }
static inline int nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t l) { return -1; }
static inline int nvs_commit(nvs_handle_t h) { return -1; }
static inline void nvs_close(nvs_handle_t h) {}
