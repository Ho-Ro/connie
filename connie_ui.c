/*****************************************************************************
 *
 *   connie_ui.c
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

// ANSI colors
#define WHITE 0
#define GREY 37
#define RED 31
#define GREEN 32
#define BROWN 33

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
int ui_colors_0[DRAWBARS_0] = { 
  WHITE, WHITE, WHITE, WHITE,
  RED, RED, RED,
  GREEN, GREEN, GREEN
};

// some program presets
#define PRESETS_0 10
// the drawbar volumes (vol_xx = 0..8)
static int ui_preset_0[PRESETS_0][DRAWBARS_0] = {
    { 6, 8, 6, 8, 8, 4, 0, 0, 0, 4 }, // preset 0
    { 0, 8, 6, 8, 4, 8, 4, 0, 0, 4 }, // preset 1
    { 0, 8, 8, 8, 0, 8, 8, 0, 0, 4 }, // preset 2
    { 4, 8, 4, 6, 8, 4, 0, 1, 0, 4 }, // preset 3
    { 4, 8, 6, 4, 8, 0, 0, 2, 0, 4 }, // preset 4
    { 8, 0, 0, 0, 8, 0, 0, 4, 0, 4 }, // preset 5
    { 0, 8, 0, 0, 8, 0, 0, 0, 0, 4 }, // preset 6
    { 0, 0, 8, 0, 8, 0, 0, 0, 0, 4 }, // preset 7
    { 0, 0, 0, 8, 8, 0, 0, 0, 0, 4 }, // preset 8
    { 8, 8, 8, 8, 8, 8, 8, 4, 0, 8 }  // preset 9
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
int ui_colors_1[DRAWBARS_1] = { 
  BROWN, BROWN, WHITE, WHITE, GREY, WHITE, GREY, GREY, WHITE,
  GREEN, GREEN, GREEN 
};

#define PRESETS_1 10
// the drawbar volumes (vol_xx = 0..8)
static int ui_preset_1[PRESETS_1][DRAWBARS_1] = {
    { 4, 2,   7, 8, 6, 6,   2, 4, 4,   0, 0, 4 }, // preset 0
    { 0, 0,   4, 5, 4, 5,   4, 4, 0,   0, 0, 4 }, // preset 1
    { 0, 0,   4, 4, 3, 2,   2, 2, 0,   0, 0, 4 }, // preset 2
    { 0, 0,   7, 3, 7, 3,   4, 3, 0,   0, 0, 4 }, // preset 3
    { 0, 0,   4, 5, 4, 4,   2, 2, 2,   0, 0, 4 }, // preset 4
    { 0, 0,   6, 6, 4, 4,   3, 2, 0,   0, 0, 4 }, // preset 5
    { 0, 0,   5, 6, 4, 2,   2, 0, 0,   0, 0, 4 }, // preset 6
    { 0, 0,   6, 8, 4, 5,   4, 3, 3,   0, 0, 4 }, // preset 7
    { 0, 0,   8, 0, 3, 0,   0, 0, 0,   0, 0, 4 }, // preset 8
    { 8, 8,   8, 8, 8, 8,   8, 8, 8,   4, 0, 8 }, // preset 9
};

// some ugly globals, fn pointer, etc.
static int *ui_draw = ui_draw_0;
static ui_t *ui_ui = ui_ui_0;
static int *ui_colors = ui_colors_0;
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
  tg_vol_sh     = ui_draw[6] * ui_draw[6] / 96.0;
  // three effects
  tg_percussion = ui_draw[7] / 8.0;
  tg_vibrato    = ui_draw[8] / 8.0;
  tg_reverb     = ui_draw[9] * ui_draw[9] / 64.0;
}

static void ui_set_volumes_1( void ) {
  // simple relation: one draw -> one stop
  for ( int i = 0; i < STOPS_1; i++ ) {
    tg_vol[i] = ui_draw[i] * ui_draw[i] / 64.0;
  }
  // three effects
  tg_percussion = ui_draw[STOPS_1] / 8.0;
  tg_vibrato    = ui_draw[STOPS_1+1] / 8.0;
  tg_reverb     = ui_draw[STOPS_1+2] * ui_draw[STOPS_1+2] / 64.0;
  // only sine waves
  tg_vol_fl     = 1.0;
  tg_vol_rd     = 0.0;
  tg_vol_sh     = 0.0;
}


static void ( *ui_set_volumes)() = ui_set_volumes_0;


// select the proper functions and constants for the models
static void ui_set_model( model_t model ) {
  switch ( model ) {
    default:
    case CONNIE:
      ui_draw = ui_draw_0;
      ui_ui = ui_ui_0;
      ui_drawbars = DRAWBARS_0;
      ui_colors = ui_colors_0;
      ui_stops = STOPS_0;
      ui_presets = PRESETS_0;
      ui_set_volumes = ui_set_volumes_0;
      ui_connie_model = CONNIE;
      break;
    case HAMMOND:
      ui_draw = ui_draw_1;
      ui_ui = ui_ui_1;
      ui_drawbars = DRAWBARS_1;
      ui_colors = ui_colors_1;
      ui_stops = STOPS_1;
      ui_presets = PRESETS_1;
      ui_set_volumes = ui_set_volumes_1;
      ui_connie_model = HAMMOND;
      break;
  }
}


// set drawbars according to presets
int ui_set_program( int prog ) {
  switch ( ui_connie_model ){
    default:
    case CONNIE:
      if ( prog >= 0 && prog < PRESETS_0 ) {
        // stops
        for ( int i = 0; i < DRAWBARS_0; i++ ) {
          ui_draw[i]    = ui_preset_0[prog][i];
        }
        ui_set_volumes();
        ui_value_changed = 1;
      }
      break;
    case HAMMOND:
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


// set drawbars according to init values
int ui_set_drawbars( const int *draws ) {
  for ( int i = 0; i < draws[0]; i++ ) {
    ui_draw[i]    = draws[i+1];
  }
  ui_set_volumes();
  ui_value_changed = 1;
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
    default:
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
  printf( "   %s: %s (%s), %s, %5.1f Hz\n\n", 
        jack_name, connie_version, name, inton_name, concert_pitch );
  printf( "   [ESC]\t\t\t\tQUIT\n   [SPACE]\t\t\t\tPANIC\n" );
  printf( "   %c%c%c%c%c%c... and %c%c%c%c%c%c... \t\tStops\n   ", 
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
  printf( "    " );
  for ( int i = 0; i < ui_drawbars; i++ )
    printf( "______" );
  printf( "\b \n" );
  // drawbar names
  printf( "   |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( "\e[%dm%s\e[0m|", ui_colors[i], ui_ui[i].name );
  }
  printf( "\n" );
  // drawbar up cmd
  printf( "   |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( " \e[%dm[%c]\e[0m |", ui_colors[i], kbd_translate( ui_ui[i].up ) );
  }
  printf( "\n" );
  // drawbar values
  printf( "   |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( "__\e[%dm%d\e[0m__|", ui_colors[i], ui_draw[i] );
  }
  printf( "\b|\n" );
  // the drawbars
  for ( int line = 0; line < 8; line++ ) {
    printf( "   |" );
    for ( int i = 0; i < ui_drawbars; i++ ) {
      printf( " \e[%dm%s\e[0m  ", ui_colors[i], ui_draw[i]>line?"###":"   " );
    }
    printf( "\b|\n" );
  }
  // drawbar down cmd
  printf( "   |" );
  for ( int i = 0; i < ui_drawbars; i++ ) {
    printf( "_\e[%dm[%c]\e[0m__", ui_colors[i], kbd_translate( ui_ui[i].dn ) );
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


static int ui_status = 0;

char *session_dir = NULL;

static int ui_connie_model;
static int ui_kbd;

void ui_save( int type, const char *path ) {
  switch( type ) {
    case 0:
      puts( "SAVE0" );
      ui_status = 0;
      break;
    case 1:
      puts( "SAVE1" );
      session_dir = strdup( path );
      ui_status = 1;
      break;
    case 2:
      puts( "SAVE2" );
      session_dir = strdup( path );
      ui_status = 2;
      break;
    case 3:
      puts( "SAVE3" );
      session_dir = strdup( path );
      ui_status = 3;
      break;
    default:
      puts( "SAVE_" );
      ui_status = 0;
      break;
  }
}



// simple "gui" control
// ********************
//
void ui_init( const int connie_model, const keybd_t kbd ) {

  struct termios t;
  // get term status
  tcgetattr (1, &t);
  ui_term_orig = t; // store status in global var
  // set keyboard to non canonical mode
  t.c_lflag &= ~(ICANON | ECHO);
  tcsetattr (1, 0, &t);
  atexit( ui_shutdown ); // tidy up

  ui_connie_model = connie_model;
  ui_kbd = kbd;

  ui_set_kbd( kbd ); // QWERTY, QWERTZ or AZERTY
  ui_set_model( connie_model ); // 
  ui_set_program( 0 );
}



void ui_loop( const char *name ) {

  int cmd;

  while ( 2 != ui_status ) {
    if ( kbhit() ) {
      cmd = kbd_translate( toupper( getchar() ) );
      if ( ' ' == cmd ) { // SPACE -> panic
        tg_panic();
        ui_value_changed++;
      } else if ( '\033' == cmd ) { // ESC -> QUIT
        printf( "QUIT? [y/N] :" );
        cmd = getchar();
        putchar( cmd );
        if ( 'y' == cmd || 'Y' == cmd )
          ui_status = 2;
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
    switch ( ui_status ) {
      default:
      case 1:
      case 3:
        ui_status = 0;
      case 2:
        {
          FILE *cfg = fopen( ".connie_session", "w" );
          fprintf( cfg, "###########################\n" );
          fprintf( cfg, "### connie session file ###\n" );
          fprintf( cfg, "###########################\n\n" );
          if ( uuid )
            fprintf( cfg, "UUID = \"%s\"\n", uuid );
          fprintf( cfg, "jack_name = \"%s\"\n", jack_name );
          fprintf( cfg, "connie_model = %d\n", ui_connie_model );
          fprintf( cfg, "keybd = %d\n", ui_kbd );
          fprintf( cfg, "intonation = %d\n", intonation );
          fprintf( cfg, "concert_pitch = %f\n", concert_pitch );
          fprintf( cfg, "transpose = %d\n", transpose );
          fprintf( cfg, "midi_channel = %d\n", tg_midi_channel );
          fprintf( cfg, "drawbars = { " );
          for ( int iii=0; iii < ui_drawbars; iii++ ) {
            fprintf( cfg, "%d, ", ui_draw[iii] );
          }
          fprintf( cfg, "}\n" );
          fclose( cfg );
          printf( "ui_status = %d, session_dir = %s\n", ui_status, session_dir );
        }
      case 0:
        break;
    }
  } //while 2 != ui_status
} // connie_ui()

