/*****************************************************************************
 *
 *   connie_tg.h
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
#ifndef CONNIE_TG_H
#define CONNIE_TG_H

// stops
extern float tg_vol[9];

// voices
extern float tg_vol_fl;
extern float tg_vol_rd;
extern float tg_vol_sh;

// master volume
extern float tg_master_vol;

// vibrato frequency
extern float tg_vibrato;

// percussion intensity
extern float tg_percussion;

// reverb intensity
extern float tg_reverb;

// all sound off
extern void tg_panic( void );


#endif
