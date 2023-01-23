#include <SDL2/SDL.h>
#include "renderer.h"
#include "atlas.inl"

ui_event_callback r_ui_event_callback = NULL;

static SDL_Window  * window;
static SDL_Renderer* renderer;
static SDL_Texture * ui_font;

const SDL_Rect drag_area = {0, 0, 700, 25};

SDL_HitTestResult hit(SDL_Window* window, const SDL_Point* area, void* data) {
  if(SDL_PointInRect(area, &drag_area)) {
    return SDL_HITTEST_DRAGGABLE;
  }
  return SDL_HITTEST_NORMAL;
}

void r_init_w(int width, int height) {
  /* init SDL window */
  window = SDL_CreateWindow("bwVNC", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
  renderer = SDL_CreateRenderer(window, -1, 0);

  ui_font = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS_WIDTH, ATLAS_HEIGHT);
  SDL_UpdateTexture(ui_font, NULL, atlas_texture, ATLAS_WIDTH*4);
  SDL_SetTextureBlendMode(ui_font, SDL_BLENDMODE_BLEND);

  SDL_SetWindowHitTest(window, hit, NULL);
}

void r_init(SDL_Window* sdlwin, SDL_Renderer* sdlrend) {
  window = sdlwin;
  renderer = sdlrend;
//  ui_font = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, ATLAS_WIDTH, ATLAS_HEIGHT);
  ui_font = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS_WIDTH, ATLAS_HEIGHT);
  SDL_UpdateTexture(ui_font, NULL, atlas_texture, ATLAS_WIDTH*4);
  SDL_SetTextureBlendMode(ui_font, SDL_BLENDMODE_BLEND);
}

void r_clean(void) {
}

void r_clean_w(void) {
  SDL_DestroyWindow(window);
  SDL_DestroyRenderer(renderer);
}

void r_draw_rect(mu_Rect rect, mu_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_Rect sdl_rect = {rect.x, rect.y, rect.w, rect.h};
  SDL_RenderFillRect(renderer, &sdl_rect);
}

void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
    SDL_Rect dst = { pos.x, pos.y, 0, 0 };
    for (const char *p = text; *p; p++) {
        if ((*p & 0xc0) == 0x80) { continue; }
        int chr = mu_min((unsigned char) *p, 127);
        SDL_Rect *src = (SDL_Rect*) &atlas[ATLAS_FONT + chr];

        dst.w = src->w;
        dst.h = src->h;

        SDL_SetTextureColorMod(ui_font, color.r, color.g, color.b);
        SDL_RenderCopy(renderer, ui_font, src, &dst);

        dst.x += dst.w;
    }
}

void r_draw_icon(int id, mu_Rect rect, mu_Color color) {
    SDL_Rect *src = (SDL_Rect*) &atlas[id];
    SDL_Rect dst = {
        .x = rect.x + (rect.w - src->w) / 2,
        .y = rect.y + (rect.h - src->h) / 2,
        .w = src->w,
        .h = src->h
    };

    SDL_SetTextureColorMod(ui_font, color.r, color.g, color.b);
    SDL_RenderCopy(renderer, ui_font, src, &dst);
};

void r_draw_frame(mu_Context *ctx, mu_Rect rect, int colorid) {
    mu_Color color;
    if (colorid >= MU_COLOR_MAX || colorid < 0) {
        color = mu_color(
            (colorid >> 24) & 0xFF, 
            (colorid >> 16) & 0xFF,
            (colorid >>  8) & 0xFF,
            colorid & 0xFF
        );
    } else {
        color = ctx->style->colors[colorid];
    }

    mu_Color border = ctx->style->colors[MU_COLOR_BORDER];
    
    if (!(colorid == MU_COLOR_SCROLLBASE  ||
        colorid == MU_COLOR_SCROLLTHUMB ||
        colorid == MU_COLOR_TITLEBG) || border.a) {

        mu_draw_rect(ctx, 
            mu_rect(rect.x-1, rect.y, rect.w+2, rect.h+1), 
            border
        );

        mu_draw_rect(ctx, 
            mu_rect(rect.x, rect.y-1, rect.w, rect.h+3), 
            border
        );
    }

    mu_draw_rect(ctx, rect, color);
    mu_draw_box(ctx, rect, mu_color(255, 255, 255, 10));
}

int r_get_text_width(mu_Font _, const char *text, int len) {
    int res = 0;
    for (const char *p = text; *p && len--; p++) {
        if ((*p & 0xc0) == 0x80) { continue; }
        int chr = mu_min((unsigned char) *p, 127);

        res += atlas[ATLAS_FONT + chr].w;
    }
    return res;
}

int r_get_text_height(mu_Font _) {
  return 18;
}

void r_set_clip_rect(mu_Rect rect) {
    SDL_Rect *_rect = (SDL_Rect*) &rect;
    if (rect.w == 0x1000000)
        _rect = NULL;

    SDL_RenderSetClipRect(renderer, _rect);
}

void r_clear(mu_Color clr) {
  SDL_SetRenderDrawColor(renderer, clr.r,clr.g,clr.b,clr.a);
  SDL_RenderClear(renderer);
}

void r_present(void) {
  SDL_RenderPresent(renderer);
}

const char button_map[256] = {
    [ SDL_BUTTON_LEFT   & 0xff ] =  MU_MOUSE_LEFT,
    [ SDL_BUTTON_RIGHT  & 0xff ] =  MU_MOUSE_RIGHT,
    [ SDL_BUTTON_MIDDLE & 0xff ] =  MU_MOUSE_MIDDLE,
};

const char key_map[256] = {
    [ SDLK_LSHIFT    & 0xff ] = MU_KEY_SHIFT,
    [ SDLK_RSHIFT    & 0xff ] = MU_KEY_SHIFT,
    [ SDLK_LCTRL     & 0xff ] = MU_KEY_CTRL,
    [ SDLK_RCTRL     & 0xff ] = MU_KEY_CTRL,
    [ SDLK_LALT      & 0xff ] = MU_KEY_ALT,
    [ SDLK_RALT      & 0xff ] = MU_KEY_ALT,
    [ SDLK_RETURN    & 0xff ] = MU_KEY_RETURN,
    [ SDLK_BACKSPACE & 0xff ] = MU_KEY_BACKSPACE,
};

int r_ui_event(mu_Context *ctx, SDL_Event *e) {
    if(r_ui_event_callback!=NULL)
      r_ui_event_callback(ctx, e);
    switch (e->type) {
        case SDL_QUIT: return 0; break;
        case SDL_MOUSEMOTION: mu_input_mousemove(ctx, e->motion.x, e->motion.y); break;
        case SDL_MOUSEWHEEL: mu_input_scroll(ctx, 0, e->wheel.y * -30); break;
        case SDL_TEXTINPUT: mu_input_text(ctx, e->text.text); break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int b = button_map[e->button.button & 0xff];

            if (b && e->type == SDL_MOUSEBUTTONDOWN)
                mu_input_mousedown(ctx, e->button.x, e->button.y, b);

            if (b && e->type ==   SDL_MOUSEBUTTONUP) 
                mu_input_mouseup(ctx, e->button.x, e->button.y, b);

            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int c = key_map[e->key.keysym.sym & 0xff];

            if (c && e->type == SDL_KEYDOWN) 
                mu_input_keydown(ctx, c);

            if (c && e->type == SDL_KEYUP) 
                mu_input_keyup(ctx, c);

            break;
        }
    }
    return 1;
}

int r_ui_draw(mu_Context *ctx) {
    mu_Command *cmd = NULL;
    while (mu_next_command(ctx, &cmd)) {
      switch (cmd->type) {
        case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
        case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
        case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
        case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
      }
    }

    return 0;
}
