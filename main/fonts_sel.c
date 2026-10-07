// fonts_sel.c — sceglie la serie di caratteri per la scheda (vedi ui.h). Non include ui.h:
// lì i nomi font_s… sono macro che puntano alla serie scelta.
#include "lvgl.h"
#include "board.h"

extern const lv_font_t font_s, font_m, font_l, font_xl, font_icon;               // 3.49
extern const lv_font_t font_s_r, font_m_r, font_l_r, font_xl_r, font_icon_r;     // tondo

const lv_font_t *ui_font_s = &font_s, *ui_font_m = &font_m, *ui_font_l = &font_l,
                *ui_font_xl = &font_xl, *ui_font_icon = &font_icon;

void ui_fonts_init(void)
{
    if (!BOARD_IS_ROUND()) return;
    ui_font_s = &font_s_r;
    ui_font_m = &font_m_r;
    ui_font_l = &font_l_r;
    ui_font_xl = &font_xl_r;
    ui_font_icon = &font_icon_r;
}
