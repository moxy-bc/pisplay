#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <assert.h>

#include "pisplay.h"


#define WINDOW_W 640
#define WINDOW_H 480
#define NUMBER_OF_TUNES 21
#define TUNE_CHANGE_DELAY_FRAMES 25
#define TUNE_MAX_PLAYTIME_FRAMES 10450


typedef struct {
	int frame_count;
	int flashing_tune;
	int playing_tune;	
	int last_tune_change_frame;
	int last_up_down_keypress_frame;
} State;


#ifdef __EMSCRIPTEN__
void em_main_loop ();
#else
int render_thread_fn ();
#endif

void shutdown_app();
void handle_input_event(SDL_Event *e);
void handle_keydown(SDL_Event *e);
void handle_keyup(SDL_Event *e);
void handle_mousebuttondown(SDL_Event *e);
void handle_tune_change();
void frame_routine();
void render_tunes_list();
void render_background();
void render_logo();
void render_starfield();
void put_text(TTF_Font *font, SDL_Color color, int x, int y, const char *text, int *tex_w, int *tex_h);


extern const char *tune_paths[NUMBER_OF_TUNES];
extern const char *tune_names[NUMBER_OF_TUNES];
extern uint8_t logo_map[];


State state;
int is_terminated;
SDL_Thread *thread;
int start_time;
SDL_Window *window;
SDL_Renderer *renderer;
TTF_Font *font;
uint8_t *pixel;
SDL_Rect tune_rect[NUMBER_OF_TUNES];


int main (int argc, char **argv) {
	int r;
	SDL_Event event;

	memset(&state, 0, sizeof(State));
	r = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	assert(r == 0);
	TTF_Init();
	font = TTF_OpenFont("assets/whitrabt.ttf", 15);

	pixel = calloc(4, WINDOW_W * WINDOW_H);
    SDL_CreateWindowAndRenderer(WINDOW_W, WINDOW_H, 0, &window, &renderer);

	pisplay_init();
	pisplay_load_and_play(tune_paths[0]);

#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(em_main_loop, 50, 1);
#else
	is_terminated = 0;
	start_time = SDL_GetTicks();
	thread = SDL_CreateThread(render_thread_fn, "pisplay_render", NULL);	
	while ( ! is_terminated) {
		r = SDL_WaitEvent(&event);
		handle_input_event(&event);
	}
#endif

	return 0;
}


#ifdef __EMSCRIPTEN__
void em_main_loop () {
	SDL_Event event;

	frame_routine();

	while (SDL_PollEvent(&event)) {
		handle_input_event(&event);
	}
}
#else
int render_thread_fn () {
	int now, elapsed;
	
	while ( ! is_terminated) {
		now = SDL_GetTicks();
		elapsed = now - start_time;
		if (elapsed >= 20) {
			
			frame_routine();
			
			start_time = now + (elapsed - 20);
		}
	}
}
#endif


void handle_input_event(SDL_Event *event) {
	if(event->type==SDL_KEYDOWN) {
		handle_keydown(event);
	} else if(event->type==SDL_KEYUP) {
		handle_keyup(event);
	} else if(event->type==SDL_MOUSEBUTTONDOWN) {
		handle_mousebuttondown(event);
	}
}


void shutdown_app () {
#ifdef __EMSCRIPTEN__
		emscripten_cancel_main_loop();
#endif
	is_terminated = 1;
	pisplay_shutdown();	
	TTF_CloseFont(font);
	TTF_Quit();
	SDL_Quit();
	free(pixel);
}


//~ ░█▀▀░█▀▄░█▀█░█▄█░█▀▀░░░█▀▄░█▀█░█░█░▀█▀░▀█▀░█▀█░█▀▀
//~ ░█▀▀░█▀▄░█▀█░█░█░█▀▀░░░█▀▄░█░█░█░█░░█░░░█░░█░█░█▀▀
//~ ░▀░░░▀░▀░▀░▀░▀░▀░▀▀▀░░░▀░▀░▀▀▀░▀▀▀░░▀░░▀▀▀░▀░▀░▀▀▀

void frame_routine() {
	render_background();
	render_tunes_list();
	handle_tune_change();
	SDL_RenderPresent(renderer);
	state.frame_count++;
}


void handle_tune_change () {
	if (state.frame_count - state.last_up_down_keypress_frame >= TUNE_CHANGE_DELAY_FRAMES &&
		//
		// Tune change requested by input
		//
		state.playing_tune != state.flashing_tune &&
		state.frame_count - state.last_tune_change_frame >= TUNE_CHANGE_DELAY_FRAMES) {
		
		state.playing_tune = state.flashing_tune;
		state.last_tune_change_frame = state.frame_count;		
		pisplay_load_and_play(tune_paths[ state.playing_tune ]);
	} else if (state.frame_count - state.last_tune_change_frame >= TUNE_MAX_PLAYTIME_FRAMES) {
		//
		// Handle automatic tune change
		//
		state.flashing_tune++;
		if (state.flashing_tune == NUMBER_OF_TUNES) state.flashing_tune = 0;
	}
}


void render_background () {
	
	memset(pixel, 0, WINDOW_W * WINDOW_H << 2);
	render_starfield();
	render_logo();
	
	SDL_Texture *pxlbuf_texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		WINDOW_W, WINDOW_H);
		
	SDL_UpdateTexture(pxlbuf_texture, NULL, pixel, WINDOW_W << 2);
	SDL_RenderCopy(renderer, pxlbuf_texture, NULL, NULL);
	SDL_DestroyTexture(pxlbuf_texture);	
}


//~ ░▀█▀░█░█░▀█▀░█▀▄░█░░░▀█▀░█▀█░█▀▀░░░█░░░█▀█░█▀▀░█▀█
//~ ░░█░░█▄█░░█░░█▀▄░█░░░░█░░█░█░█░█░░░█░░░█░█░█░█░█░█
//~ ░░▀░░▀░▀░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀░▀░▀▀▀░░░▀▀▀░▀▀▀░▀▀▀░▀▀▀

#define LOGO_W 452
#define LOGO_H 60
#define LOGO_Y 24

double squeeze_precalced[LOGO_W] = { 0.0, };
const double logo_ancle_increment = M_PI / (1.5 * LOGO_W);

void __precalc_squeeze () {
	double squeeze;
	for (int x=0; x<=(LOGO_W>>1); x++) {
		squeeze = 255.0/256.0 + 9.0 * pow(2.0, (x * 32.0 / (double)LOGO_W) - 8.0) / 256.0;
		squeeze_precalced[x] = squeeze;
		squeeze_precalced[LOGO_W-1-x] = -squeeze;
	}
}

void render_logo() {	
	uint32_t *px = (uint32_t*)pixel + LOGO_Y * WINDOW_W;
	uint32_t *lg;
	int x, y, squeeze_index;
	double logo_y, logo_y_incr, squeeze, angle;
	int logo_y_int;
	
	if (squeeze_precalced[0] == 0.0) __precalc_squeeze();
	
	angle = (double)state.frame_count * M_PI / 200.0;
	
	px += (WINDOW_W - LOGO_W) >> 1;
	for (x=0; x<LOGO_W; x++) {

		squeeze_index = (int)((1.0 - fabs(sin(angle))) * (LOGO_W - 1));
		squeeze = squeeze_precalced[squeeze_index];
		
		logo_y = ((double)LOGO_H / 2.0) * (1.0 - squeeze);
		logo_y_incr = squeeze;
				
		for (y=0; y<LOGO_H; y++, logo_y+=logo_y_incr) {
			logo_y_int = (int)round(logo_y);
			if (logo_y_int >= 0 && logo_y_int < LOGO_H) {
				lg = (uint32_t*)logo_map + x + logo_y_int * LOGO_W;
				*px = *lg;
			} else {
				*px = 0;
			}
			px += WINDOW_W;			
		}
		px -= (LOGO_H * WINDOW_W - 1);
		angle += logo_ancle_increment;
	}
}


//~ ░█▀▀░▀█▀░█▀█░█▀▄░█▀▀░▀█▀░█▀▀░█░░░█▀▄
//~ ░▀▀█░░█░░█▀█░█▀▄░█▀▀░░█░░█▀▀░█░░░█░█
//~ ░▀▀▀░░▀░░▀░▀░▀░▀░▀░░░▀▀▀░▀▀▀░▀▀▀░▀▀░

#define NUM_STARS 20

typedef struct {
	int x, y;
	int plane;
} Star;

const uint32_t star_color[9] = {
	0xffefefff,
	0xffafafef,
	0xff6f6faf,
	0xff2f2f6f,
	0xff27272f,
	0xff1f1f27,
	0xff17171f,
	0xff0f0f17,
	0xff07070f
};

Star star[NUM_STARS] = {
	{ -1, },
};

void init_stars() {	
	srand((unsigned int)SDL_GetTicks());
	for (int i=0; i<NUM_STARS; i++) {
		star[i].x = rand() % (WINDOW_W - 5);
		star[i].y = rand() % (WINDOW_H - 1);
		star[i].plane = rand() & 3;
	}
}

#define put_pixel(x, y, c) *(px + (WINDOW_W * (y)) + x) = c

void render_starfield () {
	uint32_t *px = (uint32_t*)pixel;
	
	if (star[0].x == -1) {
		init_stars();
	}
	
	for (int i=0; i<NUM_STARS; i++) {
		int x = star[i].x, y = star[i].y;
		
		int ci = star[i].plane;
		for (int j=0; j<6; j++, ci++) {
			put_pixel(x+j, y,   star_color[ ci ]);
			put_pixel(x+j, y+1, star_color[ ci ]);
		}
		
		star[i].x -= (4 - star[i].plane);
		if (star[i].x < 0) {
			star[i].x = WINDOW_W - 5;
			star[i].y = rand() % (WINDOW_H - 1);
			star[i].plane = rand() & 3;
		}
	}
}


//~ ░▀█▀░█░█░█▀█░█▀▀░█▀▀░░░█░░░▀█▀░█▀▀░▀█▀
//~ ░░█░░█░█░█░█░█▀▀░▀▀█░░░█░░░░█░░▀▀█░░█░
//~ ░░▀░░▀▀▀░▀░▀░▀▀▀░▀▀▀░░░▀▀▀░▀▀▀░▀▀▀░░▀░

#define TUNES_LIST_W 392
#define TUNES_LIST_H 312
#define TUNES_LIST_Y 120

void render_tunes_list() {	
	int first_index, last_index;
	int tex_w, tex_h;
	int x, y;
	
	first_index = 0;
	last_index = NUMBER_OF_TUNES - 1;
	
	x = (WINDOW_W - TUNES_LIST_W) >> 1;
	y = TUNES_LIST_Y;
	for (int i=first_index; i<=last_index; i++) {
		SDL_Color color;
		if (i == state.flashing_tune) {
			color.r = 0;
			color.g = 127 + ((state.frame_count << 2) & 0x7f);
			color.b = 127;
		} else {
			color.r = 159;
			color.g = 159;
			color.b = 159;
		}
		put_text(font, color, x, y, tune_names[i], &tex_w, &tex_h);
		
		tune_rect[i].x = x;  tune_rect[i].y = y;
		tune_rect[i].w = tex_w;  tune_rect[i].h = tex_h;
		
		y += tex_h + 3;			
	}
}


void put_text(TTF_Font *font, SDL_Color color, int x, int y, const char *text, int *tex_w, int *tex_h) {
	
	SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_QueryTexture(texture, NULL, NULL, tex_w, tex_h);
	SDL_Rect dstrect = { x, y, *tex_w, *tex_h };
	SDL_RenderCopy(renderer, texture, NULL, &dstrect);
	SDL_DestroyTexture(texture);
	SDL_FreeSurface(surface);	
}


//~ ░█▀▀░█▀█░█▀█░▀█▀░█▀▄░█▀█░█░░░█▀▀
//~ ░█░░░█░█░█░█░░█░░█▀▄░█░█░█░░░▀▀█
//~ ░▀▀▀░▀▀▀░▀░▀░░▀░░▀░▀░▀▀▀░▀▀▀░▀▀▀

void handle_keydown(SDL_Event *e) {
	switch (e->key.keysym.sym) {
	case SDLK_ESCAPE:
		shutdown_app();
		break;
	case SDLK_UP:
		if (state.flashing_tune > 0) state.flashing_tune--;
		state.last_up_down_keypress_frame = state.frame_count;
		break;
	case SDLK_DOWN:
		if (state.flashing_tune < NUMBER_OF_TUNES - 1) state.flashing_tune++;
		state.last_up_down_keypress_frame = state.frame_count;
		break;
	}
}


void handle_keyup(SDL_Event *e) {
	switch (e->key.keysym.sym) {
	}
}


void handle_mousebuttondown(SDL_Event *e) {
	
	int x = e->button.x;
	int y = e->button.y;

	SDL_Point point = { x, y };
	
	for (int i=0; i<NUMBER_OF_TUNES; i++) {
		if (SDL_PointInRect(&point, &tune_rect[i])) {
			
			if (state.flashing_tune != i) state.flashing_tune = i;
			state.last_up_down_keypress_frame = state.frame_count;
			
			break;
		}
	}
}


const char* tune_paths[NUMBER_OF_TUNES] = {
	"tunes/ACTION.PIS",
	"tunes/ATPEACE.PIS",
	"tunes/BENTROIT.PIS",
	"tunes/BRONIX.PIS",
	"tunes/CAVE.PIS",
	"tunes/CNNNBALL.PIS",
	"tunes/HOPE.PIS",
	"tunes/IMPLOSIV.PIS",
	"tunes/INSIDE.PIS",
	"tunes/ISLAND.PIS",
	"tunes/KKINKLE.PIS",
	"tunes/LUCIFER.PIS",
	"tunes/MALIN3.PIS",
	"tunes/MALIN.PIS",
	"tunes/NVSBLSUN.PIS",
	"tunes/SALVORE.PIS",
	"tunes/SATONIC.PIS",
	"tunes/SEDATIV.PIS",
	"tunes/THEONES.PIS",
	"tunes/TRAMPLNG.PIS",
	"tunes/ZELDNI.PIS"
};

const char* tune_names[NUMBER_OF_TUNES] = {	
	"Action",
	"At Peace with Myself",
	"Bentroit",
	"Bronix",
	"So I Have Entered This Dark Cave Called Life",
	"Cannonball",
	"Hope, Your Facial Expression Kills Me",
	"I Am an Implosive Man",
	"Inside Where I Remain",
	"With a Little Imagination It Was an Island",
	"Kip Kinkle Theme",
	"Lucifer, Don't Let Me Grow Bitter",
	"Malin in the Sea of Shining Tears",
	"Malin in Her Secret Garden",
	"The Invisible Sun Is Far Away",
	"Salvatore",
	"Satonic",
	"On Sedatives and Alcohol She Died",
	"The Ones He Couldn't Have",
	"Trampling on the Light like Swine",
	"Zeldni"
};
