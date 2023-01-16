#include <SDL.h>
#include <signal.h>
#include <rfb/rfbclient.h>
#include "keysym2ucs.h"
#include "bwvnc.h"
#include "microui.h"
#include "renderer.h"

/* UI start */
char *humanSize(uint64_t bytes, char *hrbytes)
{
    char   *suffix[] = { "B", "KB", "MB", "GB", "TB" };
    char    length = sizeof(suffix) / sizeof(suffix[0]);
    int     i;
	double dblBytes = bytes;

	if (bytes > 1024) {
		for (i = 0; (bytes / 1024) > 0 && i<length-1; i++, bytes /= 1024)
			dblBytes = bytes / 1024.0;
	}
    snprintf(hrbytes, BUFSIZ, "%.02lf %s", dblBytes, suffix[i]);
    return(hrbytes);
}

static void config_window(mu_Context *ctx)
{
	if (mu_begin_window_ex(ctx, "bwVNC", mu_rect((cl->width-300)/2, 0, 300, 240), MU_OPT_NOCLOSE)) {
		//mu_Container *cnt = mu_get_current_container(ctx);
		//cnt->rect.x = mouse_x;
		//cnt->rect.y = mouse_y;
		if (mu_header_ex(ctx, "Configuration", MU_OPT_EXPANDED)) {
			mu_layout_row(ctx, 1, (int[]) { -1 }, 0);
			mu_checkbox(ctx, "Relative mouse mode", &mouse_relmode);
			if(mu_checkbox(ctx, "Audio enabled", &isAudioEnabled)){
				cl->audioEnable = isAudioEnabled;
				SendQemuAudioOnOff(cl, isAudioEnabled ? 0 : 1);
			}
			mu_checkbox(ctx, "Zoom resize mode", &resizeMethod);
		}
		if (mu_header(ctx, "Connection Info")) {
			char buf[BUFSIZ];
			if (mu_begin_treenode(ctx, "Client stats")) {
				mu_layout_row(ctx, 2, (int[]) { 118, -1 }, 0);
				mu_label(ctx, "Audio bytes received");
				mu_label(ctx, humanSize(cl->clientStats.audioBytesRx, buf));
				mu_label(ctx, "Audio pending buffer");
				mu_label(ctx, humanSize(cl->clientStats.audioPendingBytes, buf));
				mu_label(ctx, "Audio pending ms");
				sprintf(buf,"%d ms",cl->clientStats.audioPendingMs);
				mu_label(ctx, buf);
				mu_label(ctx, "h264 bytes received");
				mu_label(ctx, humanSize(cl->clientStats.h264BytesRx, buf));
				mu_label(ctx, "h264 frames received");
				sprintf(buf,"%d",cl->clientStats.h264FramesRx);
				mu_label(ctx, buf);
				mu_end_treenode(ctx);
			}
		}
		if (mu_header(ctx, "Log")) {
			/* output text panel */
			mu_layout_row(ctx, 1, (int[]) { -1 }, 300);
			mu_begin_panel(ctx, "Log Output");
			mu_Container *panel = mu_get_current_container(ctx);
			mu_layout_row(ctx, 1, (int[]) { -1 }, -1);
			mu_text(ctx, logbuf);
			if (logbuf_updated) {
				panel->scroll.y = panel->content_size.y;
				logbuf_updated = FALSE;
			}
			mu_end_panel(ctx);
		}
	mu_end_window(ctx);
	}
}

static void ui_process_frame(mu_Context *ctx) {
	mu_begin(ctx);
	config_window(ctx);
	mu_end(ctx);
}

void ui_ev_callback(mu_Context *ctx, SDL_Event *e){
	switch(e->type) {
		case SDL_KEYUP:
		case SDL_KEYDOWN:
		if(e->key.keysym.sym == SDLK_F12 && e->type == SDL_KEYDOWN) {
			if(ui_show) {
				if(mouse_relmode)
					SDL_SetRelativeMouseMode(SDL_TRUE);
				ui_show = FALSE;
			}
			break;
		}
	}
}
/* UI end */

static rfbBool resize(rfbClient* client) {
	int width=client->width,height=client->height,
		depth=client->format.bitsPerPixel;

	if (enableResizable)
		sdlFlags |= SDL_WINDOW_RESIZABLE |  SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL;

	client->updateRect.x = client->updateRect.y = 0;
	client->updateRect.w = width; client->updateRect.h = height;

	/* (re)create the surface used as the client's framebuffer */
	SDL_FreeSurface(rfbClientGetClientData(client, SDL_Init));
	SDL_Surface* sdl=SDL_CreateRGBSurface(0,
					      width,
					      height,
					      depth,
					      0,0,0,0);
	if(!sdl)
	    rfbClientErr("resize: error creating surface: %s\n", SDL_GetError());

	rfbClientSetClientData(client, SDL_Init, sdl);
	client->width = sdl->pitch / (depth / 8);
	client->frameBuffer=sdl->pixels;

	client->format.bitsPerPixel=depth;
	client->format.redShift=sdl->format->Rshift;
	client->format.greenShift=sdl->format->Gshift;
	client->format.blueShift=sdl->format->Bshift;
	client->format.redMax=sdl->format->Rmask>>client->format.redShift;
	client->format.greenMax=sdl->format->Gmask>>client->format.greenShift;
	client->format.blueMax=sdl->format->Bmask>>client->format.blueShift;
	SetFormatAndEncodings(client);

	/* create or resize the window */
	if(!sdlWindow) {
	    sdlWindow = SDL_CreateWindow(client->desktopName,
					 SDL_WINDOWPOS_UNDEFINED,
					 SDL_WINDOWPOS_UNDEFINED,
					 width,
					 height,
					 sdlFlags);
	    if(!sdlWindow)
		rfbClientErr("resize: error creating window: %s\n", SDL_GetError());
	} else {
	    SDL_SetWindowSize(sdlWindow, width, height);
	}

	/* create the renderer if it does not already exist */
	if(!sdlRenderer) {
	    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
	    if(!sdlRenderer)
		rfbClientErr("resize: error creating renderer: %s\n", SDL_GetError());
	    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");  /* make the scaled rendering look smoother. */
	}
	SDL_RenderSetLogicalSize(sdlRenderer, width, height);  /* this is a departure from the SDL1.2-based version, but more in the sense of a VNC viewer in keeeping aspect ratio */

	/* (re)create the texture that sits in between the surface->pixels and the renderer */
	if(sdlTexture)
	    SDL_DestroyTexture(sdlTexture);
	sdlTexture = SDL_CreateTexture(sdlRenderer,
				       SDL_PIXELFORMAT_ARGB8888,
				       SDL_TEXTUREACCESS_STREAMING,
				       width, height);
	if(!sdlTexture)
	    rfbClientErr("resize: error creating texture: %s\n", SDL_GetError());

	if(sdlRenderer && sdlWindow && !ui_inited) {
		ui_inited = TRUE;
		r_init(sdlWindow, sdlRenderer);
	}
	return TRUE;
}

static rfbKeySym SDL_key2rfbKeySym(SDL_KeyboardEvent* e) {
	rfbKeySym k = 0;
	SDL_Keycode sym = e->keysym.sym;

	switch (sym) {
	case SDLK_BACKSPACE: k = XK_BackSpace; break;
	case SDLK_TAB: k = XK_Tab; break;
	case SDLK_CLEAR: k = XK_Clear; break;
	case SDLK_RETURN: k = XK_Return; break;
	case SDLK_PAUSE: k = XK_Pause; break;
	case SDLK_ESCAPE: k = XK_Escape; break;
	case SDLK_DELETE: k = XK_Delete; break;
	case SDLK_KP_0: k = XK_KP_0; break;
	case SDLK_KP_1: k = XK_KP_1; break;
	case SDLK_KP_2: k = XK_KP_2; break;
	case SDLK_KP_3: k = XK_KP_3; break;
	case SDLK_KP_4: k = XK_KP_4; break;
	case SDLK_KP_5: k = XK_KP_5; break;
	case SDLK_KP_6: k = XK_KP_6; break;
	case SDLK_KP_7: k = XK_KP_7; break;
	case SDLK_KP_8: k = XK_KP_8; break;
	case SDLK_KP_9: k = XK_KP_9; break;
	case SDLK_KP_PERIOD: k = XK_KP_Decimal; break;
	case SDLK_KP_DIVIDE: k = XK_KP_Divide; break;
	case SDLK_KP_MULTIPLY: k = XK_KP_Multiply; break;
	case SDLK_KP_MINUS: k = XK_KP_Subtract; break;
	case SDLK_KP_PLUS: k = XK_KP_Add; break;
	case SDLK_KP_ENTER: k = XK_KP_Enter; break;
	case SDLK_KP_EQUALS: k = XK_KP_Equal; break;
	case SDLK_UP: k = XK_Up; break;
	case SDLK_DOWN: k = XK_Down; break;
	case SDLK_RIGHT: k = XK_Right; break;
	case SDLK_LEFT: k = XK_Left; break;
	case SDLK_INSERT: k = XK_Insert; break;
	case SDLK_HOME: k = XK_Home; break;
	case SDLK_END: k = XK_End; break;
	case SDLK_PAGEUP: k = XK_Page_Up; break;
	case SDLK_PAGEDOWN: k = XK_Page_Down; break;
	case SDLK_F1: k = XK_F1; break;
	case SDLK_F2: k = XK_F2; break;
	case SDLK_F3: k = XK_F3; break;
	case SDLK_F4: k = XK_F4; break;
	case SDLK_F5: k = XK_F5; break;
	case SDLK_F6: k = XK_F6; break;
	case SDLK_F7: k = XK_F7; break;
	case SDLK_F8: k = XK_F8; break;
	case SDLK_F9: k = XK_F9; break;
	case SDLK_F10: k = XK_F10; break;
	case SDLK_F11: k = XK_F11; break;
	case SDLK_F12: k = XK_F12; break;
	case SDLK_F13: k = XK_F13; break;
	case SDLK_F14: k = XK_F14; break;
	case SDLK_F15: k = XK_F15; break;
	case SDLK_NUMLOCKCLEAR: k = XK_Num_Lock; break;
	case SDLK_CAPSLOCK: k = XK_Caps_Lock; break;
	case SDLK_SCROLLLOCK: k = XK_Scroll_Lock; break;
	case SDLK_RSHIFT: k = XK_Shift_R; break;
	case SDLK_LSHIFT: k = XK_Shift_L; break;
	case SDLK_RCTRL: k = XK_Control_R; break;
	case SDLK_LCTRL: k = XK_Control_L; break;
	case SDLK_RALT: k = XK_Alt_R; break;
	case SDLK_LALT: k = XK_Alt_L; break;
	case SDLK_LGUI: k = XK_Super_L; break;
	case SDLK_RGUI: k = XK_Super_R; break;
#if 0
	case SDLK_COMPOSE: k = XK_Compose; break;
#endif
	case SDLK_MODE: k = XK_Mode_switch; break;
	case SDLK_HELP: k = XK_Help; break;
	case SDLK_PRINTSCREEN: k = XK_Print; break;
	case SDLK_SYSREQ: k = XK_Sys_Req; break;
	default:
		if (e->keysym.mod & KMOD_SHIFT) sym = ucsToUpper(sym);
		k = ucs2keysym(sym);
	}

	/* SDL_TEXTINPUT does not generate characters if ctrl is down, so handle those here */
     if (k == 0 && sym > 0x0 && sym < 0x100 && e->keysym.mod & KMOD_CTRL)
		k = sym;

	return k;
}

/* UTF-8 decoding is from https://rosettacode.org/wiki/UTF-8_encode_and_decode which is under GFDL 1.2 */
static rfbKeySym utf8char2rfbKeySym(const char chr[4]) {
	int bytes = strlen(chr);
	int shift = utf8Mapping[0].bits_stored * (bytes - 1);
	rfbKeySym codep = (*chr++ & utf8Mapping[bytes].mask) << shift;
	int i;
	for(i = 1; i < bytes; ++i, ++chr) {
		shift -= utf8Mapping[0].bits_stored;
		codep |= ((char)*chr & utf8Mapping[0].mask) << shift;
	}
	return codep;
}

static void update(rfbClient* cl,int x,int y,int w,int h) {
	SDL_Surface *sdl = rfbClientGetClientData(cl, SDL_Init);
	/* update texture from surface->pixels */
	SDL_Rect r = {x,y,w,h};
 	if(SDL_UpdateTexture(sdlTexture, &r, sdl->pixels + y*sdl->pitch + x*4, sdl->pitch) < 0)
		rfbClientErr("update: failed to update texture: %s\n", SDL_GetError());
	/* copy texture to renderer and show */
	if(SDL_RenderClear(sdlRenderer) < 0)
		rfbClientErr("update: failed to clear renderer: %s\n", SDL_GetError());
	if(SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL) < 0)
		rfbClientErr("update: failed to copy texture to renderer: %s\n", SDL_GetError());

	/* UI show window */
	if(ui_show){
		ui_process_frame(ctx);
		r_ui_draw(ctx);
	}

	SDL_RenderPresent(sdlRenderer);
}

static void kbd_leds(rfbClient* cl, int value, int pad) {
	/* note: pad is for future expansion 0=unused */
	fprintf(stderr,"Led State= 0x%02X\n", value);
	fflush(stderr);
}

/* trivial support for textchat */
static void text_chat(rfbClient* cl, int value, char *text) {
	switch(value) {
	case rfbTextChatOpen:
		fprintf(stderr,"TextChat: We should open a textchat window!\n");
		TextChatOpen(cl);
		break;
	case rfbTextChatClose:
		fprintf(stderr,"TextChat: We should close our window!\n");
		break;
	case rfbTextChatFinished:
		fprintf(stderr,"TextChat: We should close our window!\n");
		break;
	default:
		fprintf(stderr,"TextChat: Received \"%s\"\n", text);
		break;
	}
	fflush(stderr);
}

void write_log(char *text) {
	if ((strlen(logbuf) + strlen(text) + 1) >= LOG_SIZE) {
		memcpy(logbuf, logbuf+LOG_SIZE/2, LOG_SIZE/2);
	}
	strcat(logbuf, text);
	logbuf_updated = TRUE;
}

static void LogWriter(const char *format, ...)
{
    va_list args;
    char buf[1024];
    time_t log_clock;

    if(!rfbEnableClientLogging)
      return;

    va_start(args, format);

    time(&log_clock);
    strftime(buf, 255, "%d/%m/%Y %X ", localtime(&log_clock));
	write_log(buf);
    fprintf(stderr, "%s", buf);
	vsprintf(buf, format, args);
	write_log(buf);
    fprintf(stderr, "%s", buf);
    fflush(stderr);

    va_end(args);
}

static void cleanup(rfbClient* cl)
{
  /*
    just in case we're running in listenLoop:
    close viewer window by restarting SDL video subsystem
  */
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  if(cl)
    rfbClientCleanup(cl);
}


static rfbBool handleSDLEvent(rfbClient *cl, SDL_Event *e)
{
	switch(e->type) {
	case SDL_WINDOWEVENT:
		switch (e->window.event) {
			case SDL_WINDOWEVENT_EXPOSED:
				SendFramebufferUpdateRequest(cl, 0, 0, cl->width, cl->height, FALSE);
				break;
			case SDL_WINDOWEVENT_RESIZED:
				if(resizeMethod == RESIZE_DESKTOP)
					SendExtDesktopSize(cl, e->window.data1, e->window.data2);
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				if (SDL_HasClipboardText()) {
					char *text = SDL_GetClipboardText();
					if(text) {
						SendClientCutText(cl, text, strlen(text));
					}
				}
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				if (rightAltKeyDown) {
					SendKeyEvent(cl, XK_Alt_R, FALSE);
					rightAltKeyDown = FALSE;
					rfbClientLog("released right Alt key\n");
				}
				if (leftAltKeyDown) {
					SendKeyEvent(cl, XK_Alt_L, FALSE);
					leftAltKeyDown = FALSE;
					rfbClientLog("released left Alt key\n");
				}
				break;
		}
		break;
	case SDL_MOUSEWHEEL:
	{
		int steps;
		if (viewOnly)
			break;

		if(e->wheel.y > 0)
			for(steps = 0; steps < e->wheel.y; ++steps) {
			SendPointerEvent(cl, mouse_x, mouse_y, rfbButton4Mask);
			SendPointerEvent(cl, mouse_x, mouse_y, 0);
			}
		if(e->wheel.y < 0)
			for(steps = 0; steps > e->wheel.y; --steps) {
			SendPointerEvent(cl, mouse_x, mouse_y, rfbButton5Mask);
			SendPointerEvent(cl, mouse_x, mouse_y, 0);
			}
		if(e->wheel.x > 0)
			for(steps = 0; steps < e->wheel.x; ++steps) {
			SendPointerEvent(cl, mouse_x, mouse_y, 0b01000000);
			SendPointerEvent(cl, mouse_x, mouse_y, 0);
			}
		if(e->wheel.x < 0)
			for(steps = 0; steps > e->wheel.x; --steps) {
			SendPointerEvent(cl, mouse_x, mouse_y, 0b00100000);
			SendPointerEvent(cl, mouse_x, mouse_y, 0);
			}
		break;
	}
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEMOTION:
	{
		int state, i;
		if (viewOnly)
			break;

		if (e->type == SDL_MOUSEMOTION) {
			if(mouse_relmode){
				mouse_x = 0x7FFF + e->motion.xrel;
				mouse_y = 0x7FFF + e->motion.yrel;
			} else {
				mouse_x = e->motion.x;
				mouse_y = e->motion.y;
			}
			state = e->motion.state;
		}
		else {
			if(!mouse_relmode) {
				mouse_x = e->button.x;
				mouse_y = e->button.y;
			}
			state = e->button.button;
			for (i = 0; buttonMapping[i].sdl; i++)
				if (state == buttonMapping[i].sdl) {
					state = buttonMapping[i].rfb;
					if (e->type == SDL_MOUSEBUTTONDOWN)
						buttonMask |= state;
					else
						buttonMask &= ~state;
					break;
				}
		}
		SendPointerEvent(cl, mouse_x, mouse_y, buttonMask);
		buttonMask &= ~(rfbButton4Mask | rfbButton5Mask);
		break;
	}
	case SDL_KEYUP:
	case SDL_KEYDOWN:
		if (viewOnly)
			break;
		if(e->key.keysym.sym == SDLK_F12 && e->type == SDL_KEYDOWN) {
			if(!ui_show) {
				ui_show = TRUE;
				if(mouse_relmode)
					SDL_SetRelativeMouseMode(SDL_FALSE);
			}
			break;
		}
		SendKeyEvent(cl, SDL_key2rfbKeySym(&e->key), e->type == SDL_KEYDOWN ? TRUE : FALSE);
		if (e->key.keysym.sym == SDLK_RALT)
			rightAltKeyDown = e->type == SDL_KEYDOWN;
		if (e->key.keysym.sym == SDLK_LALT)
			leftAltKeyDown = e->type == SDL_KEYDOWN;
		break;
	case SDL_QUIT:
        if(listenLoop)
		  {
		    cleanup(cl);
		    return FALSE;
		  }
		else
		  {
		    rfbClientCleanup(cl);
		    exit(0);
		  }
	default:
		rfbClientLog("ignore SDL event: 0x%x\n", e->type);
	}
	return TRUE;
}

static void got_selection(rfbClient *cl, const char *text, int len)
{
        if(SDL_SetClipboardText(text) != 0)
	    rfbClientErr("could not set received clipboard text: %s\n", SDL_GetError());
}

static rfbCredential* get_credential(rfbClient* cl, int credentialType){
	rfbCredential *c = malloc(sizeof(rfbCredential));
	c->userCredential.username = malloc(RFB_BUF_SIZE);
	c->userCredential.password = malloc(RFB_BUF_SIZE);

	if(credentialType != rfbCredentialTypeUser) {
	    rfbClientErr("something else than username and password required for authentication\n");
	    return NULL;
	}

	rfbClientLog("username and password required for authentication!\n");
	printf("user: ");
	fgets(c->userCredential.username, RFB_BUF_SIZE, stdin);
	printf("pass: ");
	fgets(c->userCredential.password, RFB_BUF_SIZE, stdin);

	/* remove trailing newlines */
	c->userCredential.username[strcspn(c->userCredential.username, "\n")] = 0;
	c->userCredential.password[strcspn(c->userCredential.password, "\n")] = 0;

	return c;
}

void renderBackendsSDL(void) {
    int numdrivers = SDL_GetNumRenderDrivers(); 
    printf("Render driver count: %u\n", numdrivers);
    for(int i = 0; i < numdrivers; i++) { 
        SDL_RendererInfo drinfo; 
        SDL_GetRenderDriverInfo (i, &drinfo); 

        printf("Driver name (%u): %s\n", i, drinfo.name);
        if(drinfo.flags & SDL_RENDERER_SOFTWARE)
            printf(" the renderer is a software fallback\n");
        if(drinfo.flags & SDL_RENDERER_ACCELERATED)
            printf(" the renderer uses hardware acceleration\n");
        if(drinfo.flags & SDL_RENDERER_PRESENTVSYNC)
            printf(" present is synchronized with the refresh rate\n");
        if(drinfo.flags & SDL_RENDERER_TARGETTEXTURE)
            printf(" the renderer supports rendering to texture\n");
 /*       for(int j = 0; j < drinfo.num_texture_formats; j++)
            printf("Texture format %d: %s\n", j, SDL_GetPixelFormatName(drinfo.texture_formats[j]));
  */
    }
}

#ifdef mac
#define main SDLmain
#endif

void usage(char *name) {
	fprintf (stderr,"Usage: %s [options] [vnc options] server:port\n", name);
	fprintf (stderr," Options:\n");
 	fprintf (stderr,"  -showdrivers          Show available SDL rendering drivers\n");
  	fprintf (stderr,"  -sdldriver [s]        Use [s] SDL render driver (default: %s)\n", DEFAULT_RENDER_B);
	fprintf (stderr,"  -noaudio              Disable audio support (default: enabled)\n");
	fprintf (stderr,"  -resizable [s]        Enable desktop window resizing (default: %s)\n", enableResizable ? "on" : "off");
	fprintf (stderr,"  -resize-method [s]    Resizing method to use: zoom, desktop (default: desktop)\n");
	fprintf (stderr,"                        'desktop' - change desktop resolution on the server side\n");
	fprintf (stderr,"                        'zoom'    - rescale desktop picture to the client window size\n");
 	fprintf (stderr,"  -no-logs              Disable logging (default: %s)\n", logging_enabled ? "on" : "off");
	fprintf (stderr," VNC options:\n");
    fprintf (stderr,"  -encodings [s]         VNC encoding to use: h264 tight zrle ultra copyrect hextile zlib corre rre raw (default: h264)\n");
    exit(1);
}


int main(int argc,char** argv) {
	int i, j;
	SDL_Event e;

	if(argc < 2)
		usage(argv[0]);

	for (i = 1, j = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-showdrivers")) {
			renderBackendsSDL();
			exit(1);
		} else if (!strcmp(argv[i], "-sdldriver")) {
			SDLrenderDriver = strdup(argv[i+1]);
		} else if (!strcmp(argv[i], "-viewonly"))
			viewOnly = 1;
		else if (!strcmp(argv[i], "-noaudio"))
			isAudioEnabled = FALSE;
		else if (!strcmp(argv[i], "-no-logs"))
			logging_enabled = FALSE;
		else if (!strcmp(argv[i], "-resizable")) {
			if(!strcasecmp(argv[i+1], "on"))
				enableResizable = 1;
			else if(!strcasecmp(argv[i+1], "off"))
				enableResizable = 0;
		} else if (!strcmp(argv[i], "-resize-method")) {
			if(!strcasecmp(argv[i+1], "desktop"))
				resizeMethod = RESIZE_DESKTOP;
			else if(!strcasecmp(argv[i+1], "zoom"))
				resizeMethod = RESIZE_ZOOM;
		} else if (!strcmp(argv[i], "-listen")) {
				listenLoop = 1;
				argv[i] = "-listennofork";
				++j;
		}
		else {
			if (i != j)
				argv[j] = argv[i];
			j++;
		}
	}
	argc = j;

	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);
	signal(SIGINT, exit);

	SDL_StopTextInput();

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, SDLrenderDriver ? SDLrenderDriver : DEFAULT_RENDER_B);
	rfbClientLog("SDL Rendering Driver: %s\n", SDLrenderDriver ? SDLrenderDriver : DEFAULT_RENDER_B);
	SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

	/* UI init */
  	ctx = malloc(sizeof(mu_Context));
  	mu_init(ctx);
  	ctx->text_width = r_get_text_width;
  	ctx->text_height = r_get_text_height;
	//ctx->draw_frame  = r_draw_frame;
    ctx->style->spacing = 6;
    ctx->style->padding = 4;
    ctx->style->colors[MU_COLOR_PANELBG] = mu_color(50,  50,  50,  155);
	r_ui_event_callback = ui_ev_callback;

	rfbClientLog=rfbClientErr=LogWriter;
	rfbEnableClientLogging = logging_enabled;

	do {
	  /* 16-bit: cl=rfbGetClient(5,3,2); */
	  cl=rfbGetClient(8,3,4);
	  cl->MallocFrameBuffer=resize;
	  cl->canHandleNewFBSize = TRUE;
	  cl->GotFrameBufferUpdate=update;
	  cl->HandleKeyboardLedState=kbd_leds;
	  cl->HandleTextChat=text_chat;
	  cl->GotXCutText = got_selection;
	  cl->GetCredential = get_credential;
	  cl->listenPort = LISTEN_PORT_OFFSET;
	  cl->listen6Port = LISTEN_PORT_OFFSET;

	  cl->audioEnable = isAudioEnabled;
	  cl->sampleFormat = 2;
	  cl->channels = 2;
	  cl->frequency = 22050;

	  SDL_GL_SetSwapInterval(-1);

	  if(!rfbInitClient(cl,&argc,argv))
	    {
	      cl = NULL; /* rfbInitClient has already freed the client struct */
	      cleanup(cl);
	      break;
	    }

	  while(1) {
	    if(SDL_PollEvent(&e)) {
			if(ui_show) {
				if(!r_ui_event(ctx, &e))
					break;
			} else if(!handleSDLEvent(cl, &e))
					break;
	    } else {
			i=WaitForMessage(cl,500);
			if(i<0)
			{
		  		cleanup(cl);
		  		break;
		  	}
			if(i)
			if(!HandleRFBServerMessage(cl))
		  	{
		    	cleanup(cl);
		    	break;
		  	}
			if(ui_show) {
				update(cl,0,0,cl->width,cl->height);
				SDL_Delay(4);
			}
	    }
	  }
	}
	while(listenLoop);

	return 0;
}

