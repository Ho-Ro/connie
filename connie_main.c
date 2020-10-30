/*****************************************************************************
 *
 *   connie_main.c
 *
 *   Simulation of an electronic organ like Vox Continental
 *   with JACK MIDI input and JACK audio output
 *
 *   Copyright (C) 2009,2010 Martin Homuth-Rosemann
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 ******************************************************************************/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <termios.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>

#include <confuse.h>

#include <jack/jack.h>
#include <jack/midiport.h>


//#define JACK_SESSION

#ifdef JACK_SESSION
#include <jack/session.h>
#endif

#include <fpu_control.h>

#include "connie.h"
#include "connie_ui.h"
#include "reverb.h"
#include "scales.h"

const char * connie_version = "0.4.3-rc6 20100928";
const char * connie_name = "long time gone";
const char * connie_cpu = "";
#ifdef CONNIE_SSE
  const char * connie_cpu = "sse";
#endif
#ifdef CONNIE_I386
  const char * connie_cpu = "i386";
#endif

//////////////////////////////////////////////
//            <USER TUNABLE PART>           //
//////////////////////////////////////////////
//
// "size of the instrument"
#define OCTAVES 5
#define LOWNOTE 24
#define HIGHNOTE (LOWNOTE+12*OCTAVES)

// max "leslie" rotation freq (8 steps)
#define VIBRATO 6.4
//
//////////////////////////////////////////////
//           </USER TUNABLE PART>           //
//////////////////////////////////////////////



// ***********************************************
// tonegen
// ***********************************************


#define MIDI_MAX 128
#define OCT_SAMP (OCTAVES+2)
#define OCT_MIX (OCTAVES+3)
#define NOTE_MAX (LOWNOTE+12*OCT_MIX)
#define MAX_HARMONIC (1<<(OCT_SAMP-1))

// half tone steps
#define OCT 12
#define FIFTH 7
#define THIRD 4

// solution of sample buffers
const int TG_STEP = 8;


// one halftone step
const float tg_halftone = 1.059463094;

// the intonation
int intonation = 0; // default

// tune the instrument
float concert_pitch = 440.0;
int transpose = 0;
const char *inton_name;

// type of instrument
model_t connie_model = CONNIE;

// the jack name
char *jack_name = "connie";

char *uuid = NULL;
char *connie_conf = NULL;


/* Our jack client and the ports */
static jack_client_t *jack_client = NULL;
static jack_port_t *jack_midi_port;
static jack_port_t *jack_audio_port_l;
static jack_port_t *jack_audio_port_r;



typedef jack_default_audio_sample_t sample_t;

// the current sample rate
jack_nframes_t tg_sample_rate;


// one cycle of our sound for diff voices (malloc'ed)
static sample_t *tg_cycle_fl = NULL;
static sample_t *tg_cycle_rd[ OCT_SAMP ];
static sample_t *tg_cycle_sh[ OCT_SAMP ];

// samples in cycle
static jack_nframes_t tg_sam_in_cy;

// table with frequency of each midi note
static float tg_midi_freq[MIDI_MAX];

// sample offset of each tone, advanced by rt_process
static float tg_sample_offset[12];

// actual volume of each note
static int midi_vol_raw[MIDI_MAX]; // from key press/release
static int midi_vol_smooth[MIDI_MAX]; // ramped volume
static int tg_vol_key[MIDI_MAX]; // key volume

// volume of each note after stops mixing
// maybe > MIDI_MAX!
static int tg_vol_note[NOTE_MAX];

// actual value of each midi control
int midi_cc[128];

// the midi pitch - 2000
int midi_pitch = 0;

// the actual midi prog
int midi_prog = 0;

// vibrato frequency
float tg_vibrato   = 0;
// percussion intensity
float tg_percussion = 0;
// reverb intensity
float tg_reverb = 0;

// stops
float tg_vol[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// voices
float tg_vol_fl  = 0;
float tg_vol_rd  = 0;
float tg_vol_sh  = 0;

// master volume
float tg_master_vol = 0.25;

// midi channel 1..16, or 0=all
int tg_midi_channel = 0;


#define VOL_RAW_MAX 1000
static int soft_step[ 2 * VOL_RAW_MAX + 1 ];


//
void tg_panic( void ) {
  for ( int iii = 0; iii < MIDI_MAX; iii++ )
    tg_vol_key[iii] = midi_vol_raw[iii] = 0;
  for ( int iii = 0; iii < NOTE_MAX; iii++ )
    tg_vol_note[iii] = 0;
}



// soft clipping f(x) = x - 1/3 * x^3
static sample_t clip( sample_t sample ) {
  if ( sample > 1.0 )
    sample = 2.0/3.0;
  else if ( sample < -1.0 )
    sample = -2.0/3.0;
  else sample = sample - ( sample * sample * sample ) / 3.0;
  return sample;
}



// returns the sample value of a tone in this octave
// mixes flute, reed and sharp voices
//
static sample_t getsample( unsigned int tone, unsigned int octave ) {
  float foldback_damp = 1.f;
  // "normalize" the tone
  while ( tone >= 12 ) {
    tone -= 12;
    octave++;
  }
  // octave foldback, damp the resulting sample (?)
  while ( octave >= OCT_SAMP ) {
    octave--;
    foldback_damp *= 1.5;
  }
  unsigned int pos = tg_sample_offset[ tone ] * (1<<octave);
  while ( pos >= tg_sam_in_cy )
    pos -= tg_sam_in_cy;

  // flute voice uses sine wave, no average needed
  sample_t sample = tg_cycle_fl[ pos ] * tg_vol_fl;

  if ( CONNIE == connie_model ) {
    // reed and sharp voice use bl waves
    // at octave border B->C a new sample buffer will be used
    // this leads to ugly different sound - solution:
    // average at octave border between samples for both octaves, linear transition
    // weight:
    // Ab:7*act+1*next, A:6a+2n, Bb:5a+3n, B:4a+4n,
    // C:4*prev+4*act, C#:5a+3p, D:6a+2p, D#:7a+1p
    // E, F, F#, G : only active octave
    if ( tg_vol_rd ) {
      if ( octave > 0  && tone < 4 ) {
        sample +=  ( (4-tone) * tg_cycle_rd[ octave-1 ][ pos ]
               + (4+tone) * tg_cycle_rd[ octave ][ pos ] ) * tg_vol_rd / 8 ;
      } else if ( octave < OCT_SAMP-1  && tone > 7 ) {
        sample +=  ( (11+4-tone) * tg_cycle_rd[ octave ][ pos ]
               + (tone-(11-4)) * tg_cycle_rd[ octave+1 ][ pos ] ) * tg_vol_rd / 8 ;
      } else {
        sample +=  tg_cycle_rd[ octave ][ pos ] * tg_vol_rd;
      }
    }
    if ( tg_vol_sh ) {
      if ( octave > 0  && tone < 4 ) {
        sample +=  ( (4-tone) * tg_cycle_sh[ octave-1 ][ pos ]
               + (4+tone) * tg_cycle_sh[ octave ][ pos ] ) * tg_vol_sh / 8 ;
      } else if ( octave < OCT_SAMP-1  && tone > 7 ) {
        sample +=  ( (11+4-tone) * tg_cycle_sh[ octave ][ pos ]
               + (tone-(11-4)) * tg_cycle_sh[ octave+1 ][ pos ] ) * tg_vol_sh / 8 ;
      } else {
        sample +=  tg_cycle_sh[ octave ][ pos ] * tg_vol_sh;
      }
    }
  } // if ( CONNIE )
  return sample / foldback_damp;
}




static int transpose_note( int note )
{
  note += transpose;
  if ( note < LOWNOTE || note > HIGHNOTE )
    return 0;
  else
    return note;
}



// ******************************************
// our realtime process
//
// process midi input and create audio output
// ******************************************
//
static int rt_process_cb( jack_nframes_t nframes, void *void_arg ) {

  // freq modulation for vibrato
  static float shift_offset = 0.f;

  // sampling position
  int pos;
  // voice sample accumulator
  sample_t sample;
  // vibrato fm
  float shift;
  // attac/decay/release
  static int timer = 0;

  // midi events
  // ***********
  //
  jack_nframes_t event_count = 0;
  jack_nframes_t event_index = 0;
  jack_midi_event_t in_event;
  in_event.time = 0xFFFF; // invalid


  // grab our midi input buffer
  void * midi_buffer = jack_port_get_buffer( jack_midi_port, nframes );
  event_count = jack_midi_get_event_count( midi_buffer );
  if ( event_count > 0 ) { // get the first event
    //printf("%d event(s)\n", event_count);
    jack_midi_event_get( &in_event, midi_buffer, 0 );
  }

  // grab our audio output buffer
  sample_t *out_l = (sample_t *) jack_port_get_buffer (jack_audio_port_l, nframes);
  sample_t *out_r = (sample_t *) jack_port_get_buffer (jack_audio_port_r, nframes);

  // fill the buffer
  // this implements the signal flow of an electronic organ
  for ( jack_nframes_t frame = 0; frame < nframes; frame++ ) {

    // process the actual midi in_events ( can be >1 at the same time!)
    while ( (event_index < event_count ) && (in_event.time <= frame) ) {
      // tg_midi_channel = 0: all channels, or 1..16
      if ( 0 == tg_midi_channel || tg_midi_channel-1 ==  ( *(in_event.buffer) & 0xF ) ) {
        //printf( "  %d: %d %d 0x%2X, 0x%2X\n",
        //      event_index, in_event.time, in_event.size, in_event.buffer[0], in_event.buffer[1] );
        if ( in_event.size == 3 ) { // noteon, noteoff, cc
          int note;
          if ( ( in_event.buffer[0] >> 4 ) == 0x08 ) { // note_off note vol
            note = transpose_note( in_event.buffer[1] );
            midi_vol_raw[note]=0;
          } else if ( ( in_event.buffer[0] >> 4 ) == 0x09 ) {// note_on note vol
            note = transpose_note( in_event.buffer[1] );
            if ( in_event.buffer[2] )
              midi_vol_raw[note] = VOL_RAW_MAX;
            else
              midi_vol_raw[note] = 0;
          } else if ( ( in_event.buffer[0] >> 4 ) == 0x0B ) {// cc num val
              int cc = in_event.buffer[1];
              midi_cc[cc] = in_event.buffer[2];
              if ( cc == 7 ) {
                tg_master_vol = in_event.buffer[2] * in_event.buffer[2] / 127.0 / 127.0;
              } else if ( 120 == cc || 123 == cc ) { // all sounds/notes off
                tg_panic();
              }
          } else if ( ( in_event.buffer[0] >> 4 ) == 0x0E ) {// pitch wheel
            midi_pitch = 128 * in_event.buffer[2] + in_event.buffer[1] - 0x2000;
          }
        } else if ( in_event.size == 2 ) { // prog change
          if ( ( in_event.buffer[0] >> 4 ) == 0x0C ) { // prog change
            midi_prog = in_event.buffer[1];
            ui_set_program( midi_prog );
          }
        } // if ( in_event.size ... )
      } // if ( tg_midi_channel )
      // events pending?
      if ( ++event_index < event_count ) {
        jack_midi_event_get( &in_event, midi_buffer, event_index );
      }
    } // while ( (event_index < event_count) && ... )


    // shifting the pitch and volume for (simple) leslie sim
    // shift is a sin signal used for fm and am
    // tg_vibrato 0..1 -> freq 0..1*VIBRATO Hz
    if ( tg_vibrato ) {
      shift_offset += tg_vibrato * VIBRATO / TG_STEP; // shift frequency
      if ( shift_offset >= tg_sam_in_cy )
        shift_offset -= tg_sam_in_cy;
      shift = tg_cycle_fl[ pos = shift_offset ];
    } else {
      shift_offset = shift = 0.0;
    }

    // process the keys (attac/decay/release), do stop mixture
    if ( ++timer > tg_sample_rate / 10000 ) { // 10 kHz -> every 100us
      timer = 0;
      int *p_vol = tg_vol_key + LOWNOTE; // tg_vol_key[note]
      int *p_raw = midi_vol_raw + LOWNOTE;
      int *p_smooth = midi_vol_smooth + LOWNOTE;

      int act_keys = 0;
      if ( tg_percussion ) {
        // count active keys
        for ( int note = LOWNOTE; note < HIGHNOTE; note++ )
          if ( *p_raw++ )
            act_keys++;
      }
      p_raw = midi_vol_raw + LOWNOTE; // restore pointer

      // ramp the midi volumes up/down to remove the clicking at key press/release
      for ( int octave = 0, step=1; octave < OCTAVES; octave++, step*=2 ) {
        for ( int note = 0; note < 12; note++, p_vol++, p_raw++, p_smooth++ ) {
          if ( *p_smooth < *p_raw ) {
            if ( tg_percussion && 1 == act_keys && 0 == *p_smooth ) {
              (*p_smooth) = 2 * VOL_RAW_MAX * tg_percussion; // hard step
            } else {
              (*p_smooth) += 5 * step; // attack quickly up (100 ms)
            }
          } else if ( *p_smooth > *p_raw ) {
            (*p_smooth) -= step ; // decay/release slowly down (500 ms in lowes octave)
          }
          *p_vol = soft_step[ *p_smooth ];
        } // for ( note )
      } // for ( octave )
      // clear all partial volumes
      int *p_note = tg_vol_note;
      for ( int note = 0; note < NOTE_MAX; note++ )
        *p_note++ = 0;

      // prepare pointer
      int *p_key = tg_vol_key + LOWNOTE;
      int *p_16  = tg_vol_note + LOWNOTE - OCT;
      int *p_513 = tg_vol_note + LOWNOTE + FIFTH;
      int *p_8   = tg_vol_note + LOWNOTE;
      int *p_4   = tg_vol_note + LOWNOTE + OCT;
      int *p_223 = tg_vol_note + LOWNOTE + OCT + FIFTH;
      int *p_2   = tg_vol_note + LOWNOTE + OCT + OCT;
      int *p_135 = tg_vol_note + LOWNOTE + OCT + OCT + THIRD;
      int *p_113 = tg_vol_note + LOWNOTE + OCT + OCT + FIFTH;
      int *p_1   = tg_vol_note + LOWNOTE + OCT + OCT + OCT;

      // scan key volumes and mix the note volumes according to the stops
      //
      for ( int key = LOWNOTE; key < HIGHNOTE; key++ ) {
        if ( *p_key ) { // key pressed?
          float *p_vol = tg_vol;
          *p_16  += *p_key * *p_vol++; // vol_16
          *p_513 += *p_key * *p_vol++; // vol_513
          *p_8   += *p_key * *p_vol++;
          *p_4   += *p_key * *p_vol++;
          *p_223 += *p_key * *p_vol++;
          *p_2   += *p_key * *p_vol++;
          *p_135 += *p_key * *p_vol++;
          *p_113 += *p_key * *p_vol++;
          *p_1   += *p_key * *p_vol++; // vol_1
        } // if ( *p_key )
        p_key++;
        p_16++;
        p_513++;
        p_8++;
        p_4++;
        p_223++;
        p_2++;
        p_135++;
        p_113++;
        p_1++;
      } // for ( key )
    } // if /( timer )

    // polyphonic output with drawbars tg_vol_xx
    //
    sample = 0.0;

    int note = LOWNOTE;
    for ( int octave = 0; octave < OCT_MIX; octave++ ) {
      for ( int tone = 0; tone < 12; tone++, note++ ) {
        int vol = tg_vol_note[note];
        if ( vol ) { // note actually playing
          sample += vol * getsample( tone, octave );
        } // if ( vol )
      } // for ( tone )
    } // for ( octave )

    for ( int tone = 0; tone < 12; tone++ ) {
      // advance individual sample pointer, do fm for vibrato
      // vibrato 0..8 -> 0..8 Hz rot. speed
      // typical leslie horn length 0.5 m
      // at rotation speed 1/s the transl. speed of horn mouth ist v=1m/s
      // the doppler formula: f' = f * 1 / ( 1 - v/c )
      // at 1 Hz -> f' = 1 +- 0.003 ( 5 cent shift per Hz )
      // midi pitch bend about +- 2 halftones
      tg_sample_offset[tone] += ( 1.0 + midi_pitch/70000.0 + 0.003 * shift * tg_vibrato * VIBRATO )
                             * tg_midi_freq[LOWNOTE+tone] / TG_STEP;
      if ( tg_sample_offset[tone] >= tg_sam_in_cy ) { // zero crossing
        tg_sample_offset[tone] -= tg_sam_in_cy;
      }
    } // for ( tone )

    // normalize the output
    // tg_vol_16, tg_vol_8, tg_vol_4, tg_vol_IV, tg_vol_fl, tg_vol_rd and tg_vol_sh: range 0..64
    // allow summing of multiple keys, stops, voices
    sample *= tg_master_vol / VOL_RAW_MAX / 16;

    // add some reverb
    sample += tg_reverb * reverb( sample );

    // do soft (valve style) clipping
    sample = 1.2 * clip( sample );
    // sample is now in the range [-0.8..0.8]

    out_l[ frame ] = sample * (1.0f - shift / 5); // 20% (?) am for "leslie"

    out_r[ frame ] = sample * (1.0f + shift / 5); // 20% (?) am for "leslie"

  } // for ( frame )

  return 0;

} // rt_process_cb()



// callback if sample rate changes
static int jack_srate_cb( jack_nframes_t nframes, void *arg ) {
  printf( "connie: JACK sample rate is now %lu/sec\n", (unsigned long)nframes );
  tg_sample_rate = nframes;
  return 0;
}



// callback in case of error
static void jack_error_cb( const char *desc ) {
  fprintf( stderr, "connie: JACK error (%s)\n", desc );
  jack_client = NULL;
  exit( 1 );
}



// callback at jack shutdown
static void jack_shutdown_cb( void *arg ) {
  fprintf( stderr, "connie: JACK shutdown\n" );
  exit( 0 );
}


#ifdef JACK_SESSION
void
session_callback (jack_session_event_t *event, void *arg)
{
  char retval[256];
  printf ("session notification\n");
  printf ("path %s, uuid %s, type: %d\n",
           event->session_dir, event->client_uuid, event->type );


  snprintf (retval, sizeof(retval), "x-terminal-emulator -e \"/tmp/connie -U%s.connie\"", event->session_dir);
  event->command_line = strdup (retval);

  jack_session_reply( jack_client, event );

  ui_save( event->type, event->session_dir );

  jack_session_event_free (event);
}
#endif



// called via atexit()
static void connie_tg_shutdown( void )
{
  // close jack client cleanly (avoid xruns)
  if ( jack_client ) {
    //puts( "client_close()" );
    jack_client_close( jack_client );
    jack_client = NULL;
  }
  // free memory (not necessary)
  if ( tg_cycle_fl )
    free( tg_cycle_fl );
  tg_cycle_fl = NULL;
  for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
    if ( tg_cycle_rd[ octave ] )
      free( tg_cycle_rd[ octave ] );
    tg_cycle_rd[ octave ] = NULL;
    if ( tg_cycle_sh[ octave ] )
      free( tg_cycle_sh[ octave ] );
    tg_cycle_sh[ octave ] = NULL;
  }

} // connie_tg_shutdown()



// The signal handler function to catch ^C, xterm close etc.
static void ctrl_c_handler( int sig) {
  fprintf( stderr, "Signal %d received - aborting...", sig );
  fflush( stderr );
  exit( 0 ); // -> atexit( connie_tg_shutdown )
}  // ctrl_c_handler()





// bandlimited sawtooth and rectangle
// Gibbs smoothing according:
// Joe Wright: Synthesising bandlimited waveforms using wavetables
// www.musicdsp.org/files/bandlimited.pdf
//
static sample_t saw_bl( float arg, int order, int partials ) {
  while ( arg >= 2 * M_PI )
    arg -= 2 * M_PI;
  sample_t result = 0.0;
  float k = M_PI / 2 / partials;
  for ( int n = order; n <= partials; n += order ) {
    float m = cosf( (n-1) * k );
    m = m * m;
    result += sinf( n * arg ) / n * m;
  }
  return result;
}



static sample_t rect_bl( float arg, int order, int partials ) {
  while ( arg >= 2 * M_PI )
    arg -= 2 * M_PI;
  sample_t result = 0.0;
  float k = M_PI / 2 / partials;
  for ( int n = order; n <= partials; n += 2 * order ) {
    float m = cosf( (n-1) * k );
    m = m * m;
    result += sinf( n * arg ) / n * m;
  }
  return result;
}



static void tg_init( int tg_sample_rate )
{
  // build list of eq. tuned midi frequencies starting from lowest C (note 0)
  // (three halftones above the very low A six octaves down from a' 440 Hz)

  float feq = concert_pitch / 64 * tg_halftone * tg_halftone * tg_halftone;
  float low_C = concert_pitch / 32.0 / scales[intonation].f_ratio[9];

  // build a list of intonation frequencies
  // alternative tunings are possible
  for ( int midinote = 0; midinote < 128; midinote++ ) {
    int tone = midinote % 12; // C, C#, D,..., B
    int fmult = 1 << (midinote / 12); // doubles every octave
    float f = scales[intonation].f_ratio[ tone ] * low_C * fmult;
    //printf( "%s\t%d\t%d\t%d\t%f\t%f\n", scales[intonation].label, midinote, tone, fmult, feq, f );
    tg_midi_freq[ midinote ] = f;
    feq *= tg_halftone;
    midi_vol_raw[ midinote ] = 0;
    tg_vol_key[ midinote ] = 0;
    tg_vol_note[ midinote ] = 0;
  } // for ( midinote )

  // set the starting phase of the 12 tones
  for ( int tone = 0; tone < 12; tone++ ) {
    tg_sample_offset[ tone ] = 0.0;
  }

  // create 1 cycle of the wave
  // calculate the number of samples in one cycle of the wave
  tg_sam_in_cy = tg_sample_rate / TG_STEP + 1;


  // one size fits all (flute)
  tg_cycle_fl = (sample_t *) malloc( tg_sam_in_cy * sizeof( sample_t ) );
  // exit if allocation failed
  if ( tg_cycle_fl == NULL ) {
    fprintf( stderr,"memory allocation failed\n" );
    exit( 1 );
  }

  // reed and sharp voices
  if ( CONNIE == connie_model ) {
    // allocate the space needed to store one cycle
    // use own buffer for each octave (reed voice)
    for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
      tg_cycle_rd[ octave ] = (sample_t *) malloc( tg_sam_in_cy * sizeof( sample_t ) );
      if ( tg_cycle_rd[ octave ] == NULL ) {
        fprintf( stderr,"memory allocation failed\n" );
        exit( 1 );
      }
      // use own buffer for each octave (sharp voice)
      tg_cycle_sh[ octave ] = (sample_t *) malloc( tg_sam_in_cy * sizeof( sample_t ) );
      if ( tg_cycle_sh[ octave ] == NULL ) {
        fprintf( stderr,"memory allocation failed\n" );
        exit( 1 );
      }
    }
  } // if ( CONNIE )

  // calculate our scale multiplier
  sample_t scale = 2 * M_PI / tg_sam_in_cy;
  printf( "Preparing the voices" );
  // and fill it up with one period of sine wave
  // maybe a RC filtered square wave sounds more natural
  for ( int i=0; i < tg_sam_in_cy; i++ ) {
    tg_cycle_fl[i] = sinf( i * scale ); // flute
  }

  // reed and sharp
  if ( CONNIE == connie_model ) {
    // fill sample buffer with bandlimited wave for each octave
    for ( int oct = 0; oct < OCT_SAMP; oct++ ) {
      // max partial < tg_sample_rate/3 for highest note in this octave
      // sr / 3 to reduce aliasing effects
      int partials = tg_sample_rate / 2.0 / tg_midi_freq[ LOWNOTE + 12 * oct + 12 ];
      printf( "." );
      fflush( stdout );
      for ( int i=0; i < tg_sam_in_cy; i++ ) {
        tg_cycle_rd[ oct ][ i ] = rect_bl( i * scale, 1, partials ); // reed
        tg_cycle_sh[ oct ][ i ] =  saw_bl( i * scale, 1, partials ); // sharp
      }
    }
  } // if ( CONNIE )

  // sin**2 for smoothing the steps
  for ( int vol = 0; vol <= VOL_RAW_MAX; vol++ ) {
    soft_step[ vol ] = VOL_RAW_MAX * ( 0.5 - 0.5 * cosf( M_PI * vol / VOL_RAW_MAX ) ) + 0.5f;
    soft_step[ vol + VOL_RAW_MAX ] = vol + VOL_RAW_MAX;
  }
  puts("");
}




int main( int argc, char *argv[] ) {

  // set FPU mode "Round To Zero"
  // letting denormal numbers in IIR _slowly_ fade away
  // BUT: "it's better to burn out than to fade away"
  // use function daz() "denormals are zero" in reverb
  // manipulate FPU Control Word (<fpu_contol.h>)
  fpu_control_t cw;
  _FPU_GETCW( cw );
  cw |= _FPU_RC_ZERO;
  _FPU_SETCW( cw );


// registering the handler, catching terminating signals

  signal( SIGHUP,  ctrl_c_handler ); // xterm close
  signal( SIGINT,  ctrl_c_handler ); // ^C
  signal( SIGQUIT, ctrl_c_handler );
  signal( SIGABRT, ctrl_c_handler );
  signal( SIGTERM, ctrl_c_handler );


  int c;
  int autoconnect = 0;
  char *midi_port = NULL;
  int printhelp = 0;
  keybd_t keybd = QWERTY;

  int drawbars[20] = { 0 };

  opterr = 0;
  while ((c = getopt (argc, argv, "ac:fghi:m:n:p:s:t:vC:U:")) != -1) {
    switch (c) {
      case 'a':
        autoconnect = 1;
        printf( "autoconnect\n" );
        break;
      case 'c':
        tg_midi_channel = atoi( optarg );
        if ( tg_midi_channel < 0 || tg_midi_channel > 16 )
          tg_midi_channel = 0;
        printf( "midi channel %d\n", tg_midi_channel );
        break;
      case 'f':
        keybd = AZERTY;
        printf( "french AZERTY kbd\n" );
        break;
      case 'g':
        keybd = QWERTZ;
        printf( "german QWERTZ kbd\n" );
        break;
      case 'h':
        printhelp = 1;
        break;
      case 'i':
        connie_model = atoi( optarg );
        if ( connie_model < 0 || connie_model > HAMMOND )
          connie_model = CONNIE;
        printf( "instrument: %d\n", connie_model );
        break;
      case 'm':
        midi_port = optarg;
        printf( "MIDI port: %s\n", midi_port );
        break;
      case 'n':
        jack_name = optarg;
        printf( "jack_name: %s\n", jack_name );
        break;
      case 'p':
        concert_pitch = atof( optarg );
        if ( concert_pitch < 220 || concert_pitch > 880 )
          concert_pitch = 440.0;
        printf( "concert pitch = %5.1f Hz\n", concert_pitch );
        break;
      case 's':
        intonation = atoi( optarg );
        if ( intonation < 0 || intonation >= NSCALES )
          intonation = 0;
        inton_name = scales[intonation].label;
        printf( "%s\n", inton_name );
        break;
      case 't':
        transpose = atoi( optarg );
        if ( transpose < -12 || transpose > 12 )
          transpose = 0;
        printf( "transpose %d semitones\n", transpose );
        break;
      case 'v':
        printf( "%s_%s %s (%s)\n", jack_name, connie_cpu, connie_version, connie_name );
        exit( 1 );
      case 'C':
        connie_conf = optarg;
        cfg_opt_t opts[] = {
          CFG_STR( "UUID", NULL, CFGF_NONE),
          CFG_STR( "jack_name", "connie", CFGF_NONE ),
          CFG_INT( "connie_model", 0, CFGF_NONE ),
          CFG_INT( "keybd", 0, CFGF_NONE ),
          CFG_INT( "intonation", 0, CFGF_NONE ),
          CFG_FLOAT( "concert_pitch", 440.0, CFGF_NONE ),
          CFG_INT( "transpose", 0, CFGF_NONE ),
          CFG_INT( "midi_channel", 0, CFGF_NONE ),
          CFG_INT_LIST( "drawbars", 0, CFGF_NONE),
          CFG_END()
        };
        cfg_t *cfg;

        cfg = cfg_init( opts, CFGF_NONE );
        if ( cfg_parse( cfg, connie_conf ) == CFG_PARSE_ERROR )
          exit( 1 );

        if ( !uuid && cfg_getstr( cfg, "UUID" ) )
          uuid        = strdup( cfg_getstr( cfg, "UUID" ) );
        jack_name     = strdup( cfg_getstr( cfg, "jack_name" ) );
        connie_model  = cfg_getint( cfg, "connie_model" );
        keybd         = cfg_getint( cfg, "keybd" );
        intonation    = cfg_getint( cfg, "intonation" );
        concert_pitch = cfg_getfloat( cfg, "concert_pitch" );
        transpose     = cfg_getint( cfg, "transpose" );
        tg_midi_channel  = cfg_getint( cfg, "midi_channel" );
        drawbars[0]   = cfg_size( cfg, "drawbars" );
        for (int iii = 0; iii < drawbars[0]; iii++ ) {
	  drawbars[ iii+1 ] = cfg_getnint(  cfg, "drawbars", iii );
	}
        cfg_free(cfg);
        break;
      case 'U':
        uuid = optarg;
        break;
      case '?':
        if ( 'c' == optopt || 'i' == optopt || 'm' == optopt || 'n' == optopt
          || 'p' == optopt || 's' == optopt || 't' == optopt
          || 'C' == optopt || 'U' == optopt )
          fprintf (stderr, "Option `-%c' requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        // fall through
      default:
        printhelp = 1;
        break;
    }
  }
  inton_name = scales[intonation].label;


  if ( printhelp ) {
    printf( "usage: connie [opts]\n" );
    printf( "  -a\t\t\tautoconnect to system:playback ports\n" );
    printf( "  -c CHANNEL\t\tMIDI channel (1..16), 0=all (default)\n" );
    printf( "  -f\t\t\tfrench AZERTY keyboard\n" );
    printf( "  -g\t\t\tgerman QWERTZ keyboard\n" );
    printf( "  -h\t\t\tthis help msg\n" );
    printf( "  -i INSTRUMENT\t\t0: connie (default), 1: poor-man's-hammond\n" );
    printf( "  -m MIDI_PORT\t\tconnect with midi port\n" );
    printf( "  -p PITCH\t\tconcert pitch 220..880 Hz\n" );
    printf( "  -s INTONATION_SCALE\t 0: %s\n", scales->label );
    for ( int iii = 1; iii < NSCALES; iii++ ) {
      printf( "\t\t\t%2d: %s\n", iii, scales[iii].label );
    }
    printf( "  -t TRANSPOSE\t\ttranspose -12..+12 semitones\n" );
    printf( "  -v\t\t\tprint version\n" );
    printf( "  -C configfile\t\tload config file\n" );
    printf( "  -U UUID\t\tset jack session UUID\n" );
    exit( 1 );
  }



  //
  // ******************************************************
  // * For more info about writing a JACK client look at: *
  // *  http://dis-dot-dat.net/index.cgi?item=jacktuts/   *
  // ******************************************************
  //

  // tell the JACK server to call error_cb() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.

  // This is set here so that it can catch errors in the
  // connection process

  jack_set_error_function( jack_error_cb );

//  // try to become a client of the JACK server

//  if ( (jack_client = jack_client_open( jack_name, 0, NULL ) ) == 0 ) {
//    fprintf( stderr, "jack server not running?\n" );
//    return 1;
//  }


  jack_status_t status;

  /* open a client connection to the JACK server */

#ifdef JACK_SESSION
  if( !uuid ) {
    jack_client = jack_client_open( jack_name, JackNullOption, &status );
  } else {
    printf( "UUID %s\n", uuid );
    jack_client = jack_client_open( jack_name, JackSessionID, &status, uuid );
  }
#else
  jack_client = jack_client_open( jack_name, JackNullOption, &status );
#endif

  if ( jack_client == NULL) {
    fprintf (stderr, "jack_client_open() failed, "
             "status = 0x%2.0x\n", status);
    if (status & JackServerFailed) {
      fprintf (stderr, "Unable to connect to JACK server\n");
    }
    exit (1);
  }
  if (status & JackServerStarted) {
    fprintf (stderr, "JACK server started\n");
  }
  if (status & JackNameNotUnique) {
    jack_name = jack_get_client_name( jack_client );
    fprintf (stderr, "unique name `%s' assigned\n", jack_name);
  }



  // get the individual name
  // jack_name = jack_get_client_name( jack_client );

  // tell the JACK server to call `rt_process_cb()' whenever
  // there is work to be done.

  jack_set_process_callback( jack_client, rt_process_cb, 0 );


  // tell the JACK server to call `srate_cb()' whenever
  // the sample rate of the system changes.

  jack_set_sample_rate_callback( jack_client, jack_srate_cb, 0 );


  // tell the JACK server to call `jack_shutdown_cb()'
  // if it ever shuts down, either entirely, or if it
  // just decides to stop calling us.

  jack_on_shutdown( jack_client, jack_shutdown_cb, 0 );


#ifdef JACK_SESSION
  /* tell the JACK server to call `session_callback()' if
     the session is saved.
   */

  jack_set_session_callback( jack_client, session_callback, NULL );
#endif

  // display the current sample rate. once the client is activated
  // (see below), you should rely on your own sample rate
  // callback (see above) for this value.

  tg_sample_rate = jack_get_sample_rate( jack_client );
  printf( "sample rate: %lu/sec\n", (unsigned long)tg_sample_rate );


  // init the tonegen _after_ the call to jack_get_sample_rate()
  tg_init( tg_sample_rate );


  // create one midi and two audio ports
  jack_midi_port = jack_port_register( jack_client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  jack_audio_port_l = jack_port_register( jack_client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  jack_audio_port_r = jack_port_register( jack_client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);


  // tell the JACK server that we are ready to roll
  if (jack_activate( jack_client ) ) {
    fprintf( stderr, "cannot activate client\n" );
    exit( 1 );
  }

  // exit cleanly
  atexit( connie_tg_shutdown );

  // autoconnect if cmd line option
  if ( autoconnect ) {
    const char **jack_ports, **pp;
    //autoconnect to system playback ports
    if ( ( jack_ports = jack_get_ports( jack_client, NULL, NULL, JackPortIsPhysical|JackPortIsInput ) ) == NULL) {
      fprintf( stderr, "connie: cannot find any physical playback ports\n" );
      exit(1);
    }
    pp = jack_ports;
    while ( *pp ) {
      //puts( *pp );
      if ( !jack_connect( jack_client, jack_port_name( jack_audio_port_l ), *pp++ )
       &&  !jack_connect( jack_client, jack_port_name( jack_audio_port_r ), *pp++ ) )
         break;
    }
    free( jack_ports );
  }
  if ( midi_port ) {
    if ( jack_connect( jack_client, midi_port, jack_port_name( jack_midi_port ) ) ) {
      fprintf( stderr, "connie: cannot connect %s - %s\n", midi_port, jack_port_name( jack_midi_port ) );
      exit( 1 );
    }
  }

  // start the user interface
  ui_init( connie_model, keybd );


  if ( drawbars[0] ) {
    ui_set_drawbars( drawbars );
  }


  ui_loop( connie_name );
  // connie_shutdown() called via atexit()

  exit( 0 );
}

