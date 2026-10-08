// pet_mini.c — il Polipetto in piccolo per altre schermate (l'Orologio): fondale del
// momento e polipetto che passeggia, dorme o chiama, senza comandi.
#include "pet_ui.h"
#include "esp_random.h"

void pet_mini_draw(uint16_t *buf, int lw, int lh, int sc, int stride, uint32_t ms)
{
    static float x = 10;
    static int target = 10;
    static uint32_t next;
    const pet_t *p = pet_get();
    art_begin(buf, lw, lh, sc, stride);
    pu_env();
    pu_look_self();
    art_brightness(p->asleep && p->light_off ? 90 : 255);
    art_background(ms / 100);
    int floor = FLOOR;   // il fondale è sempre alto 32 pixel logici
    if (p->stage == PET_EGG) {
        art_egg((lw - EGG_W) / 2, floor - EGG_H + 1, 0);
    } else if (pet_core_alive(p)) {
        int w, h;
        art_pet_size(p->stage, p->form, &w, &h);
        int maxx = lw - w - 1;
        if (!p->asleep) {
            if ((int32_t)(ms - next) >= 0) { target = 1 + esp_random() % (maxx > 1 ? maxx : 1); next = ms + 3000 + esp_random() % 4000; }
            if (x < target - 0.2f) x += 0.4f;
            else if (x > target + 0.2f) x -= 0.4f;
        }
        if (x > maxx) x = maxx;
        expr_t e = p->asleep ? EXPR_SLEEP : p->sick ? EXPR_SICK : p->needs ? EXPR_SAD : (ms % 4000 < 160 ? EXPR_BLINK : EXPR_NORMAL);
        int bob = !p->asleep && (ms / 700) & 1 ? -1 : 0;
        art_pet(p->stage, p->form, (int)x, floor - h + 1 + bob, e, ms / 400, x > target, 0, p->sick ? TINT_SICK : TINT_NONE);
        art_brightness(255);
        if (p->asleep) art_sprite(&SPR_Z, (int)x + w + 1, floor - h - (int)(ms / 600) % 3 * 2, false);
        else if (p->needs && (ms / 500) & 1) art_sprite(&SPR_BANG, (int)x + w + 1, floor - h, false);
    }
    art_brightness(255);
    art_look(NULL);
}
