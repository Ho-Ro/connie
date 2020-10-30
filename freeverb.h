//-----------------------------------------------------
// name : "freeverb"
// version : "1.0"
// author : "Grame" 
// license : "BSD"
// copyright : "(c)GRAME 2006"
//
// Code generated with Faust 0.9.9.4 (http://faust.grame.fr)
//-----------------------------------------------------

//----------------------------------------------------------------
//  d�inition du processeur de signal
//----------------------------------------------------------------
extern float 	freeverbRoomSize;
extern float 	freeverbDamping;
extern float 	freeverbWet;

extern void freeverbInit( int samplingFreq );
extern void freeverbSetParam( void );
extern void freeverbComputeOne( float* samples );

