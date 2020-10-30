/*****************************************************************************
 *
 *   reverb.c
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


#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <linux/types.h>

#include "reverb.h"

// modified JCRev (with output feedback)
// -> ccrma.stanford.edu/~jos/pasp/Schroeder_Reverberator_called_JCRev.html
// JCRev uses 3 all pass filtes in series
// and 4 parallel feed forward comb filters
// to replace the FFCF with IIR filters uncomment next line

// #define IIR

// three all pass filters
// ======================
// length of delay line
#define NA1 1051
#define NA2  337
#define NA3  113
// gain
#define GA1 0.707
#define GA2 0.707
#define GA3 0.707
// delay lines
static float ap1[NA1];
static float ap2[NA2];
static float ap3[NA3];
// position in delay line
static int ia1;
static int ia2;
static int ia3;


// four comb filters
// =================
#ifndef IIR
// length of delay line
#define NC1 4799
#define NC2 4999
#define NC3 5399
#define NC4 5801
#else
// length of delay line
#define NC1 479
#define NC2 499
#define NC3 539
#define NC4 581
#endif
// delay line
static float cf1[NC1];
static float cf2[NC2];
static float cf3[NC3];
static float cf4[NC4];
// position in delay line
static int ic1;
static int ic2;
static int ic3;
static int ic4;

//
// DENORMALS ARE EVIL
//
// 32 bit float
// SEEEEEEEEMMMMMMMMMMMMMMMMMMMMMMM
// E = 0, M != 0 -> denormal
// processing denormals uses lot of cpu.
// problem: an IIR feeds back 0.7*y.
// a value > 0 will decay until the smallest float is reached:
// 00000000000000000000000000000001
// multiplying with 0.7 and rounding (to nearest, default) gives again:
// 00000000000000000000000000000001
// this value circulates forever and consumes lot of cpu cycles :(
// even with "round to zero" - set in main() -
// it takes about 5 seconds until the denorm fades to zero...
//
// solution:
// "it's better to burn out than to fade away"
//
// denormals are zero
static inline float daz( float f )
{
  // define an aliasing type to perform a "reinterpret cast"
  typedef __u32 __attribute__ (( __may_alias__ )) u32bit;
  if ( *( (u32bit*)&f ) & 0x7F000000 ) // E > 1 : normal.
    return f;
  else // E <= 1 : zero or _almost_ denormal 
       // (may become denormal with next operation)
    return 0.0;
}



//
// reverb for one sample
//
float reverb( float xin )
{
  static float yout = 0.0;
  static float xv0, xv1, yv0, yv1;
  float x, y;

  // additional feedback
  x  = daz( xin/8 + yout/64 );

// three all pass filters
  y = ap1[ia1];
  ap1[ia1] = daz( GA1 * (x + y) );
  x = y - x;
  if ( ++ia1 >= NA1 )
    ia1 = 0;

  y = ap2[ia2];
  ap2[ia2] = daz( GA2 * (x + y) );
  x = y - x;
  if ( ++ia2 >= NA2 )
    ia2 = 0;

  y = ap3[ia3];
  ap3[ia3] = daz( GA3 * (x + y) );
  x = y - x;
  if ( ++ia3 >= NA3 )
    ia3 = 0;


#ifndef IIR
// four feed forward comb filters 

// gain
#define GC1 0.742
#define GC2 0.733
#define GC3 0.715
#define GC4 0.697

  yout = 0;

  yout += x + GC1 * cf1[ic1];
  cf1[ic1] = x;
  if ( ++ic1 >= NC1 )
    ic1 = 0;

  yout += x + GC2 * cf2[ic2];
  cf2[ic2] = x;
  if ( ++ic2 >= NC2 )
    ic2 = 0;

  yout += x + GC3 * cf3[ic3];
  cf3[ic3] = x;
  if ( ++ic3 >= NC3 )
    ic3 = 0;

  yout += x + GC4 * cf4[ic4];
  cf4[ic4] = x;
  if ( ++ic4 >= NC4 )
    ic4 = 0;

#else

// four recursive comb filters

// gain
#define GC1 0.7
#define GC2 0.7
#define GC3 0.7
#define GC4 0.7

  yout = 0.0;

  y = cf1[ic1];
  cf1[ic1] = daz( x + GC1 * y );
  if ( ++ic1 >= NC1 )
    ic1 = 0;
  yout += y;

  y = cf2[ic2];
  cf2[ic2] = daz( x + GC2 * y );
  if ( ++ic2 >= NC2 )
    ic2 = 0;
  yout += y;

  y = cf3[ic3];
  cf3[ic3] = daz( x + GC3 * y );
  if ( ++ic3 >= NC3 )
    ic3 = 0;
  yout += y;

  y = cf4[ic4];
  cf4[ic4] = daz( x + GC4 * y );
  if ( ++ic4 >= NC4 )
    ic4 = 0;
  yout += y;

#endif

  // IIR LP filter 3000 Hz
  xv0 = xv1;
  xv1 = yout/6;
  yv0 = yv1;
  yout = yv1 = daz( xv0 + xv1 + 0.668 * yv0 );

  return yout;

}


