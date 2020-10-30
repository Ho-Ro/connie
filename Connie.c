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

#include <jack/jack.h>
#include <jack/midiport.h>


const char * connie_version = "0.1";
const char * connie_name = "light my fire";

// tune the instrument
const double concert_pitch_440 = 440.0;

// one halftone step 
const double halftone = 1.059463094;

const double PI = 3.14159265;

/* Our ports */
jack_port_t *midi_port;
jack_port_t *output_port;

typedef jack_default_audio_sample_t sample_t;

/*The current sample rate*/
jack_nframes_t sr;

/*one cycle of our sound*/
sample_t* cycle;
/*samples in cycle*/
jack_nframes_t samincy;


// table with frequency of midi note
double midi_freq[128];

// sample offset of each note
double sample_offset[128];

// actual volume of each note
int midi_vol[128];

// actual value of each control
int midi_cc[128];

// the actual prog
int midi_prog = 0;

// the drawbar volumes (vol_xx = 0..8)
// stops
int vol_16 = 8;
int vol_8 = 8;
int vol_4 = 8;
int vol_mix = 8;
// voices
int vol_flute = 8;
int vol_reed = 0;
// vibrato on/off (boolean)
int vibrato = 0;

// master volume (volume = 0..127)
int volume = 64;



// ********************
// our realtime process
// ********************
//
int rt_process_cb( jack_nframes_t nframes, void *void_arg ) {

  // freq modulation for vibrato
  static int shift_dir = 1;
  static double f_shift = 1.0;

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
        midi_vol[note]=0;
      } else if ( ( event.buffer[0] >> 4 ) == 0x09 ) {// note_on note vol
        note = event.buffer[1];
        midi_vol[note]= event.buffer[2];
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

  // grab our audio output buffer 
  sample_t *out = (sample_t *) jack_port_get_buffer (output_port, nframes);

  // fill the buffer
  // this implements the signal flow of an electronic organ
  for ( jack_nframes_t i=0; i<nframes; i++ ) {
    value_flute = value_reed = 0.0;
    // polyphonic output with drawbars vol_xx
    // advance the individual sample pointers
    // use only these notes
    for ( int note = 24; note <= 108; note++ ) {
      int vol = midi_vol[note];
      if ( vol ) { // note actually playing
        // use square law for drawbar volume
        // 16' stop (subharmonic)
        if ( vol_16 ) {
          if ( note >= 36 ) {
            value_flute += vol * vol_16 * vol_16 * cycle[ pos=sample_offset[ note-12 ] ];
            value_reed += vol * vol_16 * vol_16 * ( 1.0 - 2.0 * pos / sr );
          }
        }

        // 8' stop (unison stop)
        if ( vol_8 ) {
          value_flute +=  vol * vol_8 * vol_8 * cycle[ pos=sample_offset[ note ] ];
          value_reed += vol * vol_8 * vol_8 * ( 1.0 - 2.0 * pos / sr );
        }

        // 4' stop (octave)
        if ( vol_4 ) {
          if ( note <=96 ) 
            pos = sample_offset[ note+12 ];
          else // repetition 1 octave below
            pos = sample_offset[ note ];
          value_flute += vol * vol_4 * vol_4 * cycle[ pos ];
          value_reed += vol * vol_4 * vol_4 *( 1.0 - 2.0 * pos / sr );
        }

        // mixture stop
        if ( vol_mix ) {
          // 2 2/3' (octave + fifth)
          if ( note <= 89 )
             pos=sample_offset[ note+19 ];
          else if ( note <= 101 ) // rep 1 oct. below
             pos=sample_offset[ note+7 ];
          else // rep 2 oct. below
             pos=sample_offset[ note-5 ];
          value_flute += vol * vol_mix * vol_mix * cycle[ pos ];
          value_reed += vol * vol_mix * vol_mix * ( 1.0 - 2.0 * pos / sr );

          // 2' (super octave)
          if ( note <= 84 )
            pos=sample_offset[ note+24 ];
          else if ( note <= 96 ) // rep 1 oct.
            pos=sample_offset[ note+12 ];
          else // rep 2 oct.
            pos=sample_offset[ note ];
          value_flute += vol * vol_mix * vol_mix * cycle[ pos ];
          value_reed += vol * vol_mix * vol_mix * ( 1.0 - 2.0 * pos / sr );

          // 1 3/5' (2 oct. + major third)
          if ( note <= 80 )
            pos=sample_offset[ note+28 ];
          else if ( note <= 92 ) // rep. 1 oct.
            pos=sample_offset[ note+16 ];
          else if ( note <= 104 ) // rep 2 oct.
            pos=sample_offset[ note+4 ];
          value_flute += vol * vol_mix * vol_mix * cycle[ pos=sample_offset[ note+28 ] ];
          value_reed += vol * vol_mix * vol_mix * ( 1.0 - 2.0 * pos / sr );

          // 1 1/3' (2 oct. + fifth)
          // (for 1' like v*x c*ntinental change 77->72 and 31->36)
          if ( note <= 77 ) 
            pos=sample_offset[ note+31 ];
          else if ( note <= 89 )
            pos=sample_offset[ note+19 ];
          else if ( note <= 101 )
            pos=sample_offset[ note+7 ];
          else 
            pos=sample_offset[ note-5 ];
          value_flute += vol * vol_mix * vol_mix * cycle[ pos ];
          value_reed += vol * vol_mix * vol_mix * ( 1.0 - 2.0 * pos / sr );
        }
      }

      // advance individual sample pointer, do fm
      sample_offset[note] += f_shift * midi_freq[note];
      if ( sample_offset[note] >= sr )
        sample_offset[note] -= sr;
    } // for ( note )

    // normalize the output (square law for drawbar volumes)
    // vol_16, vol_8, vol_4, vol_mix, vol_flute and vol_reed: range 0..8
    // midi_vol, volume: range 0..127
    out[i] = 0.25 * volume * ( vol_flute * vol_flute * value_flute 
                            + vol_reed * vol_reed * value_reed ) 
           / ( 127 * 127 * 8 * 8 * 8 * 8 );

    // shifting the pitch
    if ( vibrato ) {
      if ( shift_dir > 0 ) {
        if ( f_shift < 1.02 )  // about +35 cent (0.06 is 100 cent)
          f_shift += 0.5 / sr; // sr is int - USE FLOAT CONST
        else
          shift_dir = -1;
      } else if ( shift_dir < 0 ) {
        if ( f_shift > 0.98 ) // about -35 cent
          f_shift -= 0.5 / sr;
        else
          shift_dir = 1;
      }
    } else {
      f_shift = 1.0;
    } // vibrato

  } // for ( nframes )
  return 0;
}



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



void print_help( void ) {
  puts( "\n\n\n\nESC\tQuit" );
  puts( "a q\t16' stop\tpull / shift");
  puts( "s w\t 8' stop\tpull / shift");
  puts( "d e\t 4' stop\tpull / shift");
  puts( "f r\tmix stop\tpull / shift");
  puts( "j u\tflute voice\tpull / shift");
  puts( "k i\treed voice\tpull / shift");
  puts( "v\tvibrato\t\ton/off\n");
}



void print_status( void ) {
  int line;
  printf( "Connie %s (%s)\n", connie_version, connie_name );
  printf( "+---------------------------------+\n" );
  printf( "| 16'  8'  4' mix   fl  rd    vib |\n" );
  printf( "+---------------------------------+\n" );
  for ( line = 0; line < 8; line++ ) {
    printf( "|  %c   %c   %c   %c     %c   %c     %c  |\n", 
  	vol_16>line?'#':' ', vol_8>line?'#':' ', 
  	vol_4>line?'#':' ', vol_mix>line?'#':' ', 
  	vol_flute>line?'#':' ', vol_reed>line?'#':' ', 
  	vibrato?'#':' ' );
  }
  printf( "+---------------------------------+\n" );
}



int main( int argc, char *argv[] ) {

  char *name = "connie";

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
  jack_client_t *client;

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

  /* create two ports */
  output_port = jack_port_register( client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  midi_port = jack_port_register( client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

 /*Create 1 cycle of the wave*/
  /*Calculate the number of samples in one cycle of the wave*/
  samincy = sr;
  /*Calculate our scale multiplier*/
  sample_t scale = 2 * PI / samincy;
  /*Allocate the space needed to store one cycle*/
  cycle = (sample_t *) malloc( samincy * sizeof( sample_t ) );
 /*Exit if allocation failed (more sense from Jussi)*/
  if ( cycle == NULL ) {
    fprintf( stderr,"memory allocation failed\n" );
    return 1;
  }

  // And fill it up with one period of sine wave
  // maybe a RC filtered square wave sounds more natural
  for ( int i=0; i < samincy; i++ ) {
    cycle[i] = sin( i * scale ); // sin
  }

  // build list of midi frequencies starting from lowest C (note 0)
  // (three halftones above the very low A six octaves down from a' 440 Hz)
  double f = concert_pitch_440 / 64 * halftone * halftone * halftone;
  for ( int midinote=0; midinote<128; midinote++ ) {
    midi_freq[midinote] = f;
    f *= halftone; 
    midi_vol[midinote] = 0;
    sample_offset[midinote] = 0.0;
  }


  /* tell the JACK server that we are ready to roll */
  if (jack_activate( client ) ) {
    fprintf( stderr, "cannot activate client\n" );
    return 1;
  }

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
               QUIT } cmd = NONE;

  while ( running ) {
    c = getchar();
    switch( c ) {
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

      // vibrato on/off
      case 'v':
      case 'V':
        cmd = NONE;
        if ( vibrato ) 
          vibrato = 0;
        else
          vibrato = 1;
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

      // execute command
      case '\n':
        switch( cmd ) {
          case QUIT:
            running = 0;
          case NONE:
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
        } // switch( cmd )

        if ( running ) {
          print_help();
          print_status();
        }
        break;

      // catch undefined cmds
      default:
        // printf( "c = %d\n", c );
        cmd = NONE;
        break;
    } // switch( c )
  } //while running

  jack_client_close( client );
  free( cycle );

  exit( 0 );
}

