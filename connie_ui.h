/*****************************************************************************
 *
 *   connie_ui.h
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


//extern int ui_value_changed;

typedef enum keybd_enum { QWERTY=0, QWERTZ, AZERTY } keybd_t;

extern int ui_set_program( int prog );
extern void ui( const char *name, const int connie_model, const keybd_t keybd );

