// textnorm.c
#include "textnorm.h"
#include <ctype.h>

void text_norm(const char *in, char *out, size_t n)
{
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)in;
    while (*s && o + 1 < n) {
        unsigned char c = *s++;
        if (c < 0x80) { out[o++] = (char)tolower(c); continue; }
        if (c == 0xC3 && *s) {                       // lettere accentate latine (U+00C0–U+00FF)
            unsigned char d = *s++ | 0x20;           // forza minuscolo
            char r = 0;
            if (d >= 0xA0 && d <= 0xA5) r = 'a';
            else if (d == 0xA7) r = 'c';
            else if (d >= 0xA8 && d <= 0xAB) r = 'e';
            else if (d >= 0xAC && d <= 0xAF) r = 'i';
            else if (d == 0xB1) r = 'n';
            else if ((d >= 0xB2 && d <= 0xB6) || d == 0xB8) r = 'o';
            else if (d >= 0xB9 && d <= 0xBC) r = 'u';
            if (r) out[o++] = r;
            continue;
        }
        // altri caratteri multibyte (icone, simboli): saltali
        while ((*s & 0xC0) == 0x80) s++;
    }
    out[o] = 0;
}
