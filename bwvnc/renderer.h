#ifndef RENDERER_H
#define RENDERER_H

#include "microui.h"
#include <SDL2/SDL.h>

typedef void (*ui_event_callback)(mu_Context *ctx, SDL_Event *e);

void r_init(SDL_Window * sdlwin, SDL_Renderer* sdlrend);
void r_clean(void);
void r_init_w(int width, int height);
void r_clean_w(void);
void r_set_clip_rect(mu_Rect rect);
void r_draw_rect(mu_Rect rect, mu_Color color);
void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color);
void r_draw_icon(int id, mu_Rect rect, mu_Color color);
void r_draw_frame(mu_Context *ctx, mu_Rect rect, int colorid);
int  r_get_text_width(mu_Font _, const char *text, int len);
int  r_get_text_height(mu_Font _);
void r_clear(mu_Color color);
void r_present(void);

int r_ui_draw(mu_Context *ctx);
int r_ui_event(mu_Context *ctx, SDL_Event *e);

#endif

