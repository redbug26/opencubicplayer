/* OpenCP Module Player
 * copyright (c) 1994-'10 Niklas Beisert <nbeisert@physik.tu-muenchen.de>
 * copyright (c) 2004-'22 Stian Skjelstad <stian.skjelstad@gmail.com>
 *
 * FFT routines for the spectrum analysers
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * revision history: (please note changes here)
 *  -nb980510   Niklas Beisert <nbeisert@physik.tu-muenchen.de>
 *    -first release
 *
 *  -doj980924  Dirk Jagdmann <doj@cubic.org>
 *    -added fft.h
 *    -removed dos h-files and prepared for use with unix
 *    -cleaned up some rubbish
 *    -changed cpianal.cpp and cpigraph.cpp to satisfy new dependencies
 *    -wrote asm for innerloop of fft routine
 *
 * -doj981105  Dirk Jagdmann <doj@cubic.org>
 *   - changed paramter declaration of fftanalyseall()
 *  -fd981119   Felix Domke <tmbinc@gmx.net>
 *    -added the really important 'NO_CPIFACE_IMPORT'
 */

#include "config.h"
#include <math.h>
#include "types.h"
#include "fft.h"

#define POW 11
#define SAMPLES (1<<POW)
#define SAMPLES2 (1<<(POW-1))

static int32_t cossintab86[SAMPLES2][2]=
{{268435455, 0}, {268434192, 823548}, {268430402, 1647088},
{268424086, 2470614}, {268415243, 3294115}, {268403873, 4117586},
{268389978, 4941018}, {268373556, 5764404}, {268354608, 6587735},
{268333134, 7411005}, {268309134, 8234204}, {268282610, 9057326},
{268253559, 9880363}, {268221985, 10703307}, {268187885, 11526150},
{268151261, 12348885}, {268112113, 13171503}, {268070442, 13993997},
{268026247, 14816360}, {267979530, 15638583}, {267930290, 16460659},
{267878529, 17282580}, {267824246, 18104339}, {267767442, 18925927},
{267708118, 19747337}, {267646274, 20568561}, {267581911, 21389591},
{267515029, 22210420}, {267445630, 23031040}, {267373713, 23851443},
{267299279, 24671622}, {267222330, 25491569}, {267142865, 26311275},
{267060886, 27130734}, {266976394, 27949938}, {266889388, 28768878},
{266799870, 29587548}, {266707841, 30405939}, {266613302, 31224044},
{266516253, 32041855}, {266416696, 32859365}, {266314631, 33676565},
{266210059, 34493448}, {266102982, 35310007}, {265993400, 36126233},
{265881315, 36942120}, {265766727, 37757658}, {265649637, 38572841},
{265530047, 39387661}, {265407958, 40202111}, {265283370, 41016182},
{265156286, 41829866}, {265026706, 42643158}, {264894631, 43456047},
{264760063, 44268528}, {264623003, 45080592}, {264483453, 45892232},
{264341413, 46703440}, {264196884, 47514208}, {264049870, 48324529},
{263900369, 49134395}, {263748385, 49943799}, {263593918, 50752733},
{263436971, 51561188}, {263277543, 52369159}, {263115638, 53176637},
{262951256, 53983614}, {262784399, 54790083}, {262615069, 55596036},
{262443267, 56401466}, {262268994, 57206365}, {262092254, 58010726},
{261913046, 58814541}, {261731373, 59617802}, {261547236, 60420502},
{261360638, 61222633}, {261171579, 62024188}, {260980063, 62825159},
{260786089, 63625539}, {260589662, 64425320}, {260390781, 65224495},
{260189450, 66023056}, {259985670, 66820995}, {259779442, 67618305},
{259570769, 68414979}, {259359654, 69211009}, {259146097, 70006387},
{258930101, 70801107}, {258711667, 71595160}, {258490799, 72388539},
{258267497, 73181237}, {258041765, 73973246}, {257813604, 74764559},
{257583016, 75555168}, {257350004, 76345066}, {257114569, 77134246},
{256876715, 77922699}, {256636443, 78710419}, {256393755, 79497398},
{256148653, 80283629}, {255901141, 81069104}, {255651220, 81853816},
{255398893, 82637758}, {255144162, 83420922}, {254887030, 84203301},
{254627498, 84984887}, {254365570, 85765673}, {254101247, 86545652},
{253834533, 87324816}, {253565430, 88103159}, {253293940, 88880672},
{253020066, 89657348}, {252743810, 90433181}, {252465175, 91208162},
{252184165, 91982285}, {251900780, 92755543}, {251615025, 93527927},
{251326901, 94299431}, {251036411, 95070047}, {250743559, 95839768},
{250448347, 96608588}, {250150777, 97376498}, {249850853, 98143491},
{249548577, 98909561}, {249243953, 99674700}, {248936982, 100438900},
{248627668, 101202156}, {248316014, 101964458}, {248002023, 102725801},
{247685698, 103486177}, {247367041, 104245579}, {247046056, 105004000},
{246722745, 105761432}, {246397113, 106517869}, {246069161, 107273304},
{245738893, 108027729}, {245406312, 108781136}, {245071421, 109533520},
{244734224, 110284873}, {244394723, 111035188}, {244052922, 111784458},
{243708823, 112532676}, {243362431, 113279835}, {243013748, 114025927},
{242662778, 114770946}, {242309523, 115514885}, {241953988, 116257736},
{241596176, 116999493}, {241236089, 117740150}, {240873733, 118479697},
{240509108, 119218130}, {240142220, 119955440}, {239773072, 120691622},
{239401667, 121426667}, {239028009, 122160570}, {238652100, 122893322},
{238273946, 123624918}, {237893548, 124355351}, {237510912, 125084613},
{237126040, 125812697}, {236738936, 126539598}, {236349604, 127265307},
{235958047, 127989818}, {235564270, 128713125}, {235168275, 129435220},
{234770066, 130156097}, {234369648, 130875749}, {233967024, 131594169},
{233562198, 132311350}, {233155173, 133027287}, {232745954, 133741970},
{232334544, 134455396}, {231920947, 135167555}, {231505167, 135878442},
{231087209, 136588051}, {230667075, 137296374}, {230244770, 138003404},
{229820298, 138709136}, {229393663, 139413562}, {228964868, 140116675},
{228533919, 140818470}, {228100818, 141518940}, {227665571, 142218077},
{227228181, 142915876}, {226788652, 143612330}, {226346988, 144307431},
{225903194, 145001175}, {225457273, 145693554}, {225009231, 146384561},
{224559070, 147074191}, {224106796, 147762436}, {223652413, 148449291},
{223195924, 149134748}, {222737335, 149818802}, {222276649, 150501445},
{221813871, 151182672}, {221349005, 151862476}, {220882056, 152540851},
{220413028, 153217789}, {219941925, 153893286}, {219468752, 154567334},
{218993513, 155239927}, {218516213, 155911059}, {218036856, 156580724},
{217555447, 157248914}, {217071990, 157915625}, {216586490, 158580849},
{216098952, 159244581}, {215609379, 159906814}, {215117778, 160567542},
{214624151, 161226758}, {214128504, 161884457}, {213630842, 162540632},
{213131169, 163195277}, {212629490, 163848386}, {212125809, 164499953},
{211620132, 165149972}, {211112463, 165798436}, {210602807, 166445340},
{210091169, 167090677}, {209577553, 167734441}, {209061965, 168376627},
{208544409, 169017227}, {208024890, 169656237}, {207503413, 170293650},
{206979983, 170929460}, {206454605, 171563661}, {205927283, 172196248},
{205398023, 172827213}, {204866830, 173456552}, {204333709, 174084258},
{203798664, 174710326}, {203261701, 175334749}, {202722825, 175957522},
{202182041, 176578639}, {201639354, 177198094}, {201094769, 177815881},
{200548291, 178431994}, {199999926, 179046428}, {199449678, 179659176},
{198897553, 180270234}, {198343555, 180879594}, {197787691, 181487253},
{197229965, 182093203}, {196670383, 182697439}, {196108950, 183299955},
{195545670, 183900746}, {194980551, 184499806}, {194413595, 185097130},
{193844811, 185692711}, {193274201, 186286545}, {192701772, 186878625},
{192127530, 187468946}, {191551479, 188057503}, {190973625, 188644290},
{190393974, 189229301}, {189812531, 189812531}
};
static uint16_t permtab[SAMPLES];

void
__attribute__ ((visibility ("internal")))
fftInit(void)
{
	int i,j,k;
	j=0;
	for (i=0; i<SAMPLES; ++i)
	{
		permtab[i]=j;
		for (k=SAMPLES2; k&&(k<=j); k>>=1)
			j-=k;
		j+=k;
	}

	for (i=SAMPLES2/4+1; i<=SAMPLES2/2; ++i)
	{
		cossintab86[i][0]=cossintab86[SAMPLES2/2-i][1];
		cossintab86[i][1]=cossintab86[SAMPLES2/2-i][0];
	}


	for (i=SAMPLES2/2+1; i<SAMPLES2; ++i)
	{
		cossintab86[i][0]=-cossintab86[SAMPLES2-i][0];
		cossintab86[i][1]=cossintab86[SAMPLES2-i][1];
	}
}

static /*inline*/ int imul29(int a, int b)
{
	double d=(double)a*(double)b;
	d/=(1<<29);
	return (int)d;
}

static /*inline*/ void fftCalc(int32_t *xi, int32_t *curcossin, uint32_t d2)
{
	uint32_t xd[2];
	xd[0]=xi[0]-xi[d2+0];
	xi[0]=(xi[0]+xi[d2+0])/2;

	xd[1]=xi[1]-xi[d2+1];
	xi[1]=(xi[1]+xi[d2+1])/2;

	xi[d2+0]=imul29(xd[0],curcossin[0])-imul29(xd[1],curcossin[1]);
	xi[d2+1]=imul29(xd[0],curcossin[1])+imul29(xd[1],curcossin[0]);
}

static /*inline*/ void dofft86(int32_t (*x)[2], const int n)
{
	unsigned int i,j;
	int32_t *xe=x[1<<n];
	int32_t curcossin[2];
	int32_t *xi;
	for (i=POW-n; i<POW; ++i)
	{
		const uint32_t s2dk=SAMPLES2>>i;
		const uint32_t d2=2*s2dk;
		for (j=0; j<s2dk; ++j)
		{
			curcossin[0]=cossintab86[j<<i][0];
			curcossin[1]=cossintab86[j<<i][1];
			for (xi=x[j]; xi<xe; xi+=2*d2)
				fftCalc(xi, curcossin, d2);
		}
	}
}


static int32_t x86[SAMPLES][2];

void fftanalyseall(uint16_t *ana, const int16_t *samp, const int inc, const int bits)
{
	const unsigned int full=1<<bits;
	const unsigned int half=full>>1;
	unsigned int i;
	int32_t xr[2];

	for (i=0; i<full; ++i)
	{
		x86[i][0]=*samp<<12;
		samp+=inc;
		x86[i][1]=0;
	}
	dofft86(x86, bits);

	for (i=1; i<=half; ++i)
	{
		xr[0]=x86[permtab[i]>>(POW-bits)][0]>>12;
		xr[1]=x86[permtab[i]>>(POW-bits)][1]>>12;
		ana[i-1]=sqrt((xr[0]*xr[0]+xr[1]*xr[1])*i);
	}
}
