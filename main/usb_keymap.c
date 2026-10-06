// usb_keymap.c — carattere → tasti, per ogni layout della tastiera del PC (vedi .h).
// Riferimento: i layout standard di Windows. Prima i caratteri ASCII (password, codici),
// poi le lettere locali; le lettere accentate senza tasto proprio passano dal tasto morto.
#include "usb_keymap.h"
#include "settings.h"

#define SH KM_SHIFT
#define AG KM_ALTGR

static bool put(kb_stroke_t *o, uint8_t mod, uint8_t key)
{
    *o = (kb_stroke_t){.mod = mod, .key = key};
    return true;
}

// simbolo di un tasto morto da solo: il tasto e poi uno spazio
static bool dead(kb_stroke_t *o, uint8_t mod, uint8_t key)
{
    *o = (kb_stroke_t){.mod = mod, .key = key, .space = true};
    return true;
}

// lettera accentata: tasto morto (accento) e poi la lettera
static bool comp(kb_stroke_t *o, uint8_t pmod, uint8_t pkey, uint8_t mod, uint8_t key)
{
    *o = (kb_stroke_t){.pmod = pmod, .pkey = pkey, .mod = mod, .key = key};
    return true;
}

// spazio, a capo, tabulazione: uguali ovunque
static bool common(uint32_t c, kb_stroke_t *o)
{
    switch (c) {
    case ' ':  return put(o, 0, K_SPACE);
    case '\n': return put(o, 0, K_ENTER);
    case '\t': return put(o, 0, K_TAB);
    }
    return false;
}

// lettera a-z/A-Z sul tasto fisico `k`
static bool letter(uint32_t c, uint8_t k, kb_stroke_t *o) { return put(o, c >= 'A' && c <= 'Z' ? SH : 0, k); }

static uint8_t qwerty(uint32_t c) { return K_A + ((c | 0x20) - 'a'); }

/* ---------------- US ---------------- */

static bool us(uint32_t c, kb_stroke_t *o)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return letter(c, qwerty(c), o);
    if (c >= '1' && c <= '9') return put(o, 0, K_1 + (c - '1'));
    switch (c) {
    case '0': return put(o, 0, K_0);
    case '-': return put(o, 0, K_MINUS);      case '_': return put(o, SH, K_MINUS);
    case '=': return put(o, 0, K_EQUAL);      case '+': return put(o, SH, K_EQUAL);
    case '[': return put(o, 0, K_LBRACKET);   case '{': return put(o, SH, K_LBRACKET);
    case ']': return put(o, 0, K_RBRACKET);   case '}': return put(o, SH, K_RBRACKET);
    case '\\': return put(o, 0, K_BACKSLASH); case '|': return put(o, SH, K_BACKSLASH);
    case ';': return put(o, 0, K_SEMICOLON);  case ':': return put(o, SH, K_SEMICOLON);
    case '\'': return put(o, 0, K_APOSTROPHE); case '"': return put(o, SH, K_APOSTROPHE);
    case '`': return put(o, 0, K_GRAVE);      case '~': return put(o, SH, K_GRAVE);
    case ',': return put(o, 0, K_COMMA);      case '<': return put(o, SH, K_COMMA);
    case '.': return put(o, 0, K_PERIOD);     case '>': return put(o, SH, K_PERIOD);
    case '/': return put(o, 0, K_SLASH);      case '?': return put(o, SH, K_SLASH);
    case '!': return put(o, SH, K_1); case '@': return put(o, SH, K_2); case '#': return put(o, SH, K_3);
    case '$': return put(o, SH, K_4); case '%': return put(o, SH, K_5); case '^': return put(o, SH, K_6);
    case '&': return put(o, SH, K_7); case '*': return put(o, SH, K_8); case '(': return put(o, SH, K_9);
    case ')': return put(o, SH, K_0);
    }
    return false;
}

/* ---------------- US internazionale ---------------- */
// come US, ma ' " ` ~ ^ sono tasti morti; le lettere accentate stanno su AltGr

static bool us_intl(uint32_t c, kb_stroke_t *o)
{
    switch (c) {
    case '\'': return dead(o, 0, K_APOSTROPHE);
    case '"':  return dead(o, SH, K_APOSTROPHE);
    case '`':  return dead(o, 0, K_GRAVE);
    case '~':  return dead(o, SH, K_GRAVE);
    case '^':  return dead(o, SH, K_6);
    case 0xE1: return put(o, AG, K_A);       case 0xC1: return put(o, AG | SH, K_A);   // á Á
    case 0xE9: return put(o, AG, K_E);       case 0xC9: return put(o, AG | SH, K_E);   // é É
    case 0xED: return put(o, AG, K_I);       case 0xCD: return put(o, AG | SH, K_I);   // í Í
    case 0xF3: return put(o, AG, K_O);       case 0xD3: return put(o, AG | SH, K_O);   // ó Ó
    case 0xFA: return put(o, AG, K_U);       case 0xDA: return put(o, AG | SH, K_U);   // ú Ú
    case 0xF1: return put(o, AG, K_N);       case 0xD1: return put(o, AG | SH, K_N);   // ñ Ñ
    case 0xFC: return put(o, AG, K_Y);       case 0xDC: return put(o, AG | SH, K_Y);   // ü Ü
    case 0xE4: return put(o, AG, K_Q);       case 0xC4: return put(o, AG | SH, K_Q);   // ä Ä
    case 0xF6: return put(o, AG, K_P);       case 0xD6: return put(o, AG | SH, K_P);   // ö Ö
    case 0xE7: return put(o, AG, K_COMMA);   case 0xC7: return put(o, AG | SH, K_COMMA); // ç Ç
    case 0xDF: return put(o, AG, K_S);       // ß
    case 0xA1: return put(o, AG, K_1);       // ¡
    case 0xBF: return put(o, AG, K_SLASH);   // ¿
    case 0x20AC: return put(o, AG, K_5);     // €
    case 0xE0: return comp(o, 0, K_GRAVE, 0, K_A);   // à
    case 0xE8: return comp(o, 0, K_GRAVE, 0, K_E);   // è
    case 0xEC: return comp(o, 0, K_GRAVE, 0, K_I);   // ì
    case 0xF2: return comp(o, 0, K_GRAVE, 0, K_O);   // ò
    case 0xF9: return comp(o, 0, K_GRAVE, 0, K_U);   // ù
    }
    return us(c, o);
}

/* ---------------- Regno Unito ---------------- */

static bool uk(uint32_t c, kb_stroke_t *o)
{
    switch (c) {
    case '"':  return put(o, SH, K_2);
    case '@':  return put(o, SH, K_APOSTROPHE);
    case '#':  return put(o, 0, K_BACKSLASH);     // tasto accanto a Invio
    case '~':  return put(o, SH, K_BACKSLASH);
    case '\\': return put(o, 0, K_EUR2);
    case '|':  return put(o, SH, K_EUR2);
    case 0xA3: return put(o, SH, K_3);            // £
    case 0xAC: return put(o, SH, K_GRAVE);        // ¬
    case 0x20AC: return put(o, AG, K_4);          // €
    }
    return us(c, o);
}

/* ---------------- Italiano ---------------- */

static bool it(uint32_t c, kb_stroke_t *o)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return letter(c, qwerty(c), o);
    if (c >= '1' && c <= '9') return put(o, 0, K_1 + (c - '1'));
    switch (c) {
    case '0':  return put(o, 0, K_0);
    case '!':  return put(o, SH, K_1); case '"': return put(o, SH, K_2); case 0xA3: return put(o, SH, K_3); // £
    case '$':  return put(o, SH, K_4); case '%': return put(o, SH, K_5); case '&': return put(o, SH, K_6);
    case '/':  return put(o, SH, K_7); case '(': return put(o, SH, K_8); case ')': return put(o, SH, K_9);
    case '=':  return put(o, SH, K_0);
    case '\'': return put(o, 0, K_MINUS);      case '?': return put(o, SH, K_MINUS);
    case 0xEC: return put(o, 0, K_EQUAL);      case '^': return put(o, SH, K_EQUAL);       // ì
    case 0xE8: return put(o, 0, K_LBRACKET);   case 0xE9: return put(o, SH, K_LBRACKET);   // è é
    case '[':  return put(o, AG, K_LBRACKET);  case '{': return put(o, AG | SH, K_LBRACKET);
    case '+':  return put(o, 0, K_RBRACKET);   case '*': return put(o, SH, K_RBRACKET);
    case ']':  return put(o, AG, K_RBRACKET);  case '}': return put(o, AG | SH, K_RBRACKET);
    case 0xF2: return put(o, 0, K_SEMICOLON);  case 0xE7: return put(o, SH, K_SEMICOLON);  // ò ç
    case '@':  return put(o, AG, K_SEMICOLON);
    case 0xE0: return put(o, 0, K_APOSTROPHE); case 0xB0: return put(o, SH, K_APOSTROPHE); // à °
    case '#':  return put(o, AG, K_APOSTROPHE);
    case 0xF9: return put(o, 0, K_BACKSLASH);  case 0xA7: return put(o, SH, K_BACKSLASH);  // ù §
    case '\\': return put(o, 0, K_GRAVE);      case '|': return put(o, SH, K_GRAVE);
    case '<':  return put(o, 0, K_EUR2);       case '>': return put(o, SH, K_EUR2);
    case ',':  return put(o, 0, K_COMMA);      case ';': return put(o, SH, K_COMMA);
    case '.':  return put(o, 0, K_PERIOD);     case ':': return put(o, SH, K_PERIOD);
    case '-':  return put(o, 0, K_SLASH);      case '_': return put(o, SH, K_SLASH);
    case 0x20AC: return put(o, AG, K_E);       // €
    }
    return false;   // ~ e ` non ci sono sulla tastiera italiana
}

/* ---------------- Tedesco (QWERTZ) ---------------- */

static bool de(uint32_t c, kb_stroke_t *o)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        uint32_t l = c | 0x20;
        return letter(c, l == 'y' ? K_Z : l == 'z' ? K_Y : qwerty(c), o);   // Y e Z scambiate
    }
    if (c >= '1' && c <= '9') return put(o, 0, K_1 + (c - '1'));
    switch (c) {
    case '0':  return put(o, 0, K_0);
    case '!':  return put(o, SH, K_1); case '"': return put(o, SH, K_2); case 0xA7: return put(o, SH, K_3); // §
    case '$':  return put(o, SH, K_4); case '%': return put(o, SH, K_5); case '&': return put(o, SH, K_6);
    case '/':  return put(o, SH, K_7); case '(': return put(o, SH, K_8); case ')': return put(o, SH, K_9);
    case '=':  return put(o, SH, K_0);
    case 0xB2: return put(o, AG, K_2); case 0xB3: return put(o, AG, K_3);   // ² ³
    case '{':  return put(o, AG, K_7); case '[': return put(o, AG, K_8);
    case ']':  return put(o, AG, K_9); case '}': return put(o, AG, K_0);
    case 0xDF: return put(o, 0, K_MINUS);      case '?': return put(o, SH, K_MINUS);   // ß
    case '\\': return put(o, AG, K_MINUS);
    case '`':  return dead(o, SH, K_EQUAL);
    case '^':  return dead(o, 0, K_GRAVE);     case 0xB0: return put(o, SH, K_GRAVE);  // °
    case 0xFC: return put(o, 0, K_LBRACKET);   case 0xDC: return put(o, SH, K_LBRACKET); // ü Ü
    case '+':  return put(o, 0, K_RBRACKET);   case '*': return put(o, SH, K_RBRACKET);
    case '~':  return put(o, AG, K_RBRACKET);
    case 0xF6: return put(o, 0, K_SEMICOLON);  case 0xD6: return put(o, SH, K_SEMICOLON); // ö Ö
    case 0xE4: return put(o, 0, K_APOSTROPHE); case 0xC4: return put(o, SH, K_APOSTROPHE); // ä Ä
    case '#':  return put(o, 0, K_BACKSLASH);  case '\'': return put(o, SH, K_BACKSLASH);
    case '<':  return put(o, 0, K_EUR2);       case '>': return put(o, SH, K_EUR2);
    case '|':  return put(o, AG, K_EUR2);
    case ',':  return put(o, 0, K_COMMA);      case ';': return put(o, SH, K_COMMA);
    case '.':  return put(o, 0, K_PERIOD);     case ':': return put(o, SH, K_PERIOD);
    case '-':  return put(o, 0, K_SLASH);      case '_': return put(o, SH, K_SLASH);
    case '@':  return put(o, AG, K_Q);
    case 0x20AC: return put(o, AG, K_E);       // €
    case 0xB5: return put(o, AG, K_M);         // µ
    }
    return false;
}

/* ---------------- Francese (AZERTY) ---------------- */

static uint8_t azerty(uint32_t c)
{
    switch (c | 0x20) {
    case 'a': return K_Q;
    case 'q': return K_A;
    case 'z': return K_W;
    case 'w': return K_Z;
    case 'm': return K_SEMICOLON;
    default:  return qwerty(c);
    }
}

static bool fr(uint32_t c, kb_stroke_t *o)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return letter(c, azerty(c), o);
    if (c >= '1' && c <= '9') return put(o, SH, K_1 + (c - '1'));   // le cifre vogliono Shift
    switch (c) {
    case '0':  return put(o, SH, K_0);
    case '&':  return put(o, 0, K_1); case 0xE9: return put(o, 0, K_2);   // é
    case '"':  return put(o, 0, K_3); case '\'': return put(o, 0, K_4);
    case '(':  return put(o, 0, K_5); case '-':  return put(o, 0, K_6);
    case 0xE8: return put(o, 0, K_7); case '_':  return put(o, 0, K_8);   // è
    case 0xE7: return put(o, 0, K_9); case 0xE0: return put(o, 0, K_0);   // ç à
    case '~':  return dead(o, AG, K_2);
    case '#':  return put(o, AG, K_3); case '{': return put(o, AG, K_4);
    case '[':  return put(o, AG, K_5); case '|': return put(o, AG, K_6);
    case '`':  return dead(o, AG, K_7);
    case '\\': return put(o, AG, K_8); case '@': return put(o, AG, K_0);
    case ')':  return put(o, 0, K_MINUS);      case 0xB0: return put(o, SH, K_MINUS);   // °
    case ']':  return put(o, AG, K_MINUS);
    case '=':  return put(o, 0, K_EQUAL);      case '+': return put(o, SH, K_EQUAL);
    case '}':  return put(o, AG, K_EQUAL);
    case '^':  return dead(o, 0, K_LBRACKET);
    case '$':  return put(o, 0, K_RBRACKET);   case 0xA3: return put(o, SH, K_RBRACKET); // £
    case 0xF9: return put(o, 0, K_APOSTROPHE); case '%': return put(o, SH, K_APOSTROPHE); // ù
    case '*':  return put(o, 0, K_BACKSLASH);  case 0xB5: return put(o, SH, K_BACKSLASH); // µ
    case '<':  return put(o, 0, K_EUR2);       case '>': return put(o, SH, K_EUR2);
    case ',':  return put(o, 0, K_M);          case '?': return put(o, SH, K_M);
    case ';':  return put(o, 0, K_COMMA);      case '.': return put(o, SH, K_COMMA);
    case ':':  return put(o, 0, K_PERIOD);     case '/': return put(o, SH, K_PERIOD);
    case '!':  return put(o, 0, K_SLASH);      case 0xA7: return put(o, SH, K_SLASH);   // §
    case 0xB2: return put(o, 0, K_GRAVE);      // ²
    case 0x20AC: return put(o, AG, K_E);       // €
    // ^ e ¨ morti sul tasto [ (accanto alla P)
    case 0xE2: return comp(o, 0, K_LBRACKET, 0, azerty('a'));   // â
    case 0xEA: return comp(o, 0, K_LBRACKET, 0, K_E);           // ê
    case 0xEE: return comp(o, 0, K_LBRACKET, 0, K_I);           // î
    case 0xF4: return comp(o, 0, K_LBRACKET, 0, K_O);           // ô
    case 0xFB: return comp(o, 0, K_LBRACKET, 0, K_U);           // û
    case 0xEB: return comp(o, SH, K_LBRACKET, 0, K_E);          // ë
    case 0xEF: return comp(o, SH, K_LBRACKET, 0, K_I);          // ï
    case 0xFC: return comp(o, SH, K_LBRACKET, 0, K_U);          // ü
    }
    return false;
}

/* ---------------- Spagnolo ---------------- */

static bool es(uint32_t c, kb_stroke_t *o)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return letter(c, qwerty(c), o);
    if (c >= '1' && c <= '9') return put(o, 0, K_1 + (c - '1'));
    switch (c) {
    case '0':  return put(o, 0, K_0);
    case '!':  return put(o, SH, K_1); case '"': return put(o, SH, K_2); case 0xB7: return put(o, SH, K_3); // ·
    case '$':  return put(o, SH, K_4); case '%': return put(o, SH, K_5); case '&': return put(o, SH, K_6);
    case '/':  return put(o, SH, K_7); case '(': return put(o, SH, K_8); case ')': return put(o, SH, K_9);
    case '=':  return put(o, SH, K_0);
    case '|':  return put(o, AG, K_1); case '@': return put(o, AG, K_2); case '#': return put(o, AG, K_3);
    case '~':  return put(o, AG, K_4); case 0xAC: return put(o, AG, K_6);   // ¬
    case 0x20AC: return put(o, AG, K_E);       // €
    case '\'': return put(o, 0, K_MINUS);      case '?': return put(o, SH, K_MINUS);
    case 0xA1: return put(o, 0, K_EQUAL);      case 0xBF: return put(o, SH, K_EQUAL);   // ¡ ¿
    case 0xBA: return put(o, 0, K_GRAVE);      case 0xAA: return put(o, SH, K_GRAVE);   // º ª
    case '\\': return put(o, AG, K_GRAVE);
    case '`':  return dead(o, 0, K_LBRACKET);  case '^': return dead(o, SH, K_LBRACKET);
    case '[':  return put(o, AG, K_LBRACKET);
    case '+':  return put(o, 0, K_RBRACKET);   case '*': return put(o, SH, K_RBRACKET);
    case ']':  return put(o, AG, K_RBRACKET);
    case 0xF1: return put(o, 0, K_SEMICOLON);  case 0xD1: return put(o, SH, K_SEMICOLON); // ñ Ñ
    case '{':  return put(o, AG, K_APOSTROPHE);
    case 0xE7: return put(o, 0, K_BACKSLASH);  case 0xC7: return put(o, SH, K_BACKSLASH); // ç Ç
    case '}':  return put(o, AG, K_BACKSLASH);
    case '<':  return put(o, 0, K_EUR2);       case '>': return put(o, SH, K_EUR2);
    case ',':  return put(o, 0, K_COMMA);      case ';': return put(o, SH, K_COMMA);
    case '.':  return put(o, 0, K_PERIOD);     case ':': return put(o, SH, K_PERIOD);
    case '-':  return put(o, 0, K_SLASH);      case '_': return put(o, SH, K_SLASH);
    // ´ e ¨ morti sul tasto accanto alla Ñ, ` sul tasto accanto alla P
    case 0xE1: return comp(o, 0, K_APOSTROPHE, 0, K_A);   // á
    case 0xE9: return comp(o, 0, K_APOSTROPHE, 0, K_E);   // é
    case 0xED: return comp(o, 0, K_APOSTROPHE, 0, K_I);   // í
    case 0xF3: return comp(o, 0, K_APOSTROPHE, 0, K_O);   // ó
    case 0xFA: return comp(o, 0, K_APOSTROPHE, 0, K_U);   // ú
    case 0xFC: return comp(o, SH, K_APOSTROPHE, 0, K_U);  // ü
    case 0xE0: return comp(o, 0, K_LBRACKET, 0, K_A);     // à
    case 0xE8: return comp(o, 0, K_LBRACKET, 0, K_E);     // è
    case 0xF2: return comp(o, 0, K_LBRACKET, 0, K_O);     // ò
    }
    return false;
}

bool kb_map(uint32_t cp, int layout, kb_stroke_t *out)
{
    if (common(cp, out)) return true;
    switch (layout) {
    case KB_LAYOUT_IT:      return it(cp, out);
    case KB_LAYOUT_US:      return us(cp, out);
    case KB_LAYOUT_US_INTL: return us_intl(cp, out);
    case KB_LAYOUT_UK:      return uk(cp, out);
    case KB_LAYOUT_DE:      return de(cp, out);
    case KB_LAYOUT_FR:      return fr(cp, out);
    case KB_LAYOUT_ES:      return es(cp, out);
    default:                return us(cp, out);
    }
}
