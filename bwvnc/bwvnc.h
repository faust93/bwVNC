#include <SDL.h>
#include <rfb/rfbclient.h>
#include "microui.h"

#define RESIZE_DESKTOP 0
#define RESIZE_ZOOM 1

#define DEFAULT_RENDER_B "opengl"

/* logging */
#define LOG_SIZE 64000
static char logbuf[LOG_SIZE];
int logbuf_updated = FALSE;
int logging_enabled = TRUE;

/* rfb client global struct */
rfbClient* cl;
static rfbKeySym psym;
static int rightAltKeyDown, leftAltKeyDown;

static int enableResizable = 1, resizeMethod = RESIZE_DESKTOP, viewOnly, listenLoop, buttonMask;

int sdlFlags;
SDL_Texture *sdlTexture;
SDL_Renderer *sdlRenderer;
SDL_Window *sdlWindow;
char *SDLrenderDriver = NULL;

/* client's pointer position & relative mode toggle */
int mouse_x, mouse_y;
int mouse_relmode = FALSE;

int isAudioEnabled = TRUE;

/* ui data */
mu_Context *ctx;
int ui_inited = FALSE;
int ui_show = FALSE;

struct { int sdl; int rfb; } buttonMapping[]={
	{1, rfbButton1Mask},
	{2, rfbButton2Mask},
	{3, rfbButton3Mask},
	{4, rfbButton4Mask},
	{5, rfbButton5Mask},
	{0,0}
};

struct { char mask; int bits_stored; } utf8Mapping[]= {
	{0b00111111, 6},
	{0b01111111, 7},
	{0b00011111, 5},
	{0b00001111, 4},
	{0b00000111, 3},
	{0,0}
};