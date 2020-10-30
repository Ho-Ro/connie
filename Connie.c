/*****************************************************************************
 *
 *   Connie.c
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <signal.h>


#include <jack/jack.h>
#include <jack/midiport.h>


const char * connie_version = "0.3.1";
const char * connie_name = "beautiful noise";

// tune the instrument
const double concert_pitch_440 = 440.0;

// one halftone step 
const double halftone = 1.059463094;

const double PI = 3.14159265;

const int LOWNOTE=24;
const int HIGHNOTE=96;

/* Our jack client and the ports */
jack_client_t *client = NULL;
jack_port_t *midi_port;
jack_port_t *output_port_l;
jack_port_t *output_port_r;


struct termios term_orig;


typedef jack_default_audio_sample_t sample_t;

/*The current sample rate*/
jack_nframes_t sr;

/*one cycle of our sound*/
sample_t *cycle;
sample_t  *mixture;
sample_t *cycle_rd;
sample_t  *mixture_rd;

/*samples in cycle*/
jack_nframes_t samincy;


// table with frequency of midi note
double midi_freq[128];

// sample offset of each note
double sample_offset[128];

// actual volume of each note
int midi_vol_raw[128];
int midi_vol[128];

// actual value of each control
int midi_cc[128];

// the actual prog
int midi_prog = 0;

// the drawbar volumes (vol_xx = 0..8)
// stops
int vol_16 = 6;
int vol_8 = 8;
int vol_4 = 6;
int vol_mix = 4;
// voices
int vol_flute = 8;
int vol_reed = 4;
// vibrato 
int vibrato = 0;

// master volume (volume = 0..127)
int volume = 64;



// ********************
// our realtime process
// ********************
//
int rt_process_cb( jack_nframes_t nframes, void *void_arg ) {

  // freq modulation for vibrato
  static int shift_offset = 0;

  // process midi events
  // *******************
  // (inspired from fluidjack.c from Nedko Arnaudov)
  //
  jack_nframes_t event_count;
  jack_midi_event_t event;

  // grab our midi input buffer
  void * midi_buffer = jack_port_get_buffer( midi_port, nframes );
  // check for events and process them
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
      }
    }
  } // for


  // create audio output
  // *******************
  //
  // sampling position
  int pos;
  // voice samples 
  double value_flute, value_reed;
  double shift;

  static int timer = 0;

  // grab our audio output buffer 
  sample_t *out_l = (sample_t *) jack_port_get_buffer (output_port_l, nframes);
  sample_t *out_r = (sample_t *) jack_port_get_buffer (output_port_r, nframes);

  // fill the buffer
  // this implements the signal flow of an electronic organ
  for ( jack_nframes_t i=0; i<nframes; i++ ) {
    value_flute = value_reed = 0.0;

    // shifting the pitch and volume for (simple) leslie sim
    // shift is a sin signal used for fm and am
    // vibrato 0..8 -> freq 0..8 Hz
    shift_offset += vibrato; // shift frequency (int)
    if ( shift_offset >= sr )
      shift_offset -= sr;
    if ( vibrato )
      shift = cycle[ shift_offset ];
    else
      shift = 0.0;

    // polyphonic output with drawbars vol_xx
    // advance the individual sample pointers
    //
    int note = LOWNOTE;
    for ( int octave = 1; octave <=32; octave *= 2 ) { // 6 octaves 1,2,4,8,16,32
      for ( int tone = LOWNOTE; tone < LOWNOTE+12; tone++, note++ ) {
        int vol = midi_vol[note];
        if ( vol ) { // note actually playing
          // use square law for drawbar volume
          // 16' stop (subharmonic)
          if ( vol_16 && note >= LOWNOTE+12) {
            pos = sample_offset[ tone ] * octave / 2;
            while ( pos >= sr )
              pos -= sr;
            value_flute += vol * vol_16 * vol_16 * cycle[ pos ];
            value_reed  += vol * vol_16 * vol_16 * cycle_rd[ pos ];
          }

          // 8' stop (unison stop)
          if ( vol_8 ) {
            pos = sample_offset[ tone ] * octave;
            while ( pos >= sr )
              pos -= sr;
            value_flute +=  vol * vol_8 * vol_8 * cycle[ pos ];
            value_reed  += vol * vol_8 * vol_8 * cycle_rd[ pos ];
          }

          // 4' stop (octave)
          if ( vol_4 ) {
            pos = sample_offset[ tone ] * octave * 2;
            while ( pos >= sr )
              pos -= sr;
            value_flute += vol * vol_4 * vol_4 * cycle[ pos ];
            value_reed  += vol * vol_4 * vol_4 * cycle_rd[ pos ];
          }

          // mixture stop
          if ( vol_mix ) {
            pos = sample_offset[ tone ] * octave;
            while ( pos >= sr )
              pos -= sr;
            value_flute += vol * vol_mix * vol_mix * mixture[ pos ];
            value_reed  += vol * vol_mix * vol_mix * mixture_rd[ pos ];
          }
        } // if ( vol )
      } // for ( tone )
    } // for ( octave )
    for ( note =LOWNOTE; note < LOWNOTE+12; note++ ) {
      // advance individual sample pointer, do fm for vibrato
      // vibrato 0..8 -> 0..8 Hz rot. speed
      // typical leslie horn length 0.25 m 
      // at rotation speed 1/s the transl. speed of horn mouth ist v=1.5m/s
      // the doppler formula: f' = f * 1 / ( 1 - v/c )
      // at 1 Hz -> f' = 1 +- 0.005 ( 8 cent shift per Hz )
      sample_offset[note] += ( 1.0 + 0.005 * shift * vibrato  ) * midi_freq[note];
      if ( sample_offset[note] >= sr ) { // zero crossing
        sample_offset[note] -= sr;
      }
    } // for ( note )

    // normalize the output (square law for drawbar volumes)
    // vol_16, vol_8, vol_4, vol_mix, vol_flute and vol_reed: range 0..8
    // midi_vol, volume: range 0..127
    sample_t out;
    out = 0.1 * volume * ( vol_flute * vol_flute * value_flute 
                            + vol_reed * vol_reed * value_reed ) 
           / ( 127 * 127 * 8 * 8 * 8 * 8 );
    out_l[i] = out * (1.0 - shift / 4); // am for "leslie"
    out_r[i] = out * (1.0 + shift / 4); // "

    // ramp the midi volumes up/down to remove the clicking at key press/release
    if ( ++timer > sr / 8000 ) { // every 125µs
      timer = 0;
      int *p_vol = midi_vol + LOWNOTE;
      int *p_raw = midi_vol_raw + LOWNOTE;
      for ( note = LOWNOTE; note < HIGHNOTE; note++, p_vol++, p_raw++ ) {
        if ( *p_vol < *p_raw )
          (*p_vol)++;
        if ( *p_vol > *p_raw )
          (*p_vol)--;
      } // for ( note )
    } // if /( timer )
  } // for ( nframes )
  return 0;
} // rt_process_cb()



// callback if sample rate changes
int srate_cb( jack_nframes_t nframes, void *arg ) {
  printf( "the sample rate is now %lu/sec\n", (unsigned long)nframes );
  sr = nframes;
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



void connie_shutdown( void )
{
  // restore term settings
  tcsetattr( 1, 0, &term_orig );

  if ( client ) {
    //puts( "client_close()" );
    jack_client_close( client );
    client = NULL;
  }
  printf("\n");
}



// The signal handler function
void ctrl_c_handler( int sig) {
  //signal( SIGINT, ctrl_c_handler );
  printf( "Abort...\n");
  exit( 0 );
}  // handler




void print_help( void ) {
  puts( "\n\n\n\n" );
  puts( "[ESC]\tQuit\t\t[SPACE]\tPanic" );
  puts( "A Q\t16' stop\tpull / shift");
  puts( "S W\t 8' stop\tpull / shift");
  puts( "D E\t 4' stop\tpull / shift");
  puts( "F R\tmix stop\tpull / shift");
  puts( "J U\tflute voice\tpull / shift");
  puts( "K I\treed voice\tpull / shift");
  puts( "L O\tvibrato\t\tpull / shift\n");
}



void print_status( void ) {
  printf( "Connie %s (%s)\n", connie_version, connie_name );
  printf( "+---------------------------------+\n" );
  printf( "| 16'  8'  4' mix   fl  rd    vib |\n" );
  printf( "+--Q---W---E---R-----U---I-----O--+\n" );
  for ( int line = 0; line < 8; line++ ) {
    printf( "|  %c   %c   %c   %c     %c   %c     %c  |\n", 
  	vol_16>line?'#':' ', vol_8>line?'#':' ', 
  	vol_4>line?'#':' ', vol_mix>line?'#':' ', 
  	vol_flute>line?'#':' ', vol_reed>line?'#':' ', 
  	vibrato>line?'#':' ' );
  }
  printf( "+--A---S---D---F-----J---K-----L--+\n" );
}

// sawtooth 
// arg: 0..2 PI
double saw( double arg ) {
  while ( arg >= 2 * PI )
    arg -= 2 * PI;
    if ( arg < PI )
      return arg / PI;
    else
      return arg / PI - 2.0;
}


double saw_( double arg ) {
  while ( arg >= 2 * PI )
    arg -= 2 * PI;
    return cos( arg / 2 );
}



int main( int argc, char *argv[] ) {

  char *name = "Connie";



// Registering the handler, catching SIGINT signals
  signal( SIGINT, ctrl_c_handler );


  struct termios t;
  // get term status
  tcgetattr (1, &t);
  term_orig = t; // store status in global var
  // set keyboard to non canonical mode
  t.c_lflag &= ~(ICANON | ECHO);
  tcsetattr (1, 0, &t);


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

  if ( (client = jack_client_new( name ) ) == 0 ) {
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
  printf( "sample rate: %lu\n", (unsigned long)jack_get_sample_rate( client ) );

  sr = jack_get_sample_rate( client );

  /* create one midi and two  audio ports */
  midi_port = jack_port_register( client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  output_port_l = jack_port_register( client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  output_port_r = jack_port_register( client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

 /*Create 1 cycle of the wave*/
  /*Calculate the number of samples in one cycle of the wave*/
  samincy = sr;
  /*Calculate our scale multiplier*/
  sample_t scale = 2 * PI / samincy;
  /*Allocate the space needed to store one cycle*/
  cycle = (sample_t *) malloc( samincy * sizeof( sample_t ) );
  mixture = (sample_t *) malloc( samincy * sizeof( sample_t ) );
  cycle_rd = (sample_t *) malloc( samincy * sizeof( sample_t ) );
  mixture_rd = (sample_t *) malloc( samincy * sizeof( sample_t ) );
 /*Exit if allocation failed (more sense from Jussi)*/
  if ( cycle == NULL || mixture == NULL || cycle_rd == NULL || mixture_rd == NULL ) {
    fprintf( stderr,"memory allocation failed\n" );
    return 1;
  }

  // And fill it up with one period of sine wave
  // maybe a RC filtered square wave sounds more natural
  for ( int i=0; i < samincy; i++ ) {
    cycle[i] = sin( i * scale ); // sin
    mixture[i] = sin( 3 * i * scale ) 
               + sin( 4 * i * scale )
               + sin( 5 * i * scale )
               + sin( 6 * i * scale );
    cycle_rd[i] = saw( i * scale ); // saw
    mixture_rd[i] = saw( 3 * i * scale ) 
               + saw( 4 * i * scale )
               + saw( 5 * i * scale )
               + saw( 6 * i * scale );
  }

  // build list of midi frequencies starting from lowest C (note 0)
  // (three halftones above the very low A six octaves down from a' 440 Hz)
  double f = concert_pitch_440 / 64 * halftone * halftone * halftone;
//  for ( int midinote=24; midinote<36; midinote++ ) {
//    sample_offset[midinote] = 0;
//  }
  for ( int midinote=0; midinote<128; midinote++ ) {
    midi_freq[midinote] = f;
    f *= halftone; 
    midi_vol_raw[midinote] = 0;
    midi_vol[midinote] = 0;
    sample_offset[midinote] = 0.0;
  }


  /* tell the JACK server that we are ready to roll */
  if (jack_activate( client ) ) {
    fprintf( stderr, "cannot activate client\n" );
    return 1;
  }

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
  print_help();
  print_status();

  int c;
  int running = 1;
  enum cmd_t { NONE=0, 
               V16_INC, V16_DEC, V8_INC, V8_DEC, V4_INC, V4_DEC,
               VM_INC, VM_DEC, VF_INC, VF_DEC, VR_INC, VR_DEC,
               VIB_INC, VIB_DEC,
               PANIC, QUIT } cmd = NONE;

  while ( running ) {
    c = getchar();
    switch( c ) {
      case ' ': // panic
        cmd = PANIC;
        break;
      // [ESC] -> QUIT
      case '\033':
        cmd = QUIT;
        break;

      // reset to default
      case '0':
        cmd = NONE;
        vol_16 = vol_8 = vol_4 = vol_mix = vol_flute = 8;
        vol_reed = 0;
        vibrato = 0;
        break;

      // stop drawbars
      // 16'
      case 'q':
      case 'Q':
        cmd = V16_DEC;
        break;
      case 'a':
      case 'A':
        cmd = V16_INC;
        break;
      // 8'
      case 'w':
      case 'W':
        cmd = V8_DEC;
        break;
      case 's':
      case 'S':
        cmd = V8_INC;
        break;
      // 4'
      case 'e':
      case 'E':
        cmd = V4_DEC;
        break;
      case 'd':
      case 'D':
        cmd = V4_INC;
        break;
      // mixtur
      case 'r':
      case 'R':
        cmd = VM_DEC;
        break;
      case 'f':
      case 'F':
        cmd = VM_INC;
        break;

      // voice drawbars
      // flute
      case 'u':
      case 'U':
        cmd = VF_DEC;
        break;
      case 'j':
      case 'J':
        cmd = VF_INC;
        break;
      // reed
      case 'i':
      case 'I':
        cmd = VR_DEC;
        break;
      case 'k':
      case 'K':
        cmd = VR_INC;
        break;

      // vibrato
      case 'o':
      case 'O':
        cmd = VIB_DEC;
        break;
      case 'l':
      case 'L':
        cmd = VIB_INC;
        break;

        break;

      // catch undefined cmds
      default:
        // printf( "c = %d\n", c );
        cmd = NONE;
        break;
    } // switch( c )

    // execute command
    switch( cmd ) {
      case QUIT:
        printf( "QUIT? [y/N] :" );
        c = getchar();
        if ( 'y' == c || 'Y' == c )
          running = 0;
      case NONE:
        break;
      case PANIC:
        for ( int iii = 0; iii < 128; iii++ )
          midi_vol[iii] = midi_vol_raw[iii] = 0;
        break;
      case V16_INC: 
        if ( vol_16 < 8 )
          vol_16++;
        break;
      case V16_DEC: 
        if ( vol_16 > 0 )
          vol_16--;
        break;
      case V8_INC: 
        if ( vol_8 < 8 )
          vol_8++;
        break;
      case V8_DEC: 
        if ( vol_8 > 0 )
          vol_8--;
        break;
      case V4_INC: 
        if ( vol_4 < 8 )
          vol_4++;
        break;
      case V4_DEC: 
        if ( vol_4 > 0 )
          vol_4--;
        break;
      case VM_INC: 
        if ( vol_mix < 8 )
          vol_mix++;
        break;
      case VM_DEC: 
        if ( vol_mix > 0 )
          vol_mix--;
        break;
      case VF_INC: 
        if ( vol_flute < 8 )
          vol_flute++;
        break;
      case VF_DEC: 
        if ( vol_flute > 0 )
          vol_flute--;
        break;
      case VR_INC: 
        if ( vol_reed < 8 )
          vol_reed++;
        break;
      case VR_DEC: 
        if ( vol_reed > 0 )
          vol_reed--;
        break;
      case VIB_INC: 
        if ( vibrato < 8 )
          vibrato++;
        break;
      case VIB_DEC: 
        if ( vibrato > 0 )
          vibrato--;
        break;
    } // switch( cmd )
    if ( running ) {
      print_help();
      print_status();
    }
  } //while running

  connie_shutdown();
  free( cycle );

  exit( 0 );
}

