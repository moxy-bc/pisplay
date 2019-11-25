#ifndef __PISPLAY_H
#define __PISPLAY_H

#include <stdint.h>

#define PIS_NONE -1

#define OPL_MAGIC 3579545
#define OPL_NOTE_FREQUENCY_LO_B 0x143
#define OPL_NOTE_FREQUENCY_LO_C 0x157
#define OPL_NOTE_FREQUENCY_HI_B 0x287
#define OPL_NOTE_FREQUENCY_HI_C 0x2ae

#define FMOPL_OUTPUT_BUFFER_SIZE 32768
#define PIS_DEFAULT_SPEED 6


#define readb(f) ((uint8_t)fgetc(f))
#define replay_reset_voice(v) replay_set_voice_volatiles(v, 0, 0, 0);
#define EFFECT_HI(r) ((r)->effect >> 8)
#define EFFECT_LO(r) ((r)->effect & 0xff)
#define EFFECT_MIDNIB(r) (((r)->effect >> 4) & 15)
#define EFFECT_LONIB(r) ((r)->effect & 15)
#define HAS_NOTE(r) ((r)->note < 12)
#define HAS_INSTRUMENT(r) ((r)->instrument > 0)
#define IS_NOTE(n) (n < 12)


typedef struct {
	uint8_t mul1, mul2; // multiplier
	uint8_t lev1, lev2; // level
	uint8_t atd1, atd2; // attack / decay
	uint8_t sur1, sur2; // sustain / release
	uint8_t wav1, wav2; // waveform
	uint8_t fbcon;		// feedback / connection_type
} PisInstrument;


typedef struct {
	uint8_t length; // length of order list
	uint8_t number_of_patterns; // # of patterns stored in module
	uint8_t number_of_instruments; // # of instruments stored in module
	uint8_t pattern_map[128]; // maps physical to logical pattern
	uint8_t instrument_map[32]; // maps physical to logical instrument
	uint8_t order[256][9]; // order list for each channel
	uint32_t pattern[128][64]; // pattern data
	PisInstrument instrument[64]; // instrument data
} PisModule;


typedef struct {
	int note;
	int octave;
	int instrument;
	int effect;
} PisRowUnpacked;


typedef struct {
	int instrument;
	int volume;
	int note;
	int frequency;
	int octave;
	int previous_effect;
	int slide_increment;
	int porta_increment;
	int porta_src_freq;
	int porta_src_octave;
	int porta_dest_freq;
	int porta_dest_octave;
	int porta_sign;
	int arpeggio_flag;
	int arpeggio_freq[3];
	int arpeggio_octave[3];
} PisVoiceState;


typedef struct {
	int speed;
	int count;
	int position;
	int row;
	int position_jump;
	int pattern_break;
	int arpeggio_index;
	int loop_flag;
	int loop_start_row;
	int loop_count;
	PisVoiceState voice_state[9];
	PisRowUnpacked row_buffer[9];
} PisReplayState;


// Player control
void pisplay_init();
void pisplay_shutdown();
void pisplay_load_and_play(const char *path);
void load_module(const char *path, PisModule *module);
void load_pattern(uint32_t *destination, FILE *f);
void load_instrument(PisInstrument *pinstr, FILE *f);

// Replay routine
void init_replay_state(PisReplayState *pstate);
void replay_frame_routine();
void replay_voice(int);
void unpack_row();
void advance_row();
void replay_enter_row_with_portamento(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_enter_row_with_instrument_and_note(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_enter_row_with_instrument_only(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_enter_row_with_note_only(int v, PisVoiceState *vs, PisRowUnpacked *r);				
void replay_enter_row_with_possibly_effect_only(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_handle_effect(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_handle_arpeggio(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_handle_posjmp(int v, PisRowUnpacked *r);
void replay_handle_ptnbreak(int v, PisRowUnpacked *r);
void replay_handle_speed(int v, PisRowUnpacked *r);
void replay_handle_exx_command(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_handle_loop(int v, PisRowUnpacked *r);
void replay_handle_volume_slide(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_do_per_frame_effects();
void replay_do_per_frame_portamento(int v, PisVoiceState *vs);
void replay_set_note(int v, PisVoiceState *vs, PisRowUnpacked *r);
void replay_set_instrument(int v, int instr_index);
void replay_set_level(int v, int instr_index, int gain, int do_apply_correction);
void replay_set_voice_volatiles(int v, int arpeggio_flag, int slide_increment, int porta_increment);

// Audio, OPL
void audio_callback (void* userdata, Uint8* stream, int numbytes);
void audio_callback_float (void*, int);
void audio_callback_s16 (void*, int);
void s16tofloat(int16_t *source, float *destination, int numsamples);
void init_audio();
void init_opl();
void oplout(int r, int v);
void opl_set_pitch(int v, int freq, int octave);
void opl_set_instrument(int v, PisInstrument *instr);
void opl_note_off(int v);


#endif
