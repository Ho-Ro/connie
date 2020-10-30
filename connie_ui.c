/*****************************************************************************
 *
 *   connie_ui.c
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

#include "connie.h"
#include "connie_tg.h"
#include "connie_ui.h"


// **********************************************************
// the user interface globals
// **********************************************************

static int ui_value_changed = 1;
static int ui_connie_model = 0;

// ui definitions
typedef struct {
  char *name; // the name of the drawbar
  char up;    // cmd to move up
  char dn;    // cmd to move down
} ui_t;


// our model 0, the original connie
#define STOPS_0 4
#define DRAWBARS_0 10
int ui_draw_0[DRAWBARS_0] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
ui_t ui_ui_0[DRAWBARS_0] = {
  { " 16  ", 'Q', 'A' }, // stops
  { "  8  ", 'W', 'S' }, //   " 
  { "  4  ", 'E', 'D' }, //   " 
  { " IV  ", 'R', 'F' }, //   " 
  { "  ~  ", 'T', 'G' }, // voice
  { "  M  ", 'Y', 'H' }, //   " 
  { "sharp", 'U', 'J' }, //   " 
  { "perc.", 'Z', 'X' }, // percussion
  { "vibr.", 'C', 'V' }, // vibrato
  { "rev. ", 'B', 'N' }  // reverb
};

// some program presets
#define PRESETS_0 10
// the drawbar volumes (vol_xx = 0..8)
static int ui_preset_0[PRESETS_0][DRAWBARS_0] = {
    { 6, 8, 6, 8, 8, 4, 0, 0, 0, 0 }, // preset 0
    { 0, 8, 6, 8, 4, 8, 4, 0, 0, 0 }, // preset 1
    { 0, 8, 8, 8, 0, 8, 8, 0, 0, 0 }, // preset 2
    { 4, 8, 4, 6, 8, 4, 0, 1, 0, 0 }, // preset 3
    { 4, 8, 6, 4, 8, 0, 0, 2, 0, 0 }, // preset 4
    { 8, 0, 0, 0, 8, 0, 0, 4, 0, 0 }, // preset 5
    { 0, 8, 0, 0, 8, 0, 0, 0, 0, 0 }, // preset 6
    { 0, 0, 8, 0, 8, 0, 0, 0, 0, 0 }, // preset 7
    { 0, 0, 0, 8, 8, 0, 0, 0, 0, 0 }, // preset 8
    { 8, 8, 8, 8, 8, 8, 8, 8, 0, 0 }  // preset 9
};



// the test model with individual drawbars for each tonegen stop
#define STOPS_1 9
#define DRAWBARS_1 (STOPS_1+3)
int ui_draw_1[DRAWBARS_1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
ui_t ui_ui_1[DRAWBARS_1] = {
  { " 16  ", 'Q', 'A' }, // stops
  { "5 1/3", 'W', 'S' }, //   " 
  { "  8  ", 'E', 'D' }, //   " 
  { "  4  ", 'R', 'F' }, //   " 
  { "2 2/3", 'T', 'G' }, //   " 
  { "  2  ", 'Y', 'H' }, //   " 
  { "1 3/5", 'U', 'J' }, //   " 
  { "1 1/3", 'I', 'K' }, //   " 
  { "  1  ", 'O', 'L' }, //   " 
  { "perc.", 'Z', 'X' }, // percussion
  { "vibr.", 'C', 'V' }, // vibrato
  { "rev. ", 'B', 'N' }  // reverb
};

#define PRESETS_1 10
// the drawbar volumes (vol_xx = 0..8)
static int ui_preset_1[PRESETS_1][DRAWBARS_1] = {
    { 4, 2,   7, 8, 6, 6,   2, 4, 4,   0, 0, 0 }, // preset 0
    { 0, 0,   4, 5, 4, 5,   4, 4, 0,   0, 0, 0 }, // preset 1
    { 0, 0,   4, 4, 3, 2,   2, 2, 0,   0, 0, 0 }, // preset 2
    { 0, 0,   7, 3, 7, 3,   4, 3, 0,   0, 0, 0 }, // preset 3
    { 0, 0,   4, 5, 4, 4,   2, 2, 2,   0, 0, 0 }, // preset 4
    { 0, 0,   6, 6, 4, 4,   3, 2, 0,   0, 0, 0 }, // preset 5
    { 0, 0,   5, 6, 4, 2,   2, 0, 0,   0, 0, 0 }, // preset 6
    { 0, 0,   6, 8, 4, 5,   4, 3, 3,   0, 0, 0 }, // preset 7
    { 0, 0,   8, 0, 3, 0,   0, 0, 0,   0, 0, 0 }, // preset 8
    { 8, 8,   8, 8, 8, 8,   8, 8, 8,   8, 0, 0 }, // preset 9
};

// some ugly globals, fn pointer, etc.
static int *ui_draw = ui_draw_0;
static ui_t *ui_ui = ui_ui_0;
static int ui_drawbars = DRAWBARS_0;
static int ui_stops = STOPS_0;
static int ui_presets = PRESETS_0;


static void ui_set_volumes_0( void ) {
  tg_vol[0]     = ui_draw[0] * ui_draw[0] / 64.0;
  tg_vol[2]     = ui_draw[1] * ui_draw[1] / 64.0;
  tg_vol[3]     = ui_draw[2] * ui_draw[2] / 64.0;
  // the mixture draw controls four stops
  tg_vol[4]     =
  tg_vol[5]     =
  tg_vol[6]     =
  tg_vol[8]     = ui_draw[3] * ui_draw[3] / 64.0;
  // three voices
  tg_vol_fl     = ui_draw[4] * ui_draw[4] / 64.0;
  tg_vol_rd     = ui_draw[5] * ui_draw[5] / 64.0;
  tg_vol_sh     = ui_draw[6] * ui_draw[6] / 64.0;
  // threeo effects
  tg_percussion = ui_draw[7] / 8.0;
  tg_vibrato    = ui_draw[8] / 8.0;
  tg_reverb     = ui_draw[9] / 8.0;
}

static void ui_set_volumes_1( void ) {
  // simple relation: one draw -> one stop
  for ( int i = 0; i < STOPS_1; i++ ) {
    tg_vol[i] = ui_draw[i] * ui_draw[i] / 64.0;
  }
  // three effects
  tg_percussion = ui_draw[STOPS_1] / 8.0;
  tg_vibrato    = ui_draw[STOPS_1+1] / 8.0;
  tg_reverb     = ui_draw[STOPS_1+2] / 8.0;
  // only sine waves
  tg_vol_fl     = 1.0;
  tg_vol_rd     = 0.0;
  tg_vol_sh     = 0.0;
}


static void ( *ui_set_volumes)() = ui_set_volumes_0;

// select the proper functions and constants for the models
static void ui_set_model( int model ) {
  switch ( model ) {
    default:
    case 0:
      ui_draw = ui_draw_0;
      ui_ui = ui_ui_0;
      ui_drawbars = DRAWBARS_0;
      ui_stops = STOPS_0;
      ui_presets = PRESETS_0;
      ui_set_volumes = ui_set_volumes_0;
      ui_connie_model = 0;
      break;
    case 1:
      ui_draw = ui_draw_1;
      ui_ui = ui_ui_1;
      ui_drawbars = DRAWBARS_1;
      ui_stops = STOPS_1;
      ui_presets = PRESETS_1;
      ui_set_volumes = ui_set_volumes_1;
      ui_connie_model = 1;
      break;
  }
}


// set drawbars according to presets
int ui_set_program( int prog ) {
  switch ( ui_connie_model ){
    case 0:
      if ( prog >= 0 && prog < PRESETS_0 ) {
        // stops
        for ( int i = 0; i < DRAWBARS_0; i++ ) {
          ui_draw[i]    = ui_preset_0[prog][i];
        }
        ui_set_volumes();
        ui_value_changed = 1;
      }
      break;
    case 1:
      if ( prog >= 0 && prog < PRESETS_1 ) {
        // stops
        for ( int i = 0; i < DRAWBARS_1; i++ ) {
          ui_draw[i]    = ui_preset_1[prog][i];
        }
        ui_set_volumes();
        ui_value_changed = 1;
      }
      break;
  }
  return 0;
}



// keyboard translation for QWERTY, QWERTZ and AZERTY
static char kbd_translate_QWERTY( char c ) {
  return  c;
}
//
static char kbd_translate_QWERTZ( char c ) {
  switch( c ) {
    case 'Z':
      return 'Y';
    case 'Y':
      return 'Z';
   }
  return c;
}
//
static char kbd_translate_AZERTY( char c ) {
  switch( c ) {
    case 'A':
      return 'Q';
    case 'Q':
      return 'A';
    case 'W':
      return 'Z';
    case 'Z':
      return 'W';
  }
  return c;
}

// the function pointer
static char (*kbd_translate)( char ) = kbd_translate_QWERTY;


static void ui_set_kbd( keybd_t kbd ) {
  switch ( kbd ) {
    case QWERTY:
      kbd_translate = kbd_translate_QWERTY;
      break;
    case QWERTZ:
      kbd_translate = kbd_translate_QWERTZ;
      break;
    case AZERTY:
      kbd_translate = kbd_translate_AZERTY;
      break;
  }
}


// explain the user interface
static void print_help( const char *name ) {
  printf( "\n\n\n\n\n" );
  printf( "  %s: %s (%s)\n\n", name, connie_version, connie_name );
  printf( "  [ESC]\t\t\t\t\tQUIT\n  [SPACE]\t\t\t\tPANIC\n" );
  printf( "  %c%c%c%c%c%c... and %c%c%c%c%c%c... \t\tStops\n  ", 
          kbd_translate( 'Q' ), kbd_translate( 'W' ),
          kbd_translate( 'E' ), kbd_translate( 'R' ),
          kbd_translate( 'T' ), kbd_translate( 'Y' ),
          kbd_translate( 'A' ), kbd_translate( 'S' ),
          kbd_translate( 'D' ), kbd_translate( 'F' ),
          kbd_translate( 'G' ), kbd_translate( 'H' ) );
  for ( int i = 0; i < ui_presets; i++ ) {
    printf( "%d  ", i );
  }
  for ( int i = ui_presets; i < 10; i++ ) {
    printf( "   " );
  }
  printf( "\tPresets\n\n");
}


// show drawbars
static void print_status( void ) {
  // the headline
  printf( "   " );
  for ( int i = 0; i < ui_drawbars; i++ )
    printf( "______" );
  printf( "\b \n" );
  // drawbar names
  printf( "  |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( "%s|", ui_ui[i].name );
  }
  printf( "\n" );
  // drawbar up cmd
  printf( "  |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( " [%c] |", kbd_translate( ui_ui[i].up ) );
  }
  printf( "\n" );
  // drawbar values
  printf( "  |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( "__%d__|", ui_draw[i] );
  }
  printf( "\b|\n" );
  // the drawbars
  for ( int line = 0; line < 8; line++ ) {
    printf( "  |" );
    for ( int i = 0; i < ui_drawbars; i++ ) {
      printf( " %s  ", ui_draw[i]>line?"###":"   " );
    }
    printf( "\b|\n" );
  }
  // drawbar down cmd
  printf( "  |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( "_[%c]__", kbd_translate( ui_ui[i].dn ) );
  }
  printf( "\b|\n\n" );
  fflush( stdout );
}



// the original terminal io settings (needed by atexit() function)
static struct termios ui_term_orig;

// called via atexit()
static void ui_shutdown( void )
{
  // restore original term settings
  tcsetattr( 1, 0, &ui_term_orig );
  printf("\n");
}


// true if char pending, nonblocking
static int kbhit()
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



// simple "gui" control
// ********************
//
void ui( const char *name, const int connie_model, const keybd_t kbd ) {

  struct termios t;
  // get term status
  tcgetattr (1, &t);
  ui_term_orig = t; // store status in global var
  // set keyboard to non canonical mode
  t.c_lflag &= ~(ICANON | ECHO);
  tcsetattr (1, 0, &t);
  atexit( ui_shutdown ); // tidy up

  ui_set_kbd( kbd ); // QWERTY, QWERTZ or AZERTY
  ui_set_model( connie_model ); // 
  ui_set_program( 0 );

  int cmd;
  int ui_running = 1;

  while ( ui_running ) {
    if ( kbhit() ) {
      cmd = kbd_translate( toupper( getchar() ) );
      if ( ' ' == cmd ) { // SPACE -> panic
        tg_panic();
      } else if ( '\033' == cmd ) { // ESC -> QUIT
        printf( "QUIT? [y/N] :" );
        cmd = getchar();
        putchar( cmd );
        if ( 'y' == cmd || 'Y' == cmd )
          ui_running = 0;
        else
          ui_value_changed++; // force redraw
      } else if ( isdigit( cmd ) ) { // number -> set prog
        ui_set_program( cmd - '0' );
        //ui_value_changed++;
      } else if ( isalpha( cmd ) ){ // alpha -> search drawbar cmd
        for ( int i = 0; i < ui_drawbars; i++ ) {
          if ( cmd == ui_ui[i].dn ) {
            if ( ui_draw[i] < 8 ) {
              ui_draw[i]++;
              ui_value_changed++;
              break;
            } 
          } else if ( cmd == ui_ui[i].up ) {
            if ( ui_draw[i] > 0 ) {
              ui_draw[i]--;
              ui_value_changed++;
              break;
            } 
          }
        }
      }
    } 
    if ( ui_value_changed ) {
      ui_set_volumes();
      print_help( name );
      print_status();
      ui_value_changed = 0;
    } else {
      usleep( 10000 );
    }
  } //while running
} // connie_ui()

