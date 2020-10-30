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



const char * connie_version = "0.3.2";
const char * connie_name = "in the cage";



//////////////////////////////////////////////
//            <USER TUNABLE PART>           //
//////////////////////////////////////////////
//
// tune the instrument
const float concert_pitch_440 = 440.0;

// "size of the instrument"
#define OCTAVES 6
#define LOWNOTE 24
#define HIGHNOTE (LOWNOTE+12*OCTAVES)

// max "leslie" rotation freq
#define VIBRATO 8
//
#define PROGRAMS 7
// the drawbar volumes (vol_xx = 0..8)
// some program presets
int preset[PROGRAMS][7] = { 
    { 2, 8, 6, 8, 8, 4, 0 }, // preset 0
    { 0, 8, 6, 8, 4, 8, 0 }, // preset 1
    { 0, 8, 8, 8, 0, 8, 0 }, // preset 2
    { 4, 8, 4, 6, 8, 4, 1 }, // preset 3
    { 4, 8, 6, 4, 8, 0, 2 }, // preset 4
    { 8, 7, 6, 5, 8, 4, 4 }, // preset 5
    { 8, 8, 8, 8, 8, 8, 0 }, // preset 6
};
//
//////////////////////////////////////////////
//           </USER TUNABLE PART>           //
//////////////////////////////////////////////



// solution of sample buffers
#define STEP 10


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
sample_t *cycle = NULL;
sample_t *mixture = NULL;
sample_t *cycle_rd[ OCTAVES ];
sample_t *mixture_rd[ OCTAVES ];

// samples in cycle
jack_nframes_t samincy;


// table with frequency of each midi note
float midi_freq[128];

// sample offset of each note, advanced by rt_process
float sample_offset[128];

// actual volume of each note
int midi_vol_raw[128]; // from key press/release
int midi_vol[128]; // actual sounding

// actual value of each midi control
int midi_cc[128];

// the actual midi prog
int midi_prog = 0;

int value_changed = 0;


// stops
int vol_16    = 2;
int vol_8     = 8;
int vol_4     = 6;
int vol_mix   = 8;
// voices
int vol_flute = 8;
int vol_reed  = 4;
// vibrato 
int vibrato   = 0;

// the master midi volume (cc07) (volume = 0..127)
int volume = 64;

// avoid nasty warning
extern int usleep( int );



void set_program( int prog ) {
  if ( prog >= 0 && prog < PROGRAMS ) {
    // stops
    vol_16    = preset[prog][0];
    vol_8     = preset[prog][1];
    vol_4     = preset[prog][2];
    vol_mix   = preset[prog][3];
    // voices
    vol_flute = preset[prog][4];
    vol_reed  = preset[prog][5];
    // vibrato 
    vibrato   = preset[prog][6];
  }
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



// ********************
// our realtime process
// ********************
//
int rt_process_cb( jack_nframes_t nframes, void *void_arg ) {

  // freq modulation for vibrato
  static float shift_offset = 0.0f;

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
        midi_vol_raw[note]= event.buffer[2];
      } else if ( ( event.buffer[0] >> 4 ) == 0x0B ) {// cc num val
          int cc = event.buffer[1];
          midi_cc[cc] = event.buffer[2];
          if ( cc == 7 )
            volume = event.buffer[2];
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
  float value_flute, value_reed;
  float shift;

  static int timer = 0;

  // grab our audio output buffer 
  sample_t *out_l = (sample_t *) jack_port_get_buffer (output_port_l, nframes);
  sample_t *out_r = (sample_t *) jack_port_get_buffer (output_port_r, nframes);

  // fill the buffer
  // this implements the signal flow of an electronic organ
  for ( jack_nframes_t frame = 0; frame < nframes; frame++ ) {
    value_flute = value_reed = 0.0f;

    // shifting the pitch and volume for (simple) leslie sim
    // shift is a sin signal used for fm and am
    // vibrato 0..8 -> freq 0..VIBRATO Hz
    shift_offset += vibrato * VIBRATO / 8.0 / STEP; // shift frequency
    if ( shift_offset >= samincy )
      shift_offset -= samincy;
    if ( vibrato )
      shift = cycle[ pos = shift_offset ];
    else
      shift = 0.0f;

    // polyphonic output with drawbars vol_xx
    //
    int note = LOWNOTE;
    for ( int octave = 0, harmonic = 1; octave < OCTAVES; octave++, harmonic *= 2 ) { // 6 octaves 1,2,4,8,16,32
      for ( int tone = 0; tone < 12; tone++, note++ ) {
        int vol = midi_vol[note];
        if ( vol ) { // note actually playing
          // use square law for drawbar volumes (vol_x * vol_x / 64.0)

          // 16' stop (subharmonic)
          if ( vol_16 && octave > 0) { // no subharmonics for lowest octave
            pos = sample_offset[ LOWNOTE + tone ] * harmonic / 2;
            while ( pos >= samincy )
              pos -= samincy;
            value_flute += vol * vol_16 * vol_16 * cycle[ pos ];
            // at octave border B->C a new sample buffer will be used
            // this leads to ugly different sound - solution:
            // average at octave border between samples for both octaves, linear transition
            // weight: Ab:7*act+1*next, A:6a+2n, Bb:5a+3n, B:4a+4n, C:4prev+4act, C#:5a+3p, D:6a+2p, D#:7a+1p
            if ( octave > 1 && tone < 4 ) { // C, C#, D, D#
              value_reed  += vol * vol_16 * vol_16 
                             * ( (4-tone) * cycle_rd[ octave-2 ][ pos ] // previous samples
                               + (4+tone) * cycle_rd[ octave-1 ][ pos ] ) / 8 ; // actual samples
            } else if ( tone > 7 ) {  // Ab, A, Bb, B
              value_reed  += vol * vol_16 * vol_16 
                             * ( ( 15-tone ) * cycle_rd[ octave-1 ][ pos ]  // prev.
                               + ( tone-7 ) * cycle_rd[ octave ][ pos ] ) / 8; // act.
            } else { // E...A
              value_reed  += vol * vol_16 * vol_16 * cycle_rd[ octave-1 ][ pos ]; // act. samples
            }
          }

          // 8' stop (unison stop)
          if ( vol_8 ) {
            pos = sample_offset[ LOWNOTE + tone ] * harmonic;
            while ( pos >= samincy )
              pos -= samincy;
            value_flute += vol * vol_8 * vol_8 * cycle[ pos ];
            // same average as above
            if ( octave > 0  && tone < 4 ) {
              value_reed  += vol * vol_8 * vol_8 
                             * ( (4-tone) * cycle_rd[ octave-1 ][ pos ]
                               + (4+tone) * cycle_rd[ octave ][ pos ] ) / 8 ;
            } else if ( octave < OCTAVES-1  && tone > 7 ) {
              value_reed  += vol * vol_8 * vol_8 
                             * ( (15-tone) * cycle_rd[ octave ][ pos ]
                               + (tone-7) * cycle_rd[ octave+1 ][ pos ] ) / 8 ;
            } else {
              value_reed  += vol * vol_8 * vol_8 * cycle_rd[ octave ][ pos ];
            }
          }

          // 4' stop (octave)
          if ( vol_4 ) {
            pos = sample_offset[ LOWNOTE + tone ] * harmonic * 2;
            while ( pos >= samincy )
              pos -= samincy;
            value_flute += vol * vol_4 * vol_4 * cycle[ pos ];
            // average as above
            if ( octave < OCTAVES-1 ) {
              if ( octave > 0  && tone < 4 ) {
                value_reed  += vol * vol_4 * vol_4 
                               * ( (4-tone) * cycle_rd[ octave ][ pos ]
                                 + (4+tone) * cycle_rd[ octave+1 ][ pos ] ) / 8 ;
              } else if ( octave < OCTAVES-2  && tone > 7 ) {
                value_reed  += vol * vol_4 * vol_4 
                               * ( (15-tone) * cycle_rd[ octave+1 ][ pos ]
                                 + (tone-7) * cycle_rd[ octave+2 ][ pos ] ) / 8 ;
              } else {
                value_reed  += vol * vol_4 * vol_4 * cycle_rd[ octave+1 ][ pos ];
              }
            } else { // octave foldback
              value_reed  += vol * vol_4 * vol_4 * cycle_rd[ octave ][ pos ];
            } // if ( octave < OCTAVES-1 )
          } // if ( vol_4 )

          // mixture stop
          if ( vol_mix ) {
            pos = sample_offset[ LOWNOTE + tone ] * harmonic;
            while ( pos >= samincy )
              pos -= samincy;
            value_flute += vol * vol_mix * vol_mix * mixture[ pos ];
            if ( octave > 0  && tone < 4 ) {
              value_reed  += vol * vol_mix * vol_mix 
                             * ( (4-tone) * mixture_rd[ octave-1 ][ pos ]
                               + (4+tone) * mixture_rd[ octave ][ pos ] ) / 8 ;
            } else if ( octave < OCTAVES-1  && tone > 7 ) {
              value_reed  += vol * vol_mix * vol_mix
                             * ( (15-tone) * mixture_rd[ octave ][ pos ]
                               + (tone-7) * mixture_rd[ octave+1 ][ pos ] ) / 8 ;
            } else {
              value_reed  += vol * vol_mix * vol_mix * mixture_rd[ octave ][ pos ];
            }
          } // if ( vol_mix )

        } // if ( vol )
      } // for ( tone )
    } // for ( octave )

    for ( note = LOWNOTE; note < LOWNOTE+12; note++ ) {
      // advance individual sample pointer, do fm for vibrato
      // vibrato 0..8 -> 0..8 Hz rot. speed
      // typical leslie horn length 0.2 m
      // at rotation speed 1/s the transl. speed of horn mouth ist v=1.2m/s
      // the doppler formula: f' = f * 1 / ( 1 - v/c )
      // at 1 Hz -> f' = 1 +- 0.003 ( 5 cent shift per Hz )
      sample_offset[note] += ( 1.0f + 0.003f * shift * vibrato * VIBRATO / 8.0 ) 
                             * midi_freq[note] / STEP;
      if ( sample_offset[note] >= samincy ) { // zero crossing
        sample_offset[note] -= samincy;
      }
    } // for ( note )

    // normalize the output (square law for master and drawbar volumes)
    // vol_16, vol_8, vol_4, vol_mix, vol_flute and vol_reed: range 0..8
    // midi_vol, volume: range 0..127
    sample_t out;
    // allow summing of multiple keys, stops, voices without hard limiting
    out = 0.1 * volume * volume 
          * ( vol_flute * vol_flute * value_flute 
            + vol_reed * vol_reed * value_reed ) 
          / ( 127.0 * 127 *127 * 8 * 8 * 8 * 8 );

    // soft clipping, leave 20% headroom for leslie am
    out = 1.25 * clip( out );

    // TODO: adjust value of leslie am index
    out_l[frame] = out * (1.0f - shift / 10); // 10% (?) am for "leslie"
    out_r[frame] = out * (1.0f + shift / 10); // "

    // ramp the midi volumes up/down to remove the clicking at key press/release
    if ( ++timer > samplerate / 8000 ) { // every 125µs
      timer = 0;
      int *p_vol = midi_vol + LOWNOTE; // midi_vol[note]
      int *p_raw = midi_vol_raw + LOWNOTE;
      for ( note = LOWNOTE; note < HIGHNOTE; note++, p_vol++, p_raw++ ) {
        if ( *p_vol < *p_raw )
          (*p_vol)+=2;
        if ( *p_vol > *p_raw )
          (*p_vol)--;
      } // for ( note )
    } // if /( timer )

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
  if ( cycle )
    free( cycle );
  if ( mixture )
    free( mixture );
  cycle = mixture = NULL;
  for ( int oct = 0; oct < OCTAVES; oct++ ) {
    if ( cycle_rd[ oct ] )
      free( cycle_rd[ oct ] );
    if ( mixture_rd[ oct ] )
      free( mixture_rd[ oct ] );
    cycle_rd[ oct ] = mixture_rd[ oct ] = NULL;
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
  puts( "Q/A  W/S  E/D  R/T\t16'  8'  4'  IV  stops");
  puts( "U/J  I/K\t\tflute  reed  voices");
  puts( "O/L\t\t\tvibrato");
  puts( "1  2  3  4  5  6\tpresets\n\n");
}



void print_status( void ) {
  printf( "+---------------------------------+\n" );
  printf( "| 16'  8'  4'  IV   fl  rd    vib |\n" );
  printf( "+--Q---W---E---R-----U---I-----O--+\n" );
  for ( int line = 0; line < 8; line++ ) {
    printf( "|  %c   %c   %c   %c     %c   %c     %c  |\n", 
  	vol_16>line?'#':' ', vol_8>line?'#':' ', 
  	vol_4>line?'#':' ', vol_mix>line?'#':' ', 
  	vol_flute>line?'#':' ', vol_reed>line?'#':' ', 
  	vibrato>line?'#':' ' );
  }
  printf( "+--A---S---D---F-----J---K-----L--+\n\n" );
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
  if (argc >= 2) {
    name = argv[1];
  }

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


  // build list of midi frequencies starting from lowest C (note 0)
  // (three halftones above the very low A six octaves down from a' 440 Hz)
  float f = concert_pitch_440 / 64 * halftone * halftone * halftone;

  for ( int midinote = 0; midinote < 128; midinote++ ) {
    midi_freq[ midinote ] = f;
    f *= halftone; 
    midi_vol_raw[ midinote ] = 0;
    midi_vol[ midinote ] = 0;
    sample_offset[ midinote ] = 0.0;
  }


  // create 1 cycle of the wave
  // calculate the number of samples in one cycle of the wave
  samincy = samplerate / STEP;
  // calculate our scale multiplier
  sample_t scale = 2 * PI / samincy;


  // allocate the space needed to store one cycle
  // use own buffer for each octave (reed voice)
  for ( int octave = 0; octave < OCTAVES; octave++ ) {
    cycle_rd[ octave ]   = (sample_t *) malloc( samincy * sizeof( sample_t ) );
    mixture_rd[ octave ] = (sample_t *) malloc( samincy * sizeof( sample_t ) );
    if ( cycle_rd[ octave ] == NULL || mixture_rd[ octave ] == NULL) {
      fprintf( stderr,"memory allocation failed\n" );
      exit( 1 );
    }
  }
  // one size fits all (flute)
  cycle = (sample_t *) malloc( samincy * sizeof( sample_t ) );
  mixture = (sample_t *) malloc( samincy * sizeof( sample_t ) );
  // exit if allocation failed
  if ( cycle == NULL || mixture == NULL ) {
    fprintf( stderr,"memory allocation failed\n" );
    exit( 1 );
  }

  printf( "Preparing the voices" );
  // and fill it up with one period of sine wave
  // maybe a RC filtered square wave sounds more natural
  // the vox continental uses a 3-4-5-8 mixture
  for ( int i=0; i < samincy; i++ ) {
    cycle[i] = sinf( i * scale ); // flute
    mixture[i] = sinf( 3 * i * scale ) 
               + sinf( 4 * i * scale )
               + sinf( 5 * i * scale )
               + sinf( 8 * i * scale );
  }

  // fill sample buffer with bandlimited wave for each octave
  for ( int oct = 0; oct < OCTAVES; oct++ ) {
    // max partial < samplerate/2 for highest note in this octave
    // TEST: sr / 3
    int partials = samplerate / 3.0 / midi_freq[ LOWNOTE + 12 * oct + 12 ];
    printf( "." );
    fflush( stdout );
    for ( int i=0; i < samincy; i++ ) {
      cycle_rd[ oct][ i ]     = saw_bl( i * scale, 1, partials ); // reed
      mixture_rd[ oct ][ i ]  = saw_bl( i * scale, 3, partials )
                              + saw_bl( i * scale, 4, partials )
                              + saw_bl( i * scale, 5, partials )
                              + saw_bl( i * scale, 8, partials );
    }
  }


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

#ifdef AUTOCONNECT
// DON'T autoconnect - maybe as an cmd line option
// ===============================================
  // autoconnect to system playback port
  const char **ports;
  /* connect the ports*/
  if ( ( ports = jack_get_ports( client, NULL, NULL, JackPortIsPhysical|JackPortIsInput ) ) == NULL) {
    fprintf( stderr, "cannot find any physical playback ports\n" );
    exit(1);
  }

  int i=0;
  while ( ports[i]!=NULL ) {
    if ( jack_connect( client, jack_port_name( output_port ), ports[i] ) ) {
      fprintf( stderr, "cannot connect output port %s\n", ports[i] );
    }
    i++;
  }

  free( ports );
#endif


// simple "gui" control
// ********************
//

  set_program( 0 );
  print_help();
  print_status();


  int c;
  int running = 1;

  while ( running ) {
    if ( kbhit() ) {
      c = getchar();
      switch( c ) {

        // SPACE -> panic
        case ' ':
          for ( int iii = 0; iii < 128; iii++ )
            midi_vol[iii] = midi_vol_raw[iii] = 0;
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
          set_program( c - '0' );
          break;

        // stop drawbars
        // 16'
        case 'q':
        case 'Q':
          if ( vol_16 > 0 )
            vol_16--;
          break;
        case 'a':
        case 'A':
          if ( vol_16 < 8 )
            vol_16++;
          break;

        // 8'
        case 'w':
        case 'W':
          if ( vol_8 > 0 )
            vol_8--;
          break;
        case 's':
        case 'S':
          if ( vol_8 < 8 )
            vol_8++;
          break;

        // 4'
        case 'e':
        case 'E':
          if ( vol_4 > 0 )
            vol_4--;
          break;
        case 'd':
        case 'D':
          if ( vol_4 < 8 )
            vol_4++;
          break;

        // mixture
        case 'r':
        case 'R':
          if ( vol_mix > 0 )
            vol_mix--;
          break;
        case 'f':
        case 'F':
          if ( vol_mix < 8 )
            vol_mix++;
          break;

        // voice drawbars
        // flute
        case 'u':
        case 'U':
          if ( vol_flute > 0 )
            vol_flute--;
          break;
        case 'j':
        case 'J':
          if ( vol_flute < 8 )
            vol_flute++;
          break;

        // reed
        case 'i':
        case 'I':
          if ( vol_reed > 0 )
            vol_reed--;
          break;
        case 'k':
        case 'K':
          if ( vol_reed < 8 )
            vol_reed++;
          break;

        // vibrato
        case 'o':
        case 'O':
          if ( vibrato > 0 )
            vibrato--;
          break;
        case 'l':
        case 'L':
          if ( vibrato < 8 )
            vibrato++;
          break;

        // catch undefined cmds
        default:
          // printf( "c = %d\n", c );
          break;
      } // switch( c )


      if ( running ) {
        print_help();
        print_status();
      }
    } else if ( value_changed ) {
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

