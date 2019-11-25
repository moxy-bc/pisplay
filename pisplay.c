#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <assert.h>

#include "fmopl.h"
#include "pisplay.h"


const int opl_voice_offset_into_registers[9] = {
	0,1,2,8,9,10,16,17,18
};

const int frequency_table[12] = {
	0x157,0x16B,0x181,0x198,0x1B0,0x1CA,
	0x1E5,0x202,0x220,0x241,0x263,0x287
};


PisModule module;
PisReplayState replay_state;
int is_playing;
SDL_AudioSpec obtainedAudioSpec;
FM_OPL *opl;
INT16 *fmopl_output_buffer;
float *float_buffer;
int samples_per_frame;
int frame_countdown;


void pisplay_init() {
	init_audio();
	init_opl();
	
	fmopl_output_buffer = calloc(2, FMOPL_OUTPUT_BUFFER_SIZE >> 1);
	
	// >> 1 to match # of samples
	float_buffer = calloc(4, FMOPL_OUTPUT_BUFFER_SIZE >> 1); 
}


void pisplay_shutdown() {
	is_playing = 0;
	SDL_PauseAudio(1);
	free(float_buffer);
	free(fmopl_output_buffer);
	OPLDestroy(opl);
	SDL_CloseAudio();
}


void pisplay_load_and_play(const char *path) {
	
	if (is_playing) {
		is_playing = 0;
		SDL_PauseAudio(1);
	}
	
	load_module(path, &module);
	OPLResetChip(opl);
	oplout(1, 0x20); // enable waveform control
	init_replay_state(&replay_state);
	is_playing = 1;
	SDL_PauseAudio(0);	
}


void init_replay_state(PisReplayState *pstate) {
	memset(pstate, 0, sizeof(PisReplayState));
	pstate->speed = PIS_DEFAULT_SPEED;
	pstate->count = PIS_DEFAULT_SPEED - 1;
	pstate->position_jump = PIS_NONE;
	pstate->pattern_break = PIS_NONE;
	for (int i=0; i<9; i++) {
		pstate->voice_state[i].instrument = PIS_NONE;
	}

	frame_countdown = samples_per_frame;
}


void replay_frame_routine() {
	if (is_playing) {			
		replay_state.count++;
		if (replay_state.count >= replay_state.speed) {

			unpack_row();
			
			for (int v=0; v<9; v++) {
				replay_voice(v);
			}			
			
			advance_row();
		} else {
			replay_do_per_frame_effects();
		}
	}
}


void replay_voice(int v) {
	PisVoiceState *vs = &replay_state.voice_state[v];	
	PisRowUnpacked r = replay_state.row_buffer[v];

	if (EFFECT_HI(&r) == 0x03) {
		//
		// With portamento
		//
		replay_enter_row_with_portamento(v, vs, &r);
	} else {
		if (HAS_INSTRUMENT(&r)) {
			if (HAS_NOTE(&r)) {
				//
				// Instrument + note
				//
				replay_enter_row_with_instrument_and_note(v, vs, &r);
			} else {
				//
				// Instrument only
				//
				replay_enter_row_with_instrument_only(v, vs, &r);
			}			
		} else {
			if (HAS_NOTE(&r)) {
				//
				// Note only
				//
				replay_enter_row_with_note_only(v, vs, &r);				
			} else {
				//
				// Possibly effect only
				//
				replay_enter_row_with_possibly_effect_only(v, vs, &r);
			}			
		}
	}
	
	replay_handle_effect(v, vs, &r);
	
	if (r.effect) {
		vs->previous_effect = r.effect;
	} else {
		vs->previous_effect = PIS_NONE;
		replay_reset_voice(v);
	}	
}


void replay_enter_row_with_portamento(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	if (HAS_INSTRUMENT(r)) {
		replay_set_instrument(v, r->instrument);
		if (vs->volume < 63) {
			replay_set_level(v, r->instrument, PIS_NONE, 0);
		}
	}
	if (HAS_NOTE(r)) {
		vs->porta_src_freq = vs->frequency;
		vs->porta_src_octave = vs->octave;
		vs->porta_dest_freq = frequency_table[ r->note ];
		vs->porta_dest_octave = r->octave;
		
		if (vs->porta_dest_octave < vs->octave) {
			vs->porta_sign = -1;
		} else if (vs->porta_dest_octave > vs->octave) {
			vs->porta_sign = 1;
		} else {
			vs->porta_sign = (vs->porta_dest_freq < vs->frequency)
						   ? -1
						   : 1;
		}
	}
}


void replay_enter_row_with_instrument_and_note(int v, PisVoiceState *vs, PisRowUnpacked *r) {

	vs->previous_effect = PIS_NONE;
	
	opl_note_off(v);
	if (EFFECT_HI(r) != 0x0c) {
		//
		// Volume is not set
		//
		if (r->instrument != vs->instrument) {
			//
			// Is new instrument
			//
			replay_set_instrument(v, r->instrument);
		} else if (vs->volume < 63) {
			replay_set_level(v, r->instrument, PIS_NONE, 0);
		}
			
	} else {
		//
		// Volume is set
		//
		if (r->instrument != vs->instrument) {
			//
			// Is new instrument
			//
			replay_set_instrument(v, r->instrument);
		}
		replay_set_level(v, r->instrument, EFFECT_LO(r), 1);
	}
	//
	// Trigger new note
	//
	replay_set_note(v, vs, r);
}


void replay_enter_row_with_instrument_only(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	
	if (r->instrument != vs->instrument) {
		//
		// Is new instrument
		//
		replay_set_instrument(v, r->instrument);
		
		//
		// Set operator level according to instrument and possibly Cxx effect
		//
		if (EFFECT_HI(r) == 0x0c) {
			replay_set_level(v, r->instrument, EFFECT_LO(r), 1);
		} else if (vs->volume < 63) {
			replay_set_level(v, r->instrument, PIS_NONE, 0);
		}
		
		if ((vs->previous_effect != PIS_NONE) && ((vs->previous_effect & 0xF00) == 0)) {
			//
			// Reset to base tone after arpeggio
			//
			opl_set_pitch(v, vs->frequency, vs->octave);
		}
	}	
}


void replay_enter_row_with_note_only(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	
	vs->previous_effect = PIS_NONE;

	if (vs->instrument != PIS_NONE) {
		//
		// Set operator level according to instrument and possibly Cxx effect
		//
		if (EFFECT_HI(r) == 0x0c) {
			replay_set_level(v, vs->instrument, EFFECT_LO(r), 1);
		} else if (vs->volume < 63) {
			replay_set_level(v, vs->instrument, PIS_NONE, 0);
		}		
	}
	//
	// Trigger new note
	//
	replay_set_note(v, vs, r);	
}


void replay_enter_row_with_possibly_effect_only(int v, PisVoiceState *vs, PisRowUnpacked *r) {
		
	//
	// Set operator level according to instrument and Cxx effect
	//
	if (vs->instrument != PIS_NONE && EFFECT_HI(r) == 0x0c) {
		replay_set_level(v, vs->instrument, EFFECT_LO(r), 1);
	}

	if ((vs->previous_effect != PIS_NONE) && ((vs->previous_effect & 0xF00) == 0)) {
		//
		// Reset to base tone after arpeggio
		//
		opl_set_pitch(v, vs->frequency, vs->octave);
	}	
}


void replay_handle_effect(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	int effect_hi = EFFECT_HI(r);
	switch (effect_hi) {
		case 0x00: // arpeggio
			if (EFFECT_LO(r)) {
				replay_handle_arpeggio(v, vs, r);
			} else {
				vs->arpeggio_flag = 0;
			}
			break;
		case 0x01: // slide up
			vs->slide_increment = EFFECT_LO(r);
			break;
		case 0x02: // slide down
			vs->slide_increment = - EFFECT_LO(r);
			break;
		case 0x03: // tone portamento
			replay_set_voice_volatiles(v, 0, 0, EFFECT_LO(r));
			break;
		case 0x0b: // position jump
			replay_handle_posjmp(v, r);
			break;
		case 0x0d: // pattern break
			replay_handle_ptnbreak(v, r);
			break;
		case 0x0e: // Exx commands
			replay_handle_exx_command(v, vs, r);
			break;
		case 0x0f: // set speed
			replay_handle_speed(v, r);
			break;
	}
}


void replay_handle_exx_command(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	switch (EFFECT_MIDNIB(r)) {
		case 0x06: // loop
			replay_handle_loop(v, r);
			break;
		case 0x0a: // volume slide up
		case 0x0b: // volume slide down
			replay_handle_volume_slide(v, vs, r);
			break;
	}
}


void replay_handle_loop(int v, PisRowUnpacked *r) {
	
	if ( ! replay_state.loop_flag) {
		//
		// Playing for the first time
		//
		if (EFFECT_LONIB(r) == 0) {
			//
			// Set loop start row
			//
			replay_state.loop_start_row = replay_state.row;
		} else {
			//
			// Initialize loop counter
			//
			replay_state.loop_count = EFFECT_LONIB(r);
			replay_state.loop_flag = 1;
		}
	}
	
	if ((replay_state.loop_flag) && (EFFECT_LONIB(r))) {
		//
		// Repeating
		//
		replay_state.loop_count--;
		
		if (replay_state.loop_count >= 0) {
			replay_state.row = replay_state.loop_start_row - 1;
		} else {
			replay_state.loop_flag = 0;
		}
	}
}


void replay_handle_volume_slide(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	int level;
	
	if (vs->instrument != PIS_NONE) {
		level = (EFFECT_MIDNIB(r) == 0x0a)
				  ? (vs->volume + EFFECT_LONIB(r))
				  : (vs->volume - EFFECT_LONIB(r));
				  
		if (level < 2)
			level = 2;
		else if (level > 63)
			level = 63;
			
		replay_set_level(v, vs->instrument, level, 0);	
	}	
}


void replay_do_per_frame_effects() {

	replay_state.arpeggio_index++;
	if (replay_state.arpeggio_index == 3) replay_state.arpeggio_index = 0;

	for (int v=0; v<8; v++) {
		PisVoiceState *vs = &replay_state.voice_state[ v ];
		if (vs->slide_increment) {
			vs->frequency += vs->slide_increment;
			opl_set_pitch(v, vs->frequency, vs->octave);						
		} else if (vs->porta_increment) {
			replay_do_per_frame_portamento(v, vs);
		} else if (vs->arpeggio_flag) {
			int freq = vs->arpeggio_freq[ replay_state.arpeggio_index ];
			opl_set_pitch(v, freq, vs->arpeggio_octave[ replay_state.arpeggio_index ]);
		}		
	}
				
}


void replay_do_per_frame_portamento(int v, PisVoiceState *vs) {	
	if (vs->porta_sign == 1) {
		vs->frequency += vs->porta_increment;
		if ((vs->octave == vs->porta_dest_octave) && (vs->frequency > vs->porta_dest_freq)) {
			vs->frequency = vs->porta_dest_freq;
			vs->porta_increment = 0;
		}
		if (vs->frequency > OPL_NOTE_FREQUENCY_HI_B) {
			vs->frequency = OPL_NOTE_FREQUENCY_LO_B + (vs->frequency - OPL_NOTE_FREQUENCY_HI_B);
			vs->octave++;
		}
	} else {
		vs->frequency -= vs->porta_increment;
		if ((vs->octave == vs->porta_dest_octave) && (vs->frequency < vs->porta_dest_freq)) {
			vs->frequency = vs->porta_dest_freq;
			vs->porta_increment = 0;
		}
		if (vs->frequency < OPL_NOTE_FREQUENCY_LO_C) {
			vs->frequency = OPL_NOTE_FREQUENCY_HI_C - (OPL_NOTE_FREQUENCY_LO_C - vs->frequency);
			vs->octave--;
		}
	}
	opl_set_pitch(v, vs->frequency, vs->octave);
}


void replay_handle_arpeggio(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	int an1, an2;
	if (EFFECT_LO(r) != (vs->previous_effect & 0xff)) {
		vs->arpeggio_freq[0] = frequency_table[ vs->note ];
		vs->arpeggio_octave[0] = vs->octave;
		an1 = vs->note + EFFECT_MIDNIB(r);
		an2 = vs->note + EFFECT_LONIB(r);
		if (IS_NOTE(an1)) {
			vs->arpeggio_freq[1] = frequency_table[ an1 ];
			vs->arpeggio_octave[1] = vs->octave;
		} else {
			vs->arpeggio_freq[1] = frequency_table[ an1 - 12 ];
			vs->arpeggio_octave[1] = vs->octave + 1;
		}
		if (IS_NOTE(an2)) {
			vs->arpeggio_freq[2] = frequency_table[ an2 ];
			vs->arpeggio_octave[2] = vs->octave;
		} else {
			vs->arpeggio_freq[2] = frequency_table[ an2 - 12 ];
			vs->arpeggio_octave[2] = vs->octave + 1;
		}
		vs->arpeggio_flag = 1;
	}	
	
	vs->slide_increment = 0;
	vs->porta_increment = 0;
}


void replay_handle_posjmp(int v, PisRowUnpacked *r) {
	replay_reset_voice(v);
	replay_state.position_jump = EFFECT_LO(r);
}


void replay_handle_ptnbreak(int v, PisRowUnpacked *r) {
	replay_reset_voice(v);
	replay_state.pattern_break = EFFECT_LO(r);
}


void replay_handle_speed(int v, PisRowUnpacked *r) {
	replay_reset_voice(v);
	if (EFFECT_LO(r)) {
		replay_state.speed = EFFECT_LO(r);
	} else {
		is_playing = 0;
	}
}


void replay_set_note(int v, PisVoiceState *vs, PisRowUnpacked *r) {
	int frequency = frequency_table[ r->note ];
	opl_set_pitch(v, frequency, r->octave);
	vs->note = r->note;
	vs->octave = r->octave;
	vs->frequency = frequency;
}


void replay_set_instrument(int v, int instr_index) {
	PisInstrument *pinstr = &module.instrument[ instr_index ];
	opl_set_instrument(v, pinstr);
	replay_state.voice_state[v].instrument = instr_index;
}


void replay_set_level(int v, int instr_index, int gain, int do_apply_correction) {
	int base, l1, l2;
	PisInstrument *instr = &module.instrument[ instr_index ];
	
	base = do_apply_correction
	     ? 62
	     : 64;

	if (gain == PIS_NONE) {
		gain = 64;
		replay_state.voice_state[ v ].volume = 63;
	} else {
		replay_state.voice_state[ v ].volume = gain;
	}
	     
	l1 = base - (gain * (64 - instr->lev1) >> 6);
	l2 = base - (gain * (64 - instr->lev2) >> 6);
	
	oplout(0x40 + opl_voice_offset_into_registers[ v ], l1);
	oplout(0x43 + opl_voice_offset_into_registers[ v ], l2);
}


void replay_set_voice_volatiles(int v, int arpeggio_flag, int slide_increment, int porta_increment) {
	PisVoiceState *vs = &replay_state.voice_state[v];
	vs->arpeggio_flag = arpeggio_flag;
	vs->slide_increment = slide_increment;
	vs->porta_increment = porta_increment;
}


void unpack_row() {
	int pattern_index;
	uint32_t *pptn;
	uint32_t packed;
	uint8_t b1, b2, el;
	int note, octave, instrument, effect;
	
	for (int v=0; v<9; v++) {
		pattern_index = module.order[ replay_state.position ][ v ];
		pptn = module.pattern[ pattern_index ];
		packed = pptn[ replay_state.row ];

		el = packed & 0xff;  packed >>= 8;
		b2 = packed & 0xff;  packed >>= 8;
		b1 = packed & 0xff;
	
		replay_state.row_buffer[v].note = b1 >> 4;
		replay_state.row_buffer[v].octave = (b1 >> 1) & 7;
		replay_state.row_buffer[v].instrument = ((b1 & 1) << 4) | (b2 >> 4);
		replay_state.row_buffer[v].effect = ((b2 & 15) << 8) | el;	
	}
}

void advance_row() {
	if (replay_state.position_jump >= 0) {
		replay_state.position = replay_state.position_jump;
		if (replay_state.pattern_break == PIS_NONE) {
			//
			// Position jump without pattern break
			//
			replay_state.row = 0;			
		}
		else {
			//
			// Position jump with pattern break
			//
			replay_state.row = replay_state.pattern_break;
			replay_state.pattern_break = PIS_NONE;
		}
		replay_state.position_jump = PIS_NONE;
	}
	else if (replay_state.pattern_break >= 0) {
		//
		// Pattern break
		//
		replay_state.position++;
		if (replay_state.position == module.length) {
			replay_state.position = 0;
		}
		replay_state.row = replay_state.pattern_break;
		replay_state.pattern_break = PIS_NONE;
	}
	else {
		//
		// Simple row advance
		//
		replay_state.row++;
		if (replay_state.row == 64) {
			replay_state.row = 0;
			replay_state.position++;
			if (replay_state.position == module.length) {
				replay_state.position = 0;
			}
		}
	}
	
	replay_state.count = 0;
}


void load_module(const char *path, PisModule *pmodule) {
	int i, j;
	
	memset(pmodule, 0, sizeof(PisModule));
	FILE *f = fopen(path, "rb");
	assert(f);
	
	pmodule->length = readb(f);
	pmodule->number_of_patterns = readb(f);
	pmodule->number_of_instruments = readb(f);
	
	for (i=0; i<pmodule->number_of_patterns; i++) {
		pmodule->pattern_map[i] = readb(f);
	}

	for (i=0; i<pmodule->number_of_instruments; i++) {
		pmodule->instrument_map[i] = readb(f);
	}

	fread(pmodule->order, 1, 9 * pmodule->length, f);
	
	for (i=0; i<pmodule->number_of_patterns; i++) {
		j = pmodule->pattern_map[i];
		load_pattern(pmodule->pattern[j], f);
	}	

	for (i=0; i<pmodule->number_of_instruments; i++) {
		j = pmodule->instrument_map[i];
		load_instrument(&pmodule->instrument[j], f);
	}
	
	fclose(f);
}


void load_pattern(uint32_t *destination, FILE *f) {
	int row;
	uint32_t packed;
	for (row=0; row<64; row++) {
		packed  = readb(f); packed <<= 8;
		packed |= readb(f); packed <<= 8;
		packed |= readb(f);
		destination[row] = packed;
	}
}


void load_instrument(PisInstrument *pinstr, FILE *f) {
	pinstr->mul1 = readb(f);  pinstr->mul2 = readb(f);
	pinstr->lev1 = readb(f);  pinstr->lev2 = readb(f);
	pinstr->atd1 = readb(f);  pinstr->atd2 = readb(f);
	pinstr->sur1 = readb(f);  pinstr->sur2 = readb(f);
	pinstr->wav1 = readb(f);  pinstr->wav2 = readb(f);
	pinstr->fbcon = readb(f);
}


void opl_set_pitch(int v, int freq, int octave) {
	oplout(0xa0 + v, freq & 0xff);
	oplout(0xb0 + v, 0x20 | (octave << 2) | (freq >> 8));
}


void opl_note_off(int v) {
	oplout(0xb0 + v, 0);
}


void opl_set_instrument(int v, PisInstrument *instr) { 
	int opl_register = 0x20 + opl_voice_offset_into_registers[ v ];
	oplout(opl_register, instr->mul1);  opl_register += 3;
	oplout(opl_register, instr->mul2);  opl_register += 0x1d;
	oplout(opl_register, instr->lev1);  opl_register += 3;
	oplout(opl_register, instr->lev2);  opl_register += 0x1d;
	oplout(opl_register, instr->atd1);  opl_register += 3;
	oplout(opl_register, instr->atd2);  opl_register += 0x1d;
	oplout(opl_register, instr->sur1);  opl_register += 3;
	oplout(opl_register, instr->sur2);  opl_register += 0x5d;
	oplout(opl_register, instr->wav1);  opl_register += 3;
	oplout(opl_register, instr->wav2);  opl_register += 0x1d;
	oplout(0xc0 + v, instr->fbcon);
}


void oplout(int r, int v)
{
  OPLWrite(opl, 0, r);
  OPLWrite(opl, 1, v);
}


void init_audio () {
	SDL_AudioSpec wanted;
	SDL_memset(&wanted, 0, sizeof(wanted));
	wanted.freq = 44100;
	wanted.format = AUDIO_S16;
	wanted.channels = 1;
	wanted.samples = 16384;
	wanted.callback = audio_callback;
	
	int r = SDL_OpenAudio(&wanted, &obtainedAudioSpec);
	assert(r == 0);
	printf("Got audio: 0x%04x, %d Hz, %d ch, %d samples\n",
		obtainedAudioSpec.format,
		obtainedAudioSpec.freq,
		obtainedAudioSpec.channels,
		obtainedAudioSpec.samples);
	
	assert(obtainedAudioSpec.format == AUDIO_S16LSB ||
		   obtainedAudioSpec.format == AUDIO_F32LSB);
	assert(obtainedAudioSpec.channels <= 2);

	samples_per_frame = obtainedAudioSpec.freq / 50;
}


void init_opl () {
	opl = OPLCreate(OPL_TYPE_YM3812, OPL_MAGIC, obtainedAudioSpec.freq);
	assert(opl);
	oplout(1, 0x20); // enable waveform control
}


void audio_callback (void* userdata, Uint8* stream, int numbytes) {
	
	void (*audio_callback_inner)(void*, int) = NULL;

	int numsamples_requested = obtainedAudioSpec.channels == 1
				             ? numbytes >> 1
			                 : numbytes >> 2;
			                 
	int numbytes_chunk;

	if (obtainedAudioSpec.format == AUDIO_F32LSB) {
		numsamples_requested >>= 1;
		audio_callback_inner = audio_callback_float;
	} else {
		audio_callback_inner = audio_callback_s16;
	}

	while (numsamples_requested) {
		int numsamples_chunk = (numsamples_requested > frame_countdown)
							 ? frame_countdown
							 : numsamples_requested;

		frame_countdown -= numsamples_chunk;
		if (frame_countdown == 0) {
			replay_frame_routine();
			frame_countdown = samples_per_frame;
		}
		
		YM3812UpdateOne(opl, fmopl_output_buffer, numsamples_chunk);

		if (obtainedAudioSpec.format == AUDIO_F32LSB) {
			s16tofloat(fmopl_output_buffer, float_buffer, numsamples_chunk);
			numbytes_chunk = numsamples_chunk << 2;
		} else {
			numbytes_chunk = numsamples_chunk << 1;
		}
		if (obtainedAudioSpec.channels == 2) {
			numbytes_chunk <<= 1;
		}
								
		audio_callback_inner(stream, numsamples_chunk);
		stream += numbytes_chunk;
								
		numsamples_requested -= numsamples_chunk;							
	}
}


void audio_callback_float (void *stream, int numsamples) {
	float *psrc = float_buffer;
	float *pdest = stream;
	while (numsamples--) {
		*pdest = *psrc;
		if (obtainedAudioSpec.channels == 2) {
			pdest++;
			*pdest = *psrc;
		}
		psrc++;  pdest++;
	}
}


void audio_callback_s16 (void *stream, int numsamples) {
	INT16 *psrc = fmopl_output_buffer;
	INT16 *pdest = stream;
	while (numsamples--) {
		*pdest = *psrc;
		if (obtainedAudioSpec.channels == 2) {
			pdest++;
			*pdest = *psrc;
		}
		psrc++;  pdest++;
	}
}


void s16tofloat(INT16 *source, float *destination, int numsamples) {
	INT16 samplev_i;
	double samplev_f;
	while (numsamples--) {
		samplev_i = *source;
		samplev_f = (double)samplev_i / 32768.0;
		*destination = (float)samplev_f;
		source++;  destination++;
	}
}
