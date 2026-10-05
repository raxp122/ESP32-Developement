// rg_system.h — shim minimo per compilare prboom (da retro-go) senza retro-go
#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#define RG_LOG_PRINTF 0
static inline void rg_system_vlog(int level, const char *ctx, const char *fmt, va_list ap) { vprintf(fmt, ap); }
void gadget_doom_panic(const char *msg);
#define RG_PANIC(msg) gadget_doom_panic(msg)
