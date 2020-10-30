/*****************************************************************************
 *
 *   scales.h
 *   Inspired by Fons' aeolus project
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
 
#ifndef SCALES_H
#define SCALES_H

#define _GNU_SOURCE



// gear ratio of hammond organ
// http://de.wikipedia.org/wiki/Hammond-Orgel#Tonerzeugung
// motor speed 1200 rpm -> 20 revs/sec
// 2 teeth on lowest wheel -> 10 Hz * gear ratio
#define C (104.f / 85.f)
const float scale_hammond_gears[12] = { 
    C *  85.f / 104.f, // C (~8.2 Hz)
    C *  71.f /  82.f, // C#
    C *  67.f /  73.f, // D
    C * 105.f / 108.f, // D#
    C * 103.f / 100.f, // E
    C *  84.f /  77.f, // F
    C *  74.f /  64.f, // F#
    C *  98.f /  80.f, // G
    C *  96.f /  74.f, // G#
    C *  88.f /  64.f, // A (10 Hz * 88 / 64  = 13.75 Hz)
    C *  67.f /  46.f, // A#
    C * 108.f /  70.f  // B
};
#undef C



// Equal temperament
// step: 2 ** 1/12 = 1.059463094
// http://en.wikipedia.org/wiki/Equal_temperament
const float scale_equaltemp[12] =
{
    1.f,
    1.059463094f,
    1.122462048f,
    1.189207115f,
    1.259921050f,
    1.334839854f,
    1.414213562f,
    1.498307077f,
    1.587401052f,
    1.681792830f,
    1.781797436f,
    1.887748625f,
};



// Reine Stimmung (pure intonation)
// http://de.wikipedia.org/wiki/Reine_Stimmung#Erweiterte_reine_Stimmung
const float scale_pure[12] =
{
    1.f,
    16.f / 15.f,
    9.f / 8.f,
    6.f / 5.f,
    5.f / 4.f,
    4.f / 3.f,
    45.f / 32.f,
    3.f / 2.f,
    8.f / 5.f,
    5.f / 3.f,
    16.f / 9.f,
    15.f / 8.f,
};



// Pythagorean tuning
// http://en.wikipedia.org/wiki/Pythagorean_tuning
const float scale_pythagorean[12] =
{
    1.0f,
    256.f / 243.f,
    9.f / 8.f,
    32.f / 27.f,
    81.f / 64.f,
    4.f / 3.f,
    729.f / 512.f,
    3.f / 2.f,
    128.f / 81.f,
    27.f / 16.f,
    16.f / 9.f,
    243.f / 128.f,
};



// Quarter comma meantone (Pietro Aaron, 1523)
// http://en.wikipedia.org/wiki/Quarter-comma_meantone

#define X 1.044906727f
#define S 1.069984488f
#define T 1.118033989f
#define P 1.495348781f

const float scale_meanquart[12] = 
{
    1.f,
    X,
    T,
    T * S,
    T * T,
    T * T * S,
    T * T * T,
    P,
    P * X,
    P * T,
    P * T * S,
    P * T * T
};
#undef X 
#undef S
#undef T
#undef P



// Werckmeister temperament III (Andreas Werckmeister, 1681)
// http://en.wikipedia.org/wiki/Werckmeister_temperament

#define R2 1.414213562
#define RR2 1.189207115
#define RR8 1.681792831
const float scale_werckmeister3[12] = 
{
    1.f,
    256.f / 243.f,
    R2 * 64.f / 81.f,
    32.f / 27.f,
    RR2 * 256.f / 243.f,
    4.f / 3.f,
    1024.f / 729.f,
    RR8 * 8.f / 9.f,
    128.f / 81.f,
    RR2 * 1024.f / 729.f,
    16.f / 9.f,
    RR2 * 128.f / 81.f
};
#undef R2
#undef RR2
#undef RR8



// Kirnberger III (Johann Philipp Kirnberger, 1779?)
// http://groenewald-berlin.de/ttg/TTG_T093.html
// http://groenewald-berlin.de/tabellen/TAB-093.html

#define R5 2.236067977
#define RR5 1.495348781
#define RR125 3.343701525
const float scale_kirnberger3[12] = 
{
    1.0f,
    256.f / 243.f,
    R5 / 2.f,
    32.f / 27.f,
    5.f / 4.f,
    4.f / 3.f,
    45.f / 32.f,
    RR5,
    128.f / 81.f,
    RR125 / 2.f,
    16.f / 9.f,
    15.f / 8.f,
};
#undef R5
#undef RR5
#undef RR125



struct temper {
    const char *label;
    const float *f_ratio;
} scales[] =
{
    { "Hammond Gears",    scale_hammond_gears },
    { "Equally Tempered", scale_equaltemp },
    { "Pure Intonation",  scale_pure },
    { "Pythagorean",      scale_pythagorean },
    { "¼ Comma Meantone", scale_meanquart },
    { "Werckmeister III", scale_werckmeister3 },
    { "Kirnberger III",   scale_kirnberger3 },
};

const int NSCALES = sizeof( scales ) / sizeof( struct temper );

#endif

