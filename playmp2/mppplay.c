/* OpenCP Module Player
 * copyright (c) '94-'10 Niklas Beisert <nbeisert@physik.tu-muenchen.de>
 * copyright (c) '04-'20 Stian Skjelstad <stian.skjelstad@gmail.com>
 *
 * MPPlay interface routines
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
 */

#include "config.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "boot/plinkman.h"
#include "boot/psetting.h"
#include "cpiface/cpiface.h"
#include "dev/player.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "filesel/mdb.h"
#include "filesel/pfilesel.h"
#include "mpplay.h"
#include "stuff/compat.h"
#include "stuff/err.h"
#include "stuff/poutput.h"
#include "stuff/sets.h"

#define _MAX_FNAME 8
#define _MAX_EXT 4

static uint32_t mpeglen;
static uint32_t mpegrate;
static time_t starttime;
static time_t pausetime;
static char currentmodname[_MAX_FNAME+1];
static char currentmodext[_MAX_EXT+1];
static char *modname;
static char *composer;

static time_t pausefadestart;
static uint8_t pausefaderelspeed;
static int8_t pausefadedirect;

static void startpausefade(void)
{
	if (plPause)
		starttime=starttime+dos_clock()-pausetime;

	if (pausefadedirect)
	{
		if (pausefadedirect<0)
			plPause=1;
		pausefadestart=2*dos_clock()-DOS_CLK_TCK-pausefadestart;
	} else
		pausefadestart=dos_clock();

	if (plPause)
	{
		plChanChanged=1;
		mpegPause(plPause=0);
		pausefadedirect=1;
	} else
		pausefadedirect=-1;
}

static void dopausefade(void)
{
	int16_t i;
	if (pausefadedirect>0)
	{
		i=(dos_clock()-pausefadestart)*64/DOS_CLK_TCK;
		if (i<0)
			i=0;
		if (i>=64)
		{
			i=64;
			pausefadedirect=0;
		}
	} else {
		i=64-(dos_clock()-pausefadestart)*64/DOS_CLK_TCK;
		if (i>=64)
			i=64;
		if (i<=0)
		{
			i=0;
			pausefadedirect=0;
			pausetime=dos_clock();
			mpegPause(plPause=1);
			plChanChanged=1;
			mcpSetFadePars(64);
			return;
		}
	}
	pausefaderelspeed=i;
	mcpSetFadePars(i);
}

static void mpegDrawGStrings(uint16_t (*buf)[CONSOLE_MAX_X])
{
	struct mpeginfo inf;
	uint32_t tim2;
	int l;
	int p;

	mcpDrawGStrings(buf);

	mpegGetInfo(&inf);

	l=(inf.len>>(10/*-inf.stereo-inf.bit16*/)); /* these now refer to offset in file */
	if (!l) l=1;
	p=(inf.pos>>(10/*-inf.stereo-inf.bit16*/)); /* these now refer to offset in file */

	if (plPause)
		tim2=(pausetime-starttime)/DOS_CLK_TCK;
	else
		tim2=(dos_clock()-starttime)/DOS_CLK_TCK;

	if (plScrWidth<128)
	{
#if 0
		memset(buf[0]+80, 0, (plScrWidth-80)*sizeof(uint16_t));
#endif
		memset(buf[1]+80, 0, (plScrWidth-80)*sizeof(uint16_t));
		memset(buf[2]+80, 0, (plScrWidth-80)*sizeof(uint16_t));

#if 0
		writestring(buf[0], 0, 0x09, " vol: \xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa ", 15);
		writestring(buf[0], 15, 0x09, " srnd: \xfa  pan: l\xfa\xfa\xfam\xfa\xfa\xfar  bal: l\xfa\xfa\xfam\xfa\xfa\xfar ", 41);
		writestring(buf[0], 56, 0x09, " spd: ---% \x1D ptch: ---% ", 24);
		writestring(buf[0], 6, 0x0F, "\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe", (vol+4)>>3);
		writestring(buf[0], 22, 0x0F, srnd?"x":"o", 1);
		if (((pan+70)>>4)==4)
			writestring(buf[0], 34, 0x0F, "m", 1);
		else {
			writestring(buf[0], 30+((pan+70)>>4), 0x0F, "r", 1);
			writestring(buf[0], 38-((pan+70)>>4), 0x0F, "l", 1);
		}
		writestring(buf[0], 46+((bal+70)>>4), 0x0F, "I", 1);
		_writenum(buf[0], 62, 0x0F, speed*100/256, 10, 3);
		_writenum(buf[0], 75, 0x0F, speed*100/256, 10, 3);
#endif

		writestring(buf[1], 57, 0x09, "                       ", 23);
		writestring(buf[1], 0, 0x09, "  pos: ...% / ......k  size: ......k            ", 57);
		_writenum(buf[1], 7, 0x0F, p*100/l, 10, 3);
		writenum(buf[1], 29, 0x0F, l, 10, 6, 1);
		writenum(buf[1], 14, 0x0F, p, 10, 6, 1);

		writestring(buf[2],  0, 0x09, "   mpeg \xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa.\xfa\xfa\xfa: ...............................               time: ..:.. ", 80);
		writestring(buf[2],  8, 0x0F, currentmodname, _MAX_FNAME);
		writestring(buf[2], 16, 0x0F, currentmodext, _MAX_EXT);
#warning modname is not UTF-8
		writestring(buf[2], 22, 0x0F, modname, 31);
		if (plPause)
			writestring(buf[2], 57, 0x0C, " paused ", 8);
		else {
			writestring(buf[2], 57, 0x09, "kbps: ", 6);
			writenum(buf[2], 63, 0x0F, mpeg_Bitrate, 10, 3, 1);
		}
		writenum(buf[2], 74, 0x0F, (tim2/60)%60, 10, 2, 1);
		writestring(buf[2], 76, 0x0F, ":", 1);
		writenum(buf[2], 77, 0x0F, tim2%60, 10, 2, 0);
	} else {
#if 0
		memset(buf[0]+128, 0, (plScrWidth-128)*sizeof(uint16_t));
#endif
		memset(buf[1]+128, 0, (plScrWidth-128)*sizeof(uint16_t));
		memset(buf[2]+128, 0, (plScrWidth-128)*sizeof(uint16_t));

#if 0
		writestring(buf[0], 0, 0x09, "    volume: \xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa  ", 30);
		writestring(buf[0], 30, 0x09, " surround: \xfa   panning: l\xfa\xfa\xfa\xfa\xfa\xfa\xfam\xfa\xfa\xfa\xfa\xfa\xfa\xfar   balance: l\xfa\xfa\xfa\xfa\xfa\xfa\xfam\xfa\xfa\xfa\xfa\xfa\xfa\xfar  ", 72);
		writestring(buf[0], 102, 0x09,  " speed: ---% \x1D pitch: ---%    ", 30);
		writestring(buf[0], 12, 0x0F, "\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe\xfe", (vol+2)>>2);
		writestring(buf[0], 41, 0x0F, srnd?"x":"o", 1);
		if (((pan+68)>>3)==8)
			writestring(buf[0], 62, 0x0F, "m", 1);
		else {
			writestring(buf[0], 54+((pan+68)>>3), 0x0F, "r", 1);
			writestring(buf[0], 70-((pan+68)>>3), 0x0F, "l", 1);
		}
		writestring(buf[0], 83+((bal+68)>>3), 0x0F, "I", 1);
		_writenum(buf[0], 110, 0x0F, speed*100/256, 10, 3);
		_writenum(buf[0], 124, 0x0F, speed*100/256, 10, 3);
#endif

		writestring(buf[1], 0, 0x09, "    position: ...% / ......k  size: ......k                 opt: .....Hz, .. bit, ......", 92);
		_writenum(buf[1], 14, 0x0F, p*100/l, 10, 3);
		writenum(buf[1], 36, 0x0F, l, 10, 6, 1);
		writenum(buf[1], 21, 0x0F, p, 10, 6, 1);
		writenum(buf[1], 65, 0x0F, inf.rate, 10, 5, 1);
		writenum(buf[1], 74, 0x0F, 8<<inf.bit16, 10, 2, 1);
		writestring(buf[1], 82, 0x0F, inf.stereo?"stereo":"mono", 6);

		writestring(buf[1], 92, 0x09, "                                        ", 40);

		if (plPause)
			tim2=(pausetime-starttime)/DOS_CLK_TCK;
		else
			tim2=(dos_clock()-starttime)/DOS_CLK_TCK;

		writestring(buf[2],  0, 0x09, "      mpeg \xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa.\xfa\xfa\xfa: ...............................  composer: ...............................                  time: ..:..    ", 132);
		writestring(buf[2], 11, 0x0F, currentmodname, _MAX_FNAME);
		writestring(buf[2], 19, 0x0F, currentmodext, _MAX_EXT);
#warning modname is now UTF-8 etc.. and we are missing album, artist...
		writestring(buf[2], 25, 0x0F, modname, 31);
		writestring(buf[2], 68, 0x0F, composer, 31);
		if (plPause)
			writestring(buf[2], 100, 0x0C, "playback paused", 15);
		else {
			writestring(buf[2], 100, 0x09, "kbps: ", 6);
			writenum(buf[2], 106, 0x0F, mpeg_Bitrate, 10, 3, 1);
		}
		writenum(buf[2], 123, 0x0F, (tim2/60)%60, 10, 2, 1);
		writestring(buf[2], 125, 0x0F, ":", 1);
		writenum(buf[2], 126, 0x0F, tim2%60, 10, 2, 0);
	}
}

static int mpegProcessKey(uint16_t key)
{
	switch (key)
	{
		case KEY_ALT_K:
			cpiKeyHelp('p', "Start/stop pause with fade");
			cpiKeyHelp('P', "Start/stop pause with fade");
			cpiKeyHelp(KEY_CTRL_P, "Start/stop pause");
			cpiKeyHelp('<', "Jump back (big)");
			cpiKeyHelp(KEY_CTRL_LEFT, "Jump back (big)");
			cpiKeyHelp('>', "Jump forward (big)");
			cpiKeyHelp(KEY_CTRL_RIGHT, "Jump forward (big)");
			cpiKeyHelp(KEY_CTRL_UP, "Jump back (small)");
			cpiKeyHelp(KEY_CTRL_DOWN, "Jump forward (small)");
			cpiKeyHelp(KEY_CTRL_HOME, "Jump to start of track");
			mcpSetProcessKey (key);
			return 0;
		case 'p': case 'P':
			startpausefade();
			break;
		case KEY_CTRL_P:
			pausefadedirect=0;
			if (plPause)
				starttime=starttime+dos_clock()-pausetime;
			else
				pausetime=dos_clock();
			plPause=!plPause;
			mpegPause(plPause);
			break;
		case KEY_CTRL_UP:
			mpegSetPos(mpegGetPos()-mpegrate);
			break;
		case KEY_CTRL_DOWN:
			mpegSetPos(mpegGetPos()+mpegrate);
			break;
		case '<':
		case KEY_CTRL_LEFT:
			{
				uint32_t pos = mpegGetPos();
				uint32_t newpos = pos - (mpeglen>>5);
				if (newpos > pos)
				{
					newpos = 0;
				}
				mpegSetPos(newpos);
			}
			break;
		case '>':
		case KEY_CTRL_RIGHT:
			{
				uint32_t pos = mpegGetPos();
				uint32_t newpos = pos + (mpeglen>>5);
				if ((newpos < pos) || (newpos > mpeglen))
				{
					newpos = mpeglen - 4;
				}
				mpegSetPos(newpos);
			}
			break;
		case KEY_CTRL_HOME:
			mpegSetPos(0);
			break;
		default:
			return mcpSetProcessKey (key);
	}
	return 1;
}


static int mpegLooped(void)
{
	if (pausefadedirect)
		dopausefade();
	mpegSetLoop(fsLoopMods);
	mpegIdle();
	if (plrIdle)
		plrIdle();
	return !fsLoopMods&&mpegIsLooped();
}


static void mpegCloseFile(void)
{
	ID3InfoDone();
	mpegClosePlayer();
}

static int mpegOpenFile(struct moduleinfostruct *info, struct ocpfilehandle_t *mpegfile, const char *ldlink, const char *loader) /* no loader needed/used by this plugin */
{
	const char *filename;
	struct mpeginfo inf;

	if (!mpegfile)
		return -1;
#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif
#warning currentmodname currentmodext
	/* currentmodname and currentmodext as not expected to be zero-terminated */
	//strncpy(currentmodname, info->name, _MAX_FNAME);
	//strncpy(currentmodext, info->name + _MAX_FNAME, _MAX_EXT);
#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif
	modname=info->title;
	composer=info->composer;

	dirdbGetName_internalstr (mpegfile->dirdb_ref, &filename);
	fprintf(stderr, "preloading %s...\n", filename);

	plIsEnd=mpegLooped;
	plProcessKey=mpegProcessKey;
	plDrawGStrings=mpegDrawGStrings;
	plGetMasterSample=plrGetMasterSample;
	plGetRealMasterVolume=plrGetRealMasterVolume;

	if (mpegOpenPlayer(mpegfile))
		return errFileRead;

	starttime=dos_clock();
	plPause=0;
	pausefadedirect=0;

	mpegGetInfo(&inf);
	mpeglen=inf.len;
	mpegrate=inf.rate;

	ID3InfoInit();

	return errOk;
}

struct cpifaceplayerstruct mpegPlayer = {"[MPEG, libmad plugin]", mpegOpenFile, mpegCloseFile};
struct linkinfostruct dllextinfo = {.name = "playmp2", .desc = "OpenCP Audio MPEG Player (c) 1994-2020 Stian Skjelstad, Niklas Beisert & Tammo Hinrichs", .ver = DLLVERSION, .size = 0};
