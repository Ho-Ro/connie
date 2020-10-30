/*****************************************************************************
 *
 *   connie.c
 *
 *   Simulation of an electronic organ like Vox Continental
 *   with JACK MIDI input and JACK audio output
 *
 *   Copyright (C) 2009 Martin Homuth-Rosemann
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
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <termios.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>

#include <jack/jack.h>
#include <jack/midiport.h>



const char * connie_version = "0.4.0";
const char * connie_name = "land ho!";



//////////////////////////////////////////////
//            <USER TUNABLE PART>           //
//////////////////////////////////////////////
//
// tune the instrument
const float concert_pitch_440 = 440.0;

// "size of the instrument"
#define OCTAVES 5
#define LOWNOTE 24
#define HIGHNOTE (LOWNOTE+12*OCTAVES)

// max "leslie" rotation freq
#define VIBRATO 8
//
#define PROGRAMS 7
// the drawbar volumes (vol_xx = 0..8)
// some program presets
int preset[PROGRAMS][9] = { 
    { 6, 8, 6, 8, 8, 4, 0, 0, 0 }, // preset 0
    { 0, 8, 6, 8, 4, 8, 4, 0, 0 }, // preset 1
    { 0, 8, 8, 8, 0, 8, 8, 0, 0 }, // preset 2
    { 4, 8, 4, 6, 8, 4, 0, 1, 0 }, // preset 3
    { 4, 8, 6, 4, 8, 0, 0, 2, 0 }, // preset 4
    { 8, 7, 6, 5, 8, 4, 4, 4, 0 }, // preset 5
    { 0, 0, 0, 8, 8, 0, 0, 0, 0 }, // preset 6
};
//
//////////////////////////////////////////////
//           </USER TUNABLE PART>           //
//////////////////////////////////////////////



#define OCT_SAMP (OCTAVES+2)
#define MAX_HARMONIC (1<<(OCT_SAMP-1))

// half tone steps
#define OCT 12
#define FIFTH 7
#define THIRD 4

// solution of sample buffers
const int STEP = 8;


// one halftone step 
const float halftone = 1.059463094;

const float PI = 3.14159265;


/* Our jack client and the ports */
jack_client_t *client = NULL;
jack_port_t *midi_port;
jack_port_t *output_port_l;
jack_port_t *output_port_r;

// the original terminal io settings (needed by atexit() function)
struct termios term_orig;


typedef jack_default_audio_sample_t sample_t;

// the current sample rate
jack_nframes_t samplerate;


// one cycle of our sound for diff voices (malloc'ed)
sample_t *cycle_fl = NULL;
sample_t *cycle_rd[ OCT_SAMP ];
sample_t *cycle_sh[ OCT_SAMP ];

// samples in cycle
jack_nframes_t samincy;


// table with frequency of each midi note
float midi_freq[128];

// sample offset of each tone, advanced by rt_process
float sample_offset[12];

// actual volume of each note
int midi_vol_raw[128]; // from key press/release
int vol_key[128]; // smoothed/percussed key volume
int vol_note[128]; // volume of each note after stops mixing

// actual value of each midi control
int midi_cc[128];

// the master midi volume (cc07) (master_volume = 0..127)
int master = 64;

// the midi pitch - 2000
int midi_pitch = 0;

// the actual midi prog
int midi_prog = 0;

int value_changed = 0;

// stops
int draw_16  = 6;
int draw_8   = 8;
int draw_4   = 6;
int draw_IV  = 8;

// the "missing" drawbars
//int draw_513   = 0;
//int draw_223   = 0;
//int draw_2     = 0;
//int draw_135   = 0;
//int draw_113   = 0;
//int draw_1     = 0;

// voices
int draw_fl = 8;
int draw_rd  = 4;
int draw_sh = 4;

// vibrato frequency
int vibrato   = 0;
// percussion on/off
int percussion = 0;

int vol_16  = 0;
int vol_513 = 0;
int vol_8   = 0;
int vol_4   = 0;
int vol_223 = 0;
int vol_2   = 0;
int vol_135 = 0;
int vol_113 = 0;
int vol_1   = 0;

int vol_fl = 0;
int vol_rd = 0;
int vol_sh = 0;

int master_volume = 64*64;

// avoid nasty warning
extern int usleep( int );



void set_volumes( void ) {
  vol_16  = draw_16 * draw_16;
  vol_8   = draw_8  * draw_8;
  vol_4   = draw_4  * draw_4;
  vol_223 = draw_IV * draw_IV;
  vol_2   = draw_IV * draw_IV;
  vol_135 = draw_IV * draw_IV;
  vol_1   = draw_IV * draw_IV;

  vol_fl = draw_fl * draw_fl;
  vol_rd = draw_rd * draw_rd;
  vol_sh = draw_sh * draw_sh;
}



void set_program( int prog ) {
  if ( prog >= 0 && prog < PROGRAMS ) {
    // stops
    draw_16    = preset[prog][0];
    draw_8     = preset[prog][1];
    draw_4     = preset[prog][2];
    draw_IV    = preset[prog][3];
    // voices
    draw_fl    = preset[prog][4];
    draw_rd    = preset[prog][5];
    draw_sh    = preset[prog][6];
    // vibrato 
    vibrato    = preset[prog][7];
    // percussion
    percussion = preset[prog][8];
    set_volumes();
  }
}



void panic( void ) {
  for ( int iii = 0; iii < 128; iii++ )
    vol_note[iii] = vol_key[iii] = midi_vol_raw[iii] = 0;
}



// soft clipping f(x) = x - 1/3 * x^3
sample_t clip( sample_t sample ) {
  if ( sample > 1.0 )
    sample = 2.0/3.0;
  else if ( sample < -1.0 )
    sample = -2.0/3.0;
  else sample = sample - ( sample * sample * sample ) / 3;
  return sample;
}



// returns the sample value of a tone in this octave
// mixes flute, reed and sharp voices
// range [-64.0;+64.0] (because of vol_xx = 0..64)
sample_t getsample( unsigned int tone, unsigned int octave ) {
  while ( tone >= 12 ) {
    tone -= 12;
    octave++;
  }
  if ( octave > OCT_SAMP )
    octave = OCT_SAMP;
  unsigned int pos = sample_offset[ tone ] * (1<<octave);
  while ( pos >= samincy )
    pos -= samincy;

  sample_t sample = cycle_fl[ pos ] * vol_fl;
  // at octave border B->C a new sample buffer will be used
  // this leads to ugly different sound - solution:
  // average at octave border between samples for both octaves, linear transition
  // weight: Ab:7*act+1*next, A:6a+2n, Bb:5a+3n, B:4a+4n, C:4prev+4act, C#:5a+3p, D:6a+2p, D#:7a+1p
  if ( octave > 0  && tone < 4 ) {
    sample +=  ( (4-tone) * cycle_rd[ octave-1 ][ pos ]
           + (4+tone) * cycle_rd[ octave ][ pos ] ) * vol_rd / 8 ;
    sample +=  ( (4-tone) * cycle_sh[ octave-1 ][ pos ]
           + (4+tone) * cycle_sh[ octave ][ pos ] ) * vol_sh / 8 ;
  } else if ( octave < OCTAVES-1  && tone > 7 ) {
    sample +=  ( (11+4-tone) * cycle_rd[ octave ][ pos ]
           + (tone-(11-4)) * cycle_rd[ octave+1 ][ pos ] ) * vol_rd / 8 ;
    sample +=  ( (11+4-tone) * cycle_sh[ octave ][ pos ]
           + (tone-(11-4)) * cycle_sh[ octave+1 ][ pos ] ) * vol_sh / 8 ;
  } else {
    sample +=  cycle_rd[octave][ pos ] * vol_rd;
    sample +=  cycle_sh[octave][ pos ] * vol_sh;
  }
  return sample;
}



// ********************
// our realtime process
// ********************
//
int rt_process_cb( jack_nframes_t nframes, void *void_arg ) {

  // freq modulation for vibrato
  static float shift_offset = 0;

  // process midi events
  // *******************
  // (inspired from fluidjack.c from Nedko Arnaudov)
  //
  jack_nframes_t event_count;
  jack_midi_event_t event;

  // grab our midi input buffer
  void * midi_buffer = jack_port_get_buffer( midi_port, nframes );
  // check for events and process them (ignore event time)
  event_count = jack_midi_get_event_count( midi_buffer );
  for ( jack_nframes_t iii=0; iii < event_count; iii++ ) {
    jack_midi_event_get( &event, midi_buffer, iii );
    if ( event.size == 3 ) { // noteon, noteoff, cc
      int note;
      if ( ( event.buffer[0] >> 4 ) == 0x08 ) { // note_off note vol
        note = event.buffer[1];
        midi_vol_raw[note]=0;
      } else if ( ( event.buffer[0] >> 4 ) == 0x09 ) {// note_on note vol
        note = event.buffer[1];
        if ( event.buffer[2] )
          midi_vol_raw[note] = 64;
        else
          midi_vol_raw[note] = 0;
      } else if ( ( event.buffer[0] >> 4 ) == 0x0B ) {// cc num val
          int cc = event.buffer[1];
          midi_cc[cc] = event.buffer[2];
          if ( cc == 7 ) {
            master = event.buffer[2];
            master_volume = master * master;
          } else if ( 120 == cc || 123 == cc ) { // all sounds/notes off
            panic();
          }
      } else if ( ( event.buffer[0] >> 4 ) == 0x0E ) {// pitch wheel
        midi_pitch = 128 * event.buffer[2] + event.buffer[1] - 0x2000;
      }
    } else if ( event.size == 2 ) { // prog change
      if ( ( event.buffer[0] >> 4 ) == 0x0C ) { // prog change
        midi_prog = event.buffer[1];
        set_program( midi_prog );
        value_changed = 1;
      }
    }
  } // for


  // create audio output
  // *******************
  //
  // sampling position
  int pos;
  // voice samples 
  sample_t sample;
  float shift;

  static int timer = 0;

  // grab our audio output buffer 
  sample_t *out_l = (sample_t *) jack_port_get_buffer (output_port_l, nframes);
  sample_t *out_r = (sample_t *) jack_port_get_buffer (output_port_r, nframes);

  // fill the buffer
  // this implements the signal flow of an electronic organ
  for ( jack_nframes_t frame = 0; frame < nframes; frame++ ) {

    // shifting the pitch and volume for (simple) leslie sim
    // shift is a sin signal used for fm and am
    // vibrato 0..8 -> freq 0..VIBRATO Hz
    shift_offset += vibrato * VIBRATO / 8.0 / STEP; // shift frequency
    if ( shift_offset >= samincy )
      shift_offset -= samincy;
    if ( vibrato )
      shift = cycle_fl[ pos = shift_offset ];
    else
      shift = 0.0;

    if ( ++timer > samplerate / 4000 ) { // 4 kHz -> every 250us
      timer = 0;
      int *p_vol = vol_key + LOWNOTE; // vol_key[note]
      int *p_raw = midi_vol_raw + LOWNOTE;
      // ramp the midi volumes up/down to remove the clicking at key press/release
      for ( int note = LOWNOTE; note < HIGHNOTE; note++, p_vol++, p_raw++ ) {
        if ( *p_vol < *p_raw ) {
          if ( percussion ) // attack immediate up to 64..192
            (*p_vol) = 64 + 16 * percussion; // decay to 64 (0..48 ms)
          else
            (*p_vol) += 8; // quick up (2 ms)
        } else if ( *p_vol > *p_raw ) {
          (*p_vol)--; // release slow down (16 ms)
        }
      } // for ( note )

      // clear all partial volumes
      int *p_note = vol_note;
      for ( int note = 0; note < 128; note++ )
        *p_note++ = 0;

      // prepare pointer
      int *p_key = vol_key + LOWNOTE;
      int *p_16  = vol_note + LOWNOTE - OCT;
      //int *p_513 = vol_note + LOWNOTE - OCT + FIFTH;
      int *p_8   = vol_note + LOWNOTE;
      int *p_4   = vol_note + LOWNOTE + OCT;
      int *p_223 = vol_note + LOWNOTE + OCT + FIFTH;
      int *p_2   = vol_note + LOWNOTE + OCT + OCT;
      int *p_135 = vol_note + LOWNOTE + OCT + OCT + THIRD;
      //int *p_113 = vol_note + LOWNOTE + OCT + OCT + FIFTH;
      int *p_1   = vol_note + LOWNOTE + OCT + OCT + OCT;

      // scan key volumes and mix the not volumes according to the stops
      // 
      for ( int key = LOWNOTE; key < HIGHNOTE; key++ ) {
        // octave foldback
        if ( HIGHNOTE-OCT == key ) {
          p_1 -= OCT;
        //} else if ( HIGHNOTE-FIFTH == key ) {
        //  p_113 -= OCT;
        } else if ( HIGHNOTE-THIRD == key ) {
          p_135 -= OCT;
        }
        if ( *p_key ) { // key pressed?
          *p_16  += *p_key * vol_16;
          //*p_513 += *p_key * vol_513;
          *p_8   += *p_key * vol_8;
          *p_4   += *p_key * vol_4;
          *p_223 += *p_key * vol_223;
          *p_2   += *p_key * vol_2;
          *p_135 += *p_key * vol_135;
          //*p_113 += *p_key * vol_113;
          *p_1   += *p_key * vol_1;
        } // if ( *p_key )
        p_key++;
        p_16++;
        //p_513++;
        p_8++;
        p_4++;
        p_223++;
        p_2++;
        p_135++;
        //p_113++;
        p_1++;
      } // for ( key )
    } // if /( timer )

    // polyphonic output with drawbars vol_xx
    //
    sample = 0.0;

    int note = LOWNOTE;
    for ( int octave = 0, harmonic = 1; octave < OCT_SAMP; octave++, harmonic *= 2 ) { // 6 octaves 1,2,4,8,16,32
      for ( int tone = 0; tone < 12; tone++, note++ ) {
        int vol = vol_note[note];
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
      sample_offset[tone] += ( 1.0 + midi_pitch/70000.0 + 0.003 * shift * vibrato * VIBRATO / 8.0 ) 
                             * midi_freq[LOWNOTE+tone] / STEP;
      if ( sample_offset[tone] >= samincy ) { // zero crossing
        sample_offset[tone] -= samincy;
      }
    } // for ( tone )

    // normalize the output
    // vol_16, vol_8, vol_4, vol_IV, vol_fl, vol_rd and vol_sh: range 0..64
    // vol_key 0..64
    // master_volume 
    // allow summing of multiple keys, stops, voices without hard limiting
    sample *= 0.1 * master / (127.0 * 127.0)  / ( 64 * 64 );

    // soft clipping, leave 20% headroom for leslie am
    sample = 1.25 * clip( sample );

    // TODO: adjust value of leslie am index
    out_l[frame] = sample * (1.0f - shift / 10); // 10% (?) am for "leslie"
    out_r[frame] = sample * (1.0f + shift / 10); // "

  } // for ( frame )

  return 0;

} // rt_process_cb()



// callback if sample rate changes
int srate_cb( jack_nframes_t nframes, void *arg ) {
  printf( "the sample rate is now %lu/sec\n", (unsigned long)nframes );
  samplerate = nframes;
  return 0;
}



// callback in case of error
void error_cb( const char *desc ) {
  fprintf( stderr, "JACK error: %s\n", desc );
  exit( 1 );
}



// callback at jack shutdown
void jack_shutdown_cb( void *arg ) {
  exit( 1 );
}



// called via atexit()
void connie_shutdown( void )
{
  // restore term settings
  tcsetattr( 1, 0, &term_orig );

  // close jack client cleanly
  if ( client ) {
    //puts( "client_close()" );
    jack_client_close( client );
    client = NULL;
  }
  if ( cycle_fl )
    free( cycle_fl );
  cycle_fl = NULL;
  for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
    if ( cycle_rd[ octave ] )
      free( cycle_rd[ octave ] );
    cycle_rd[ octave ] = NULL;
    if ( cycle_sh[ octave ] )
      free( cycle_sh[ octave ] );
    cycle_sh[ octave ] = NULL;
  }

  printf("\n");
} // connie_shutdown()



// The signal handler function to catch ^C
void ctrl_c_handler( int sig) {
  //signal( SIGINT, ctrl_c_handler );
  printf( "Abort...");
  exit( 0 ); // -> connie_shutdown()
}  // ctrl_c_handler()



int kbhit()
{
  struct timeval tv;
  fd_set fds;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
  select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
  return FD_ISSET(STDIN_FILENO, &fds);
}



void print_help( void ) {
  puts( "\n\n\n\n" );
  printf( "connie %s (%s)\n\n", connie_version, connie_name );
  puts( "[ESC]\t\t\tQUIT\n[SPACE]\t\t\tPANIC" );
  puts( "Q-A  W-S  E-D  R-F\t16'  8'  4'  IV  stops");
  puts( "T-G  Y/Z-H  U-J\t\tflute  reed  sharp voices");
  puts( "I-K\t\t\tvibrato" );
  puts( "O-L\t\t\tpercussion");
  puts( "1  2  3  4  5  6\tpresets\n");
}



void print_status( void ) {
  printf( "+-------------------------------------------+\n" );
  printf( "| 16'  8'  4'  IV    ~   M   sh   vib  perc |\n" );
  printf( "|  %d   %d   %d   %d     %d   %d   %d     %d     %d  |\n",
          draw_16, draw_8, draw_4, draw_IV, draw_fl, draw_rd, draw_sh, vibrato, percussion );
  //printf( "|                                       |\n" );
  printf( "+--Q---W---E---R-----T--Y/Z--U-----I-----O--+\n" );
  for ( int line = 0; line < 8; line++ ) {
    printf( "|  %c   %c   %c   %c     %c   %c   %c     %c     %c  |\n", 
  	draw_16>line?'#':' ', draw_8>line?'#':' ', 
  	draw_4>line?'#':' ', draw_IV>line?'#':' ', 
  	draw_fl>line?'#':' ', draw_rd>line?'#':' ', draw_sh>line?'#':' ', 
  	vibrato>line?'#':' ', percussion>line?'#':' ' );
  }
  printf( "+--A---S---D---F-----G---H---J-----K-----L--+\n" );
  fflush( stdout );
}


void print_debug( void ) {
  printf( "  %d %d %d %d %d %d %d  %d %d %d  %d    \r", 
           vol_16, vol_8, vol_4, vol_223, vol_2, vol_135, vol_1,
           vol_fl, vol_rd, vol_sh, master_volume );
           fflush( stdout );
}



// bandlimited sawtooth and rectangle
// Gibbs smoothing according:
// Joe Wright: Synthesising bandlimited waveforms using wavetables
// www.musicdsp.org/files/bandlimited.pdf
//
sample_t saw_bl( float arg, int order, int partials ) {
  while ( arg >= 2 * PI )
    arg -= 2 * PI;
  sample_t result = 0.0;
  float k = PI / 2 / partials;
  for ( int n = order; n <= partials; n += order ) {
    float m = cosf( (n-1) * k );
    m = m * m;
    result += sinf( n * arg ) / n * m;
  }
  return result;
}



sample_t rect_bl( float arg, int order, int partials ) {
  while ( arg >= 2 * PI )
    arg -= 2 * PI;
  sample_t result = 0.0;
  float k = PI / 2 / partials;
  for ( int n = order; n <= partials; n += 2 * order ) {
    float m = cosf( (n-1) * k );
    m = m * m;
    result += sinf( n * arg ) / n * m;
  }
  return result;
}




int main( int argc, char *argv[] ) {

  char *name = "Connie";

// registering the handler, catching SIGINT signals
  signal( SIGINT, ctrl_c_handler );
  signal( SIGTERM, ctrl_c_handler );


  struct termios t;
  // get term status
  tcgetattr (1, &t);
  term_orig = t; // store status in global var
  // set keyboard to non canonical mode
  t.c_lflag &= ~(ICANON | ECHO);
  tcsetattr (1, 0, &t);


  // TODO: parse cmd line args

  /*
   ******************************************************
   * For more info about writing a JACK client look at: *
   *  http://dis-dot-dat.net/index.cgi?item=jacktuts/   *
   ******************************************************
  */
 
  /* tell the JACK server to call error_cb() whenever it
     experiences an error.  Notice that this callback is
     global to this process, not specific to each client.

     This is set here so that it can catch errors in the
     connection process
  */
  jack_set_error_function( error_cb );


  /* try to become a client of the JACK server */

  if ( (client = jack_client_open( name, 0, NULL ) ) == 0 ) {
    fprintf( stderr, "jack server not running?\n" );
    return 1;
  }


  /* tell the JACK server to call `rt_process_cb()' whenever
     there is work to be done.
  */
  jack_set_process_callback( client, rt_process_cb, 0 );


  /* tell the JACK server to call `srate_cb()' whenever
     the sample rate of the system changes.
  */
  jack_set_sample_rate_callback( client, srate_cb, 0 );


  /* tell the JACK server to call `jack_shutdown_cb()' if
     it ever shuts down, either entirely, or if it
     just decides to stop calling us.
  */
  jack_on_shutdown( client, jack_shutdown_cb, 0 );


  /* display the current sample rate. once the client is activated 
     (see below), you should rely on your own sample rate
     callback (see above) for this value.
  */
  samplerate = jack_get_sample_rate( client );
  printf( "sample rate: %lu/sec\n", (unsigned long)samplerate );


  // build list of et midi frequencies starting from lowest C (note 0)
  // (three halftones above the very low A six octaves down from a' 440 Hz)
  float f = concert_pitch_440 / 64 * halftone * halftone * halftone;

  for ( int midinote = 0; midinote < 128; midinote++ ) {
    midi_freq[ midinote ] = f;
    f *= halftone; 
    midi_vol_raw[ midinote ] = 0;
    vol_key[ midinote ] = 0;
    vol_note[ midinote ] = 0;
  }

  // set the starting phase of the 12 tones
  for ( int tone = 0; tone < 12; tone++ ) {
    sample_offset[ tone ] = 0.0;
  }

  // create 1 cycle of the wave
  // calculate the number of samples in one cycle of the wave
  samincy = samplerate / STEP;
  // calculate our scale multiplier
  sample_t scale = 2 * PI / samincy;


  // allocate the space needed to store one cycle
  // use own buffer for each octave (reed voice)
  for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
    cycle_rd[ octave ]   = (sample_t *) malloc( samincy * sizeof( sample_t ) );
    if ( cycle_rd[ octave ] == NULL ) {
      fprintf( stderr,"memory allocation failed\n" );
      exit( 1 );
    }
    cycle_sh[ octave ]   = (sample_t *) malloc( samincy * sizeof( sample_t ) );
    if ( cycle_sh[ octave ] == NULL ) {
      fprintf( stderr,"memory allocation failed\n" );
      exit( 1 );
    }
  }
  // one size fits all (flute)
  cycle_fl = (sample_t *) malloc( samincy * sizeof( sample_t ) );
  // exit if allocation failed
  if ( cycle_fl == NULL ) {
    fprintf( stderr,"memory allocation failed\n" );
    exit( 1 );
  }

  printf( "Preparing the voices" );
  // and fill it up with one period of sine wave
  // maybe a RC filtered square wave sounds more natural
  // the vox continental uses a 3-4-5-8 mixture
  for ( int i=0; i < samincy; i++ ) {
    cycle_fl[i] = sinf( i * scale ); // flute
  }

  // fill sample buffer with bandlimited wave for each octave
  for ( int oct = 0; oct < OCT_SAMP; oct++ ) {
    // max partial < samplerate/3 for highest note in this octave
    // TEST: sr / 3 to reduce aliasing effects
    int partials = samplerate / 3.0 / midi_freq[ LOWNOTE + 12 * oct + 12 ];
    printf( "." );
    fflush( stdout );
    for ( int i=0; i < samincy; i++ ) {
      cycle_rd[ oct][ i ] = rect_bl( i * scale, 1, partials ); // reed
      cycle_sh[ oct][ i ] = saw_bl( i * scale, 1, partials ); // sharp
    }
  }
  puts("");

  // create one midi and two audio ports
  midi_port = jack_port_register( client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  output_port_l = jack_port_register( client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  output_port_r = jack_port_register( client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);


  // tell the JACK server that we are ready to roll
  if (jack_activate( client ) ) {
    fprintf( stderr, "cannot activate client\n" );
    exit( 1 );
  }

  // exit cleanly
  atexit( connie_shutdown );

// autoconnect if cmd line option
  if ( argc > 1 && !strcmp( argv[1], "--autoconnect" ) ) {

    // autoconnect to system playback port
    const char **ports, **pp;
    /* connect the ports*/
    if ( ( ports = jack_get_ports( client, NULL, NULL, JackPortIsPhysical|JackPortIsInput ) ) == NULL) {
      fprintf( stderr, "cannot find any physical playback ports\n" );
      exit(1);
    }
    pp = ports; 
    while ( *pp ) {
      //puts( *ports );
      if ( !jack_connect( client, jack_port_name( output_port_l ), *pp++ )
       &&  !jack_connect( client, jack_port_name( output_port_r ), *pp++ ) )
         break;
    }
    free( ports );
  }


// simple "gui" control
// ********************
//

  set_program( 0 );
  print_help();
  print_status();


  int c;
  int running = 1;
  int debug = 0;

  while ( running ) {
    if ( kbhit() ) {
      c = getchar();
      switch( c ) {

        // SPACE -> panic
        case ' ':
          panic();
          break;

        // [ESC] -> QUIT
        case '\033':
          printf( "QUIT? [y/N] :" );
          c = getchar();
          putchar( c );
          if ( 'y' == c || 'Y' == c )
            running = 0;
          break;

        // set preset 0..6
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          set_program( c - '0' );
          break;

        // stop drawbars
        // 16'
        case 'q':
        case 'Q':
          if ( draw_16 > 0 ) {
            draw_16--;
          }
          break;
        case 'a':
        case 'A':
          if ( draw_16 < 8 ) {
            draw_16++;
          }
          break;

        // 8'
        case 'w':
        case 'W':
          if ( draw_8 > 0 ) {
            draw_8--;
          }
          break;
        case 's':
        case 'S':
          if ( draw_8 < 8 ) {
            draw_8++;
          }
          break;

        // 4'
        case 'e':
        case 'E':
          if ( draw_4 > 0 ) {
            draw_4--;
          }
          break;
        case 'd':
        case 'D':
          if ( draw_4 < 8 ) {
            draw_4++;
          }
          break;

        // mixture
        case 'r':
        case 'R':
          if ( draw_IV > 0 ) {
            draw_IV--;
          }
          break;
        case 'f':
        case 'F':
          if ( draw_IV < 8 ) {
            draw_IV++;
          }
          break;

        // voice drawbars
        // flute
        case 't':
        case 'T':
          if ( draw_fl > 0 ) {
            draw_fl--;
          }
          break;
        case 'g':
        case 'G':
          if ( draw_fl < 8 ) {
            draw_fl++;
          }
          break;

        // reed
        case 'y':
        case 'Y':
        case 'z':
        case 'Z':
          if ( draw_rd > 0 ) {
            draw_rd--;
          }
          break;
        case 'h':
        case 'H':
          if ( draw_rd < 8 ) {
            draw_rd++;
          }
          break;

        // sharp
        case 'u':
        case 'U':
          if ( draw_sh > 0 ) {
            draw_sh--;
          }
          break;
        case 'j':
        case 'J':
          if ( draw_sh < 8 ) {
            draw_sh++;
          }
          break;

        // vibrato
        case 'i':
        case 'I':
          if ( vibrato > 0 ) {
            vibrato--;
          }
          break;
        case 'k':
        case 'K':
          if ( vibrato < 8 ) {
            vibrato++;
          }
          break;

        // percussion
        case 'o':
        case 'O':
          if ( percussion > 0 ) {
            percussion--;
          }
          break;
        case 'l':
        case 'L':
          if ( percussion < 8 ) {
            percussion++;
          }
          break;
        case '?':
          debug = 1;
          break;

        // catch undefined cmds
        default:
          // printf( "c = %d\n", c );
          break;
      } // switch( c )


      if ( running ) {
        set_volumes();
        print_help();
        print_status();
        if ( debug ) {
          print_debug();
          debug = 0;
        }
      }
    } else if ( value_changed ) {
      set_volumes();
      print_help();
      print_status();
      value_changed = 0;
    } else {
      usleep( 10000 );
    }
  } //while running

  // connie_shutdown() called via atexit()

  exit( 0 );
}

