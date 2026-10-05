// textnorm.h — minuscole senza accenti (UTF-8 → ASCII), per ordinare e cercare
#pragma once
#include <stddef.h>
void text_norm(const char *in, char *out, size_t n);
