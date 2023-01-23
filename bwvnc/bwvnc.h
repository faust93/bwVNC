#include <SDL.h>
#include <rfb/rfbclient.h>
#include "microui.h"
#include "renderer.h"

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

/* client's pointer position & game mode toggle */
int mouse_x, mouse_y;
/* switch to relative mouse mode & restricted keyboard input (textmode disabled) */
int game_relmode = FALSE;
int fullScreenMode = FALSE;
int isAudioEnabled = TRUE;

/* ui data */
mu_Context *ctx;
int ui_inited = FALSE;
int ui_show = FALSE;

extern ui_event_callback r_ui_event_callback;

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

const uint8_t sampleFormatU8  = 0;
const uint8_t sampleFormatS8  = 1;
const uint8_t sampleFormatU16 = 2;
const uint8_t sampleFormatS16 = 3;
const uint8_t sampleFormatU32 = 4;
const uint8_t sampleFormatS32 = 5;

#define OPUS_FMT 8
#define AUDIO_PROFILES_SZ 7
int audioProfNum = 1;

typedef struct {
  char *description;
  uint8_t nChannels;
  uint16_t bitsPerSample;
  uint8_t sampleFormat;
} aProfiles;

aProfiles audioProfile[AUDIO_PROFILES_SZ] = {{"PCM 11025Hz 16-bit stereo", 2, 11025, sampleFormatU16},
                            {"PCM 22050Hz 16-bit stereo", 2, 22050, sampleFormatU16},
                            {"PCM 44100Hz 16-bit stereo", 2, 44100, sampleFormatU16},
                            {"OPUS 22050Hz 16-bit stereo", 2, 22050, sampleFormatU16 | OPUS_FMT},
                            {"OPUS 32000Hz 16-bit stereo", 2, 22050, sampleFormatU16 | OPUS_FMT},
                            {"OPUS 44100Hz 16-bit stereo", 2, 44100, sampleFormatU16 | OPUS_FMT},
                            {"OPUS 48000Hz 16-bit stereo", 2, 48000, sampleFormatU16 | OPUS_FMT}
                            };
