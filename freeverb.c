//-----------------------------------------------------
// name : "freeverb"
// version : "1.0"
// author : "Grame" 
// license : "BSD"
// copyright : "(c)GRAME 2006"
//
// Code generated with Faust 0.9.9.4 (http://faust.grame.fr)
//-----------------------------------------------------
/* link with  */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <time.h>

#include <libgen.h>
#include <jack/jack.h>

// g++ -O3 -lm -lsndfile  myfx.cpp

//----------------------------------------------------------------
//  d�inition du processeur de signal
//----------------------------------------------------------------
	float 	freeverbRoomSize;
	float 	freeverbDamping;
	float 	freeverbWet;

	float 	fRec9[2];
	int 	IOTA;
	float 	fVec0[2048];
	float 	fRec8[2];
	float 	fRec11[2];
	float 	fVec1[2048];
	float 	fRec10[2];
	float 	fRec13[2];
	float 	fVec2[2048];
	float 	fRec12[2];
	float 	fRec15[2];
	float 	fVec3[2048];
	float 	fRec14[2];
	float 	fRec17[2];
	float 	fVec4[2048];
	float 	fRec16[2];
	float 	fRec19[2];
	float 	fVec5[2048];
	float 	fRec18[2];
	float 	fRec21[2];
	float 	fVec6[2048];
	float 	fRec20[2];
	float 	fRec23[2];
	float 	fVec7[2048];
	float 	fRec22[2];
	float 	fVec8[1024];
	float 	fRec6[2];
	float 	fVec9[512];
	float 	fRec4[2];
	float 	fVec10[512];
	float 	fRec2[2];
	float 	fVec11[256];
	float 	fRec0[2];
	float 	fRec33[2];
	float 	fVec12[2048];
	float 	fRec32[2];
	float 	fRec35[2];
	float 	fVec13[2048];
	float 	fRec34[2];
	float 	fRec37[2];
	float 	fVec14[2048];
	float 	fRec36[2];
	float 	fRec39[2];
	float 	fVec15[2048];
	float 	fRec38[2];
	float 	fRec41[2];
	float 	fVec16[2048];
	float 	fRec40[2];
	float 	fRec43[2];
	float 	fVec17[2048];
	float 	fRec42[2];
	float 	fRec45[2];
	float 	fVec18[2048];
	float 	fRec44[2];
	float 	fRec47[2];
	float 	fVec19[2048];
	float 	fRec46[2];
	float 	fVec20[1024];
	float 	fRec30[2];
	float 	fVec21[512];
	float 	fRec28[2];
	float 	fVec22[512];
	float 	fRec26[2];
	float 	fVec23[256];
	float 	fRec24[2];
	float 	fSamplingFreq;

	float fSlow0;
	float fSlow1;
	float fSlow2;
	float fSlow3;
	float fSlow4;


void freeverbInit(int samplingFreq) {
	fSamplingFreq = samplingFreq;
	freeverbRoomSize = 0.500000f;
	freeverbDamping = 0.500000f;
	freeverbWet = 0.333300f;
		for (int i=0; i<2; i++) fRec9[i] = 0;
	IOTA = 0;
	for (int i=0; i<2048; i++) fVec0[i] = 0;
	for (int i=0; i<2; i++) fRec8[i] = 0;
	for (int i=0; i<2; i++) fRec11[i] = 0;
	for (int i=0; i<2048; i++) fVec1[i] = 0;
	for (int i=0; i<2; i++) fRec10[i] = 0;
	for (int i=0; i<2; i++) fRec13[i] = 0;
	for (int i=0; i<2048; i++) fVec2[i] = 0;
	for (int i=0; i<2; i++) fRec12[i] = 0;
	for (int i=0; i<2; i++) fRec15[i] = 0;
	for (int i=0; i<2048; i++) fVec3[i] = 0;
	for (int i=0; i<2; i++) fRec14[i] = 0;
	for (int i=0; i<2; i++) fRec17[i] = 0;
	for (int i=0; i<2048; i++) fVec4[i] = 0;
	for (int i=0; i<2; i++) fRec16[i] = 0;
	for (int i=0; i<2; i++) fRec19[i] = 0;
	for (int i=0; i<2048; i++) fVec5[i] = 0;
	for (int i=0; i<2; i++) fRec18[i] = 0;
	for (int i=0; i<2; i++) fRec21[i] = 0;
	for (int i=0; i<2048; i++) fVec6[i] = 0;
	for (int i=0; i<2; i++) fRec20[i] = 0;
	for (int i=0; i<2; i++) fRec23[i] = 0;
	for (int i=0; i<2048; i++) fVec7[i] = 0;
	for (int i=0; i<2; i++) fRec22[i] = 0;
	for (int i=0; i<1024; i++) fVec8[i] = 0;
	for (int i=0; i<2; i++) fRec6[i] = 0;
	for (int i=0; i<512; i++) fVec9[i] = 0;
	for (int i=0; i<2; i++) fRec4[i] = 0;
	for (int i=0; i<512; i++) fVec10[i] = 0;
	for (int i=0; i<2; i++) fRec2[i] = 0;
	for (int i=0; i<256; i++) fVec11[i] = 0;
	for (int i=0; i<2; i++) fRec0[i] = 0;
	for (int i=0; i<2; i++) fRec33[i] = 0;
	for (int i=0; i<2048; i++) fVec12[i] = 0;
	for (int i=0; i<2; i++) fRec32[i] = 0;
	for (int i=0; i<2; i++) fRec35[i] = 0;
	for (int i=0; i<2048; i++) fVec13[i] = 0;
	for (int i=0; i<2; i++) fRec34[i] = 0;
	for (int i=0; i<2; i++) fRec37[i] = 0;
	for (int i=0; i<2048; i++) fVec14[i] = 0;
	for (int i=0; i<2; i++) fRec36[i] = 0;
	for (int i=0; i<2; i++) fRec39[i] = 0;
	for (int i=0; i<2048; i++) fVec15[i] = 0;
	for (int i=0; i<2; i++) fRec38[i] = 0;
	for (int i=0; i<2; i++) fRec41[i] = 0;
	for (int i=0; i<2048; i++) fVec16[i] = 0;
	for (int i=0; i<2; i++) fRec40[i] = 0;
	for (int i=0; i<2; i++) fRec43[i] = 0;
	for (int i=0; i<2048; i++) fVec17[i] = 0;
	for (int i=0; i<2; i++) fRec42[i] = 0;
	for (int i=0; i<2; i++) fRec45[i] = 0;
	for (int i=0; i<2048; i++) fVec18[i] = 0;
	for (int i=0; i<2; i++) fRec44[i] = 0;
	for (int i=0; i<2; i++) fRec47[i] = 0;
	for (int i=0; i<2048; i++) fVec19[i] = 0;
	for (int i=0; i<2; i++) fRec46[i] = 0;
	for (int i=0; i<1024; i++) fVec20[i] = 0;
	for (int i=0; i<2; i++) fRec30[i] = 0;
	for (int i=0; i<512; i++) fVec21[i] = 0;
	for (int i=0; i<2; i++) fRec28[i] = 0;
	for (int i=0; i<512; i++) fVec22[i] = 0;
	for (int i=0; i<2; i++) fRec26[i] = 0;
	for (int i=0; i<256; i++) fVec23[i] = 0;
	for (int i=0; i<2; i++) fRec24[i] = 0;
}

void freeverbSetParam( void ) {
	fSlow0 = (0.400000f * freeverbRoomSize);
	fSlow1 = (1 - fSlow0);
	fSlow2 = (0.700000f + (0.280000f * freeverbDamping));
	fSlow3 = freeverbWet;
	fSlow4 = (1 - fSlow3);
}

void freeverbComputeOne( float* samples ) {
	fRec9[0] = ((fSlow1 * fRec8[1]) + (fSlow0 * fRec9[1]));
	float fTemp0 = samples[1];
	float fTemp1 = samples[0];
	float fTemp2 = (1.500000e-02f * (fTemp1 + fTemp0));
	fVec0[IOTA&2047] = (fTemp2 + (fSlow2 * fRec9[0]));
	fRec8[0] = fVec0[(IOTA-1617)&2047];
	fRec11[0] = ((fSlow1 * fRec10[1]) + (fSlow0 * fRec11[1]));
	fVec1[IOTA&2047] = (fTemp2 + (fSlow2 * fRec11[0]));
	fRec10[0] = fVec1[(IOTA-1557)&2047];
	fRec13[0] = ((fSlow1 * fRec12[1]) + (fSlow0 * fRec13[1]));
	fVec2[IOTA&2047] = (fTemp2 + (fSlow2 * fRec13[0]));
	fRec12[0] = fVec2[(IOTA-1491)&2047];
	fRec15[0] = ((fSlow1 * fRec14[1]) + (fSlow0 * fRec15[1]));
	fVec3[IOTA&2047] = (fTemp2 + (fSlow2 * fRec15[0]));
	fRec14[0] = fVec3[(IOTA-1422)&2047];
	fRec17[0] = ((fSlow1 * fRec16[1]) + (fSlow0 * fRec17[1]));
	fVec4[IOTA&2047] = (fTemp2 + (fSlow2 * fRec17[0]));
	fRec16[0] = fVec4[(IOTA-1356)&2047];
	fRec19[0] = ((fSlow1 * fRec18[1]) + (fSlow0 * fRec19[1]));
	fVec5[IOTA&2047] = (fTemp2 + (fSlow2 * fRec19[0]));
	fRec18[0] = fVec5[(IOTA-1277)&2047];
	fRec21[0] = ((fSlow1 * fRec20[1]) + (fSlow0 * fRec21[1]));
	fVec6[IOTA&2047] = (fTemp2 + (fSlow2 * fRec21[0]));
	fRec20[0] = fVec6[(IOTA-1188)&2047];
	fRec23[0] = ((fSlow1 * fRec22[1]) + (fSlow0 * fRec23[1]));
	fVec7[IOTA&2047] = (fTemp2 + (fSlow2 * fRec23[0]));
	fRec22[0] = fVec7[(IOTA-1116)&2047];
	float fTemp3 = (((((((fRec22[0] + fRec20[0]) + fRec18[0]) + fRec16[0]) + fRec14[0]) + fRec12[0]) + fRec10[0]) + fRec8[0]);
	fVec8[IOTA&1023] = (fTemp3 + (0.500000f * fRec6[1]));
	fRec6[0] = fVec8[(IOTA-556)&1023];
	float 	fRec7 = (fRec6[1] - fTemp3);
	fVec9[IOTA&511] = (fRec7 + (0.500000f * fRec4[1]));
	fRec4[0] = fVec9[(IOTA-441)&511];
	float 	fRec5 = (fRec4[1] - fRec7);
	fVec10[IOTA&511] = (fRec5 + (0.500000f * fRec2[1]));
	fRec2[0] = fVec10[(IOTA-341)&511];
	float 	fRec3 = (fRec2[1] - fRec5);
	fVec11[IOTA&255] = (fRec3 + (0.500000f * fRec0[1]));
	fRec0[0] = fVec11[(IOTA-225)&255];
	float 	fRec1 = (fRec0[1] - fRec3);
	// output channel 0
	samples[0] = ((fSlow4 * fTemp1) + (fSlow3 * fRec1));
	fRec33[0] = ((fSlow1 * fRec32[1]) + (fSlow0 * fRec33[1]));
	fVec12[IOTA&2047] = (fTemp2 + (fSlow2 * fRec33[0]));
	fRec32[0] = fVec12[(IOTA-1640)&2047];
	fRec35[0] = ((fSlow1 * fRec34[1]) + (fSlow0 * fRec35[1]));
	fVec13[IOTA&2047] = (fTemp2 + (fSlow2 * fRec35[0]));
	fRec34[0] = fVec13[(IOTA-1580)&2047];
	fRec37[0] = ((fSlow1 * fRec36[1]) + (fSlow0 * fRec37[1]));
	fVec14[IOTA&2047] = (fTemp2 + (fSlow2 * fRec37[0]));
	fRec36[0] = fVec14[(IOTA-1514)&2047];
	fRec39[0] = ((fSlow1 * fRec38[1]) + (fSlow0 * fRec39[1]));
	fVec15[IOTA&2047] = (fTemp2 + (fSlow2 * fRec39[0]));
	fRec38[0] = fVec15[(IOTA-1445)&2047];
	fRec41[0] = ((fSlow1 * fRec40[1]) + (fSlow0 * fRec41[1]));
	fVec16[IOTA&2047] = (fTemp2 + (fSlow2 * fRec41[0]));
	fRec40[0] = fVec16[(IOTA-1379)&2047];
	fRec43[0] = ((fSlow1 * fRec42[1]) + (fSlow0 * fRec43[1]));
	fVec17[IOTA&2047] = (fTemp2 + (fSlow2 * fRec43[0]));
	fRec42[0] = fVec17[(IOTA-1300)&2047];
	fRec45[0] = ((fSlow1 * fRec44[1]) + (fSlow0 * fRec45[1]));
	fVec18[IOTA&2047] = (fTemp2 + (fSlow2 * fRec45[0]));
	fRec44[0] = fVec18[(IOTA-1211)&2047];
	fRec47[0] = ((fSlow1 * fRec46[1]) + (fSlow0 * fRec47[1]));
	fVec19[IOTA&2047] = (fTemp2 + (fSlow2 * fRec47[0]));
	fRec46[0] = fVec19[(IOTA-1139)&2047];
	float fTemp4 = (((((((fRec46[0] + fRec44[0]) + fRec42[0]) + fRec40[0]) + fRec38[0]) + fRec36[0]) + fRec34[0]) + fRec32[0]);
	fVec20[IOTA&1023] = (fTemp4 + (0.500000f * fRec30[1]));
	fRec30[0] = fVec20[(IOTA-579)&1023];
	float 	fRec31 = (fRec30[1] - fTemp4);
	fVec21[IOTA&511] = (fRec31 + (0.500000f * fRec28[1]));
	fRec28[0] = fVec21[(IOTA-464)&511];
	float 	fRec29 = (fRec28[1] - fRec31);
	fVec22[IOTA&511] = (fRec29 + (0.500000f * fRec26[1]));
	fRec26[0] = fVec22[(IOTA-364)&511];
	float 	fRec27 = (fRec26[1] - fRec29);
	fVec23[IOTA&255] = (fRec27 + (0.500000f * fRec24[1]));
	fRec24[0] = fVec23[(IOTA-248)&255];
	float 	fRec25 = (fRec24[1] - fRec27);
	// output channel 1
	samples[1] = ((fSlow4 * fTemp0) + (fSlow3 * fRec25));
	// post processing
	fRec24[1] = fRec24[0];
	fRec26[1] = fRec26[0];
	fRec28[1] = fRec28[0];
	fRec30[1] = fRec30[0];
	fRec46[1] = fRec46[0];
	fRec47[1] = fRec47[0];
	fRec44[1] = fRec44[0];
	fRec45[1] = fRec45[0];
	fRec42[1] = fRec42[0];
	fRec43[1] = fRec43[0];
	fRec40[1] = fRec40[0];
	fRec41[1] = fRec41[0];
	fRec38[1] = fRec38[0];
	fRec39[1] = fRec39[0];
	fRec36[1] = fRec36[0];
	fRec37[1] = fRec37[0];
	fRec34[1] = fRec34[0];
	fRec35[1] = fRec35[0];
	fRec32[1] = fRec32[0];
	fRec33[1] = fRec33[0];
	fRec0[1] = fRec0[0];
	fRec2[1] = fRec2[0];
	fRec4[1] = fRec4[0];
	fRec6[1] = fRec6[0];
	fRec22[1] = fRec22[0];
	fRec23[1] = fRec23[0];
	fRec20[1] = fRec20[0];
	fRec21[1] = fRec21[0];
	fRec18[1] = fRec18[0];
	fRec19[1] = fRec19[0];
	fRec16[1] = fRec16[0];
	fRec17[1] = fRec17[0];
	fRec14[1] = fRec14[0];
	fRec15[1] = fRec15[0];
	fRec12[1] = fRec12[0];
	fRec13[1] = fRec13[0];
	fRec10[1] = fRec10[0];
	fRec11[1] = fRec11[0];
	fRec8[1] = fRec8[0];
	IOTA = IOTA+1;
	fRec9[1] = fRec9[0];
}

