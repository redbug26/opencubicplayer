/*-----------------------------------------------------------------------------

	ST-Sound ( YM files player library )

	Manage YM file depacking and parsing

-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
* ST-Sound, ATARI-ST Music Emulator
* Copyright (c) 1995-1999 Arnaud Carre ( http://leonard.oxg.free.fr )
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*
-----------------------------------------------------------------------------*/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "YmTypes.h"
#include "YmMusic.h"
#ifdef HAVE_LZH
#include "lzh/lzh.h"
#endif

static	ymu16 ymVolumeTable[16] =
{	62,161,265,377,580,774,1155,1575,2260,3088,4570,6233,9330,13187,21220,32767};


static	void	signeSample(ymu8 *ptr,yms32 size)
{

		if (size>0)
		{
			do
			{
				*ptr++ ^= 0x80;
			}
			while (--size);
		}
}

void	myFree(void **pPtr)
{
		if (*pPtr) free(*pPtr);
		*pPtr = NULL;
}

char	*mstrdup(const char *in)
{
	const int size = strlen(in)+1;
	char *out = (char*)malloc(size);
	if (out)
		strcpy(out,in);
	return out;
}

ymu32      readMotorolaDword(ymu8 **ptr, ymu32 *ptr_size)
{
ymu32 n;
ymu8 *p = *ptr;
	if (*ptr_size<4)
		return 0;
        n = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
        p+=4;
        *ptr = p;
	(*ptr_size)+=4;
        return n;
}

ymu16      readMotorolaWord(ymu8 **ptr, ymu32 *ptr_size)
{
ymu16 n;
ymu8 *p = *ptr;
	if (*ptr_size<2)
		return 0;
        n = (p[0]<<8)|p[1];
        p+=2;
        *ptr = p;
	(*ptr_size)+=2;
        return n;
}

ymchar    *readNtString(ymchar **ptr, ymu32 *ptr_size)
{
ymchar *p;
ymint len = 0;
	if (*ptr_size<=0)
	{
		(*ptr_size)-=1;
		return mstrdup("");
	}
	p=*ptr;
	while(*p)
	{
		p++;
		(*ptr_size)--;
		len++;
		if (*ptr_size==0)
		{
			(*ptr_size)-=1;
			return mstrdup("");
		}
	}

		p = mstrdup(*ptr);
		(*ptr) += len+1;
        return p;
}

yms32	ReadLittleEndian32(ymu8 *pLittle, ymu32 ptr_size)
{
	yms32 v;
	if (ptr_size<4)
		return 0;
	v = ( (pLittle[0]<<0) |
				(pLittle[1]<<8) |
				(pLittle[2]<<16) |
				(pLittle[3]<<24));

	return v;
}

yms32	ReadBigEndian32(ymu8 *pBig, ymu32 ptr_size)
{
	yms32 v;
	if (ptr_size<4)
		return 0;
	v = ( (pBig[0]<<24) |
				(pBig[1]<<16) |
				(pBig[2]<<8) |
				(pBig[3]<<0));

	return v;
}

unsigned char	*CYmMusic::depackFile(ymu32 checkOriginalSize)
 {
#ifndef HAVE_LZH
	return pBigMalloc;
#else
 lzhHeader_t *pHeader;
 ymu8	*pNew;
 ymu8	*pSrc;
 ymu32	ptr_left = fileSize;

		if (ptr_left < (ymint)sizeof(lzhHeader_t))
		{
			return pBigMalloc;
		}

		pHeader = (lzhHeader_t*)pBigMalloc;

		if ((pHeader->size==0) ||					// NOTE: Endianness works because value is 0
			(strncmp(pHeader->id,"-lh5-",5)))
		{ // Le fichier n'est pas compresse, on retourne l'original.
			return pBigMalloc;
		}

		fileSize = (ymu32)-1;

		if (pHeader->level > 1)					// NOTE: Endianness works because value is 0
		{ // Compression LH5, header > 1 : Error.
			free(pBigMalloc);
			pBigMalloc = NULL;
			setLastError("LHARC Header must be 0 !");
			return NULL;
		}

		fileSize = ReadLittleEndian32((ymu8*)&pHeader->original, 4);
		pNew = (ymu8*)malloc(fileSize);
		if (!pNew)
		{
			setLastError("MALLOC Failed !");
			free(pBigMalloc);
			pBigMalloc = NULL;
			return NULL;
		}

		pSrc = pBigMalloc+sizeof(lzhHeader_t)+pHeader->name_lenght;			// NOTE: Endianness works because name_lenght is a byte
		ptr_left -= sizeof(lzhHeader_t)+pHeader->name_lenght;

		pSrc += 2;		// skip CRC16
		ptr_left -= 2;

		if (pHeader->level == 1) { // https://github.com/jca02266/lha/blob/master/header.doc.md
        		pSrc++;   // skip os-type
			ptr_left--;

        		ymu16 nextHeaderSize;

			do {
			    nextHeaderSize = pSrc[0] << 0 | pSrc[1] << 8;
			    pSrc += 2;
			    ptr_left -= 2;

			    pSrc += nextHeaderSize;
			    ptr_left -= nextHeaderSize;
			} while (nextHeaderSize != 0);
		}
	
		ymu32		packedSize = ReadLittleEndian32((ymu8*)&pHeader->packed, 4);

		checkOriginalSize -= ymu32(pSrc - pBigMalloc);

		if (packedSize > checkOriginalSize)
			packedSize = checkOriginalSize;

		if (packedSize > ptr_left)
		{
			setLastError("File too small");
			free(pNew);
			return pBigMalloc;
		}

		// Check for corrupted archive
		if (packedSize <= checkOriginalSize)
		{
			// alloc space for depacker and depack data
			CLzhDepacker *pDepacker = new CLzhDepacker;
			const bool bRet = pDepacker->LzUnpack(pSrc,packedSize,pNew,fileSize);
			delete pDepacker;

			if (!bRet)
			{	// depacking error
				setLastError("LH5 Depacking Error !");
				free(pNew);
				pNew = NULL;
			}
		}
		else
		{
			setLastError("LH5 Depacking Error !");
			free(pNew);
			pNew = NULL;
		}

		// Free up source buffer, whatever depacking fail or success
		free(pBigMalloc);

		return pNew;
#endif
 }





static ymint	fileSizeGet(FILE *h)
 {
 ymint size;
 ymint old;

		old = ftell(h);
		fseek(h,0,SEEK_END);
		size = ftell(h);
		fseek(h,old,SEEK_SET);
		return size;
 }


ymbool	CYmMusic::deInterleave(void)
 {
 yms32	nextPlane[32];
 ymu8	*pW,*tmpBuff;
 yms32	j,k;


		if (attrib&A_STREAMINTERLEAVED)
		{

			tmpBuff = (ymu8*)malloc(nbFrame*streamInc);
			if (!tmpBuff)
			{
				setLastError("Malloc error in deInterleave()\n");
				return YMFALSE;
			}

			// Precalcul les offsets.
			for (j=0;j<streamInc;j++) nextPlane[j] = nbFrame*j;

			pW = tmpBuff;
			for (j=0;j<nextPlane[1];j++)
			{
				for (k=0;k<streamInc;k++)
				{
					pW[k] = pDataStream[j + nextPlane[k]];
				}
				pW += streamInc;
			}

			free(pBigMalloc);
			pBigMalloc = tmpBuff;
			pDataStream = tmpBuff;

			attrib &= (~A_STREAMINTERLEAVED);
		}
		return YMTRUE;
 }


enum
{
	e_YM2a = ('Y' << 24) | ('M' << 16) | ('2' << 8) | ('!'),	//'YM2!'
	e_YM3a = ('Y' << 24) | ('M' << 16) | ('3' << 8) | ('!'),	//'YM3!'
	e_YM3b = ('Y' << 24) | ('M' << 16) | ('3' << 8) | ('b'),	//'YM3b'
	e_YM4a = ('Y' << 24) | ('M' << 16) | ('4' << 8) | ('!'),	//'YM4!'
	e_YM5a = ('Y' << 24) | ('M' << 16) | ('5' << 8) | ('!'),	//'YM5!'
	e_YM6a = ('Y' << 24) | ('M' << 16) | ('6' << 8) | ('!'),	//'YM6!'
	e_MIX1 = ('M' << 24) | ('I' << 16) | ('X' << 8) | ('1'),	//'MIX1'
	e_YMT1 = ('Y' << 24) | ('M' << 16) | ('T' << 8) | ('1'),	//'YMT1'
	e_YMT2 = ('Y' << 24) | ('M' << 16) | ('T' << 8) | ('2'),	//'YMT2'
};

ymbool	CYmMusic::ymDecode(void)
 {
 ymu8 *pUD;
 ymu8	*ptr;
 ymu32 ptr_size = fileSize;
 ymint skip;
 ymint i;
 ymu32 sampleSize;
 yms32 tmp;
 ymu32 id;

		if (ptr_size < 4)
		{ 
			setLastError("File too small");
			return YMFALSE;
		}
		id = ReadBigEndian32((unsigned char*)pBigMalloc, ptr_size);
		switch (id)
		{
			case e_YM2a://'YM2!':		// MADMAX specific.
				songType = YM_V2;
				nbFrame = (fileSize-4)/14;
				if (nbFrame == 0)
				{ 
					setLastError("No frames in file");
					return YMFALSE;
				}
				loopFrame = 0;
				ymChip.setClock(ATARI_CLOCK);
				setPlayerRate(50);
				pDataStream = pBigMalloc+4;
				streamInc = 14;
				nbDrum = 0;
				setAttrib(A_STREAMINTERLEAVED|A_TIMECONTROL);
				pSongName = mstrdup("Unknown");
				pSongAuthor = mstrdup("Unknown");
				pSongComment = mstrdup("Converted by Leonard.");
				pSongType = mstrdup("YM 2");
				pSongPlayer = mstrdup("YM-Chip driver");
				break;

			case e_YM3a://'YM3!':		// Standart YM-Atari format.
				songType = YM_V3;
				nbFrame = (fileSize-4)/14;
				if (nbFrame == 0)
				{ 
					setLastError("No frames in file");
					return YMFALSE;
				}
				loopFrame = 0;
				ymChip.setClock(ATARI_CLOCK);
				setPlayerRate(50);
				pDataStream = pBigMalloc+4;
				streamInc = 14;
				nbDrum = 0;
				setAttrib(A_STREAMINTERLEAVED|A_TIMECONTROL);
				pSongName = mstrdup("Unknown");
				pSongAuthor = mstrdup("Unknown");
				pSongComment = mstrdup("");
				pSongType = mstrdup("YM 3");
				pSongPlayer = mstrdup("YM-Chip driver");
				break;

			case e_YM3b://'YM3b':		// Standart YM-Atari format + Loop info.
				if (ptr_size < 4)
				{ 
					setLastError("File too small");
					return YMFALSE;
				}
				pUD = (ymu8*)(pBigMalloc+fileSize-4);
				songType = YM_V3;
				nbFrame = (fileSize-4)/14;
				if (nbFrame == 0)
				{ 
					setLastError("No frames in file");
					return YMFALSE;
				}
				loopFrame = ReadLittleEndian32(pUD, 4);
				ymChip.setClock(ATARI_CLOCK);
				setPlayerRate(50);
				pDataStream = pBigMalloc+4;
				streamInc = 14;
				nbDrum = 0;
				setAttrib(A_STREAMINTERLEAVED|A_TIMECONTROL);
				pSongName = mstrdup("Unknown");
				pSongAuthor = mstrdup("Unknown");
				pSongComment = mstrdup("");
				pSongType = mstrdup("YM 3b (loop)");
				pSongPlayer = mstrdup("YM-Chip driver");
				break;

			case e_YM4a://'YM4!':		// Extended ATARI format.
				setLastError("No more YM4! support. Use YM5! format.");
				return YMFALSE;
				break;

			case e_YM5a://'YM5!':		// Extended YM2149 format, all machines.
			case e_YM6a://'YM6!':		// Extended YM2149 format, all machines.
				if (ptr_size < 12)
				{ 
					setLastError("File too small");
					return YMFALSE;
				}
				if (strncmp((const char*)(pBigMalloc+4),"LeOnArD!",8))
				{
					setLastError("Not a valid YM format !");
					return YMFALSE;
				}
				ptr = pBigMalloc+12;
				ptr_size -= 12;
				nbFrame = readMotorolaDword(&ptr, &ptr_size);
				setAttrib(readMotorolaDword(&ptr, &ptr_size) | A_TIMECONTROL);
				nbDrum = readMotorolaWord(&ptr, &ptr_size);
				ymChip.setClock(readMotorolaDword(&ptr, &ptr_size));
				setPlayerRate(readMotorolaWord(&ptr, &ptr_size));
				loopFrame = readMotorolaDword(&ptr, &ptr_size);
				skip = readMotorolaWord(&ptr, &ptr_size);
				ptr += skip;
				ptr_size -= skip;
				if (ptr_size <= 0)
				{
					setLastError("File too small");
					return YMFALSE;
				}
				if (nbDrum>0)
				{
					pDrumTab=(digiDrum_t*)calloc(nbDrum, sizeof(digiDrum_t));
					for (i=0;i<nbDrum;i++)
					{
						pDrumTab[i].size = readMotorolaDword(&ptr, &ptr_size);
						if (ptr_size <= 0)
						{
							setLastError("File too small");
							goto error_out;
						}
						if (pDrumTab[i].size)
						{
							if (pDrumTab[i].size >= 0x80000000)
							{
								setLastError("Too big drumtab");
								goto error_out;
							}
							if (ptr_size<pDrumTab[i].size)
							{
								setLastError("File too small");
								goto error_out;
							}
							pDrumTab[i].pData = (ymu8*)malloc(pDrumTab[i].size);
							memcpy(pDrumTab[i].pData,ptr,pDrumTab[i].size);
							if (attrib&A_DRUM4BITS)
							{
								ymu32 j;
								ymu8 *pw = pDrumTab[i].pData;
								for (j=0;j<pDrumTab[i].size;j++)
								{
									*pw = ymVolumeTable[(*pw)&15]>>7;
									pw++;
								}
							}
							ptr += pDrumTab[i].size;
							ptr_size -= pDrumTab[i].size;
						}
						else
						{
							pDrumTab[i].pData = NULL;
						}
					}
					attrib &= (~A_DRUM4BITS);
				}
				pSongName = readNtString((char**)&ptr, &ptr_size);
				pSongAuthor = readNtString((char**)&ptr, &ptr_size);
				pSongComment = readNtString((char**)&ptr, &ptr_size);
				if (ptr_size <= 0)
				{
					setLastError("File too small");
					goto error_out;
				}
				songType = YM_V5;
				if (id==e_YM6a)//'YM6!')
				{
					songType = YM_V6;
					pSongType = mstrdup("YM 6");
				}
				else
				{
					pSongType = mstrdup("YM 5");
				}
				if ((nbFrame >= 0x08000000) || (nbFrame < 0))
				{
					setLastError("Too many frames");
					goto error_out;
				}
				if (ptr_size < ((ymu32)nbFrame * 16))
				{
					setLastError("File too small");
					goto error_out;
				}
				pDataStream = ptr;
				streamInc = 16;
				pSongPlayer = mstrdup("YM-Chip driver");
				break;

			case e_MIX1://'MIX1':		// ATARI Remix digit format.
				if (ptr_size < 12)
				{ 
					setLastError("File too small");
					return YMFALSE;
				}

				if (strncmp((const char*)(pBigMalloc+4),"LeOnArD!",8))
				{
					setLastError("Not a valid YM format !");
					return YMFALSE;
				}
				ptr = pBigMalloc+12;
				ptr_size -= 12;
				songType = YM_MIX1;
				tmp = readMotorolaDword(&ptr, &ptr_size);
				setAttrib(0);
				if (tmp&1) setAttrib(A_DRUMSIGNED);
				sampleSize = readMotorolaDword(&ptr, &ptr_size);
				nbMixBlock = readMotorolaDword(&ptr, &ptr_size);
				if (ptr_size <= 0)
				{
					setLastError("File too small");
					goto error_out;
				}
				if (sampleSize <= 0)
				{
					setLastError("Invalid sampleSize");
					goto error_out;
				}
				if (nbMixBlock <= 0)
				{
					setLastError("Invalid number of mixblocks");
					goto error_out;
				}
				pMixBlock = (mixBlock_t*)malloc(nbMixBlock*sizeof(mixBlock_t));
				for (i=0;i<nbMixBlock;i++)
				{	// Lecture des block-infos.
#warning sampleStart and sampleLength needs to be validated
					pMixBlock[i].sampleStart = readMotorolaDword(&ptr, &ptr_size);
					pMixBlock[i].sampleLength = readMotorolaDword(&ptr, &ptr_size);
					pMixBlock[i].nbRepeat = readMotorolaWord(&ptr, &ptr_size);
					pMixBlock[i].replayFreq = readMotorolaWord(&ptr, &ptr_size);
				}
				pSongName = readNtString((char**)&ptr, &ptr_size);
				pSongAuthor = readNtString((char**)&ptr, &ptr_size);
				pSongComment = readNtString((char**)&ptr, &ptr_size);

				if (sampleSize>=0x80000000)
				{
					setLastError("Invalid sampleSize");
					goto error_out;
				}
				if (ptr_size < sampleSize)
				{
					setLastError("File too small");
					goto error_out;
				}

				pBigSampleBuffer = (unsigned char*)malloc(sampleSize);
				memcpy(pBigSampleBuffer,ptr,sampleSize);

				if (!(attrib&A_DRUMSIGNED))
				{
					signeSample(pBigSampleBuffer,sampleSize);
					setAttrib(A_DRUMSIGNED);
				}

				setAttrib(getAttrib() | A_TIMECONTROL);
				computeTimeInfo();

				mixPos = -1;		// numero du block info.
				currentPente = 0;
				currentPos = 0;
				pSongType = mstrdup("MIX1");
				pSongPlayer = mstrdup("Digi-Mix driver");

				break;

			case e_YMT1://'YMT1':		// YM-Tracker
			case e_YMT2://'YMT2':		// YM-Tracker
/*;
; Format du YM-Tracker-1
;
; 4  YMT1
; 8  LeOnArD!
; 2  Nb voice
; 2  Player rate
; 4  Music lenght
; 4  Music loop
; 2  Nb digidrum
; 4  Flags		; Interlace, signed, 8 bits, etc...
; NT Music Name
; NT Music author
; NT Music comment
; nb digi *
*/
				if (ptr_size < 12)
				{ 
					setLastError("File too small");
					return YMFALSE;
				}

				if (strncmp((const char*)(pBigMalloc+4),"LeOnArD!",8))
				{
					setLastError("Not a valid YM format !");
					return YMFALSE;
				}
				ptr = pBigMalloc+12;
				ptr_size -= 12;
				songType = YM_TRACKER1;
				nbVoice = readMotorolaWord(&ptr, &ptr_size);
				setPlayerRate(readMotorolaWord(&ptr, &ptr_size));
				nbFrame= readMotorolaDword(&ptr, &ptr_size);
				loopFrame = readMotorolaDword(&ptr, &ptr_size);
				nbDrum = readMotorolaWord(&ptr, &ptr_size);
				attrib = readMotorolaDword(&ptr, &ptr_size);
				pSongName = readNtString((char**)&ptr, &ptr_size);
				pSongAuthor = readNtString((char**)&ptr, &ptr_size);
				pSongComment = readNtString((char**)&ptr, &ptr_size);
				if (ptr_size < 0)
				{ 
					setLastError("File too small");
					return YMFALSE;
				}
				if (nbDrum>0)
				{
					pDrumTab=(digiDrum_t*)calloc(nbDrum, sizeof(digiDrum_t));
					for (i=0;i<(ymint)nbDrum;i++)
					{
						pDrumTab[i].size = readMotorolaWord(&ptr, &ptr_size);
						if (ptr_size < 0)
						{
							setLastError("File too small");
							goto error_out;
						}
						pDrumTab[i].repLen = pDrumTab[i].size;
						if (e_YMT2 == id)//('YMT2' == id)
						{
							pDrumTab[i].repLen = readMotorolaWord(&ptr, &ptr_size);	// repLen
							readMotorolaWord(&ptr, &ptr_size);		// flag
							if (ptr_size < 0)
							{
								setLastError("File too small");
								goto error_out;
							}
						}
						if (pDrumTab[i].repLen>pDrumTab[i].size)
						{
							pDrumTab[i].repLen = pDrumTab[i].size;
						}

						if (pDrumTab[i].size)
						{
							if (pDrumTab[i].size >= 0x80000000)
							{
								setLastError("Drumtab to big");
								goto error_out;
							}
							if (ptr_size<pDrumTab[i].size)
							{
								setLastError("File too small");
								goto error_out;
							}

							pDrumTab[i].pData = (ymu8*)malloc(pDrumTab[i].size);
							memcpy(pDrumTab[i].pData,ptr,pDrumTab[i].size);
							ptr += pDrumTab[i].size;
							ptr_size -= pDrumTab[i].size;
						}
						else
						{
							pDrumTab[i].pData = NULL;
						}
					}
				}

				ymTrackerFreqShift = 0;
				if (e_YMT2 == id)//('YMT2' == id)
				{
					ymTrackerFreqShift = (attrib>>28)&15;
					attrib &= 0x0fffffff;
					pSongType = mstrdup("YM-T2");
				}
				else
				{
					pSongType = mstrdup("YM-T1");
				}

				if ((nbVoice > MAX_VOICE) || (nbVoice < 0))
				{
					setLastError("Too many voices");
					goto error_out;
				}
				if ((nbFrame >= (ymint)(0x80000000 / (MAX_VOICE * (sizeof(ymTrackerLine_t))))) || (nbFrame < 0)) /* ymTrackerLine_t has a 2^N size */
				{
					setLastError("Too many frames");
					goto error_out;
				}
				if (ptr_size < (sizeof(ymTrackerLine_t) * nbVoice * nbFrame))
				{
					setLastError("File too small");
					goto error_out;
				}

				pDataStream = ptr;
				ymChip.setClock(ATARI_CLOCK);

				ymTrackerInit(100);		// 80% de volume maxi.
				streamInc = 16;
				setTimeControl(YMTRUE);
				pSongPlayer = mstrdup("Universal Tracker");
				break;

			default:
				setLastError("Unknown YM format !");
				return YMFALSE;
				break;
		}

		if (!deInterleave())
		{
			return YMFALSE;
		}

		return YMTRUE;
error_out:
	for (i=0;i<nbDrum;i++)
	{
		if (pDrumTab[i].pData)
			myFree((void **)&pDrumTab[i].pData);
	}
	if (nbDrum>0)
	{
		myFree((void **)&pDrumTab);
		nbDrum=0;
	}
	myFree((void **)&pSongName);
	myFree((void **)&pSongAuthor);
	myFree((void **)&pSongComment);
	myFree((void **)&pSongType); /* <- never needed, but we keep it for purity */
	myFree((void **)&pSongPlayer); /* <- never needed, but we keep it for purity */
	myFree((void **)&pMixBlock);
	myFree((void **)&pBigSampleBuffer); /* <- never needed, but we keep it for purity */
	return YMFALSE;
 }

 
ymbool	CYmMusic::checkCompilerTypes()
{
	setLastError("Basic types size are not correct (check ymTypes.h)");

	if (1 != sizeof(ymu8)) return YMFALSE;
	if (1 != sizeof(yms8)) return YMFALSE;
	if (1 != sizeof(ymchar)) return YMFALSE;

	if (2 != sizeof(ymu16)) return YMFALSE;
	if (2 != sizeof(yms16)) return YMFALSE;
	if (4 != sizeof(ymu32)) return YMFALSE;
	if (4 != sizeof(yms32)) return YMFALSE;

	if (2 != sizeof(ymsample)) return YMFALSE;

#ifdef YM_INTEGER_ONLY
	if (8 != sizeof(yms64)) return YMFALSE;
#endif

	if (sizeof(ymint) < 4) return YMFALSE;		// ymint should be at least 32bits

	setLastError("");
	return YMTRUE;
}


ymbool	CYmMusic::load(const char *fileName)
{
FILE	*in;


		stop();
		unLoad();

		if (!checkCompilerTypes())
			return YMFALSE;

		in = fopen(fileName,"rb");
		if (!in)
		{
			setLastError("File not Found");
			return YMFALSE;
		}

		//---------------------------------------------------
		// Allocation d'un buffer pour lire le fichier.
		//---------------------------------------------------
		fileSize = fileSizeGet(in);
		pBigMalloc = (unsigned char*)malloc(fileSize);
		if (!pBigMalloc)
		{
			setLastError("MALLOC Error");
			fclose(in);
			return YMFALSE;
		}

		//---------------------------------------------------
		// Chargement du fichier complet.
		//---------------------------------------------------
		if (fread(pBigMalloc,1,fileSize,in)!=(size_t)fileSize)
		{
			free(pBigMalloc);
			setLastError("File is corrupted.");
			fclose(in);
			return YMFALSE;
		}
		fclose(in);

		//---------------------------------------------------
		// Transforme les donnes en donnes valides.
		//---------------------------------------------------
		pBigMalloc = depackFile(fileSize);
		if (!pBigMalloc)
		{
			return YMFALSE;
		}

		//---------------------------------------------------
		// Lecture des donnes YM:
		//---------------------------------------------------
		if (!ymDecode())
		{
			free(pBigMalloc);
			pBigMalloc = NULL;
			return YMFALSE;
		}

		ymChip.reset();
		bMusicOk = YMTRUE;
		bPause = YMFALSE;
		return YMTRUE;
 }

ymbool	CYmMusic::loadMemory(void *pBlock,ymu32 size)
{


		stop();
		unLoad();

		if (!checkCompilerTypes())
			return YMFALSE;

		//---------------------------------------------------
		// Allocation d'un buffer pour lire le fichier.
		//---------------------------------------------------
		fileSize = size;
		pBigMalloc = (unsigned char*)malloc(fileSize);
		if (!pBigMalloc)
		{
			setLastError("MALLOC Error");
			return YMFALSE;
		}

		//---------------------------------------------------
		// Chargement du fichier complet.
		//---------------------------------------------------
		memcpy(pBigMalloc,pBlock,size);

		//---------------------------------------------------
		// Transforme les donnes en donnes valides.
		//---------------------------------------------------
		pBigMalloc = depackFile(size);
		if (!pBigMalloc)
		{
			return YMFALSE;
		}

		//---------------------------------------------------
		// Lecture des donnes YM:
		//---------------------------------------------------
		if (!ymDecode())
		{
			free(pBigMalloc);
			pBigMalloc = NULL;
			return YMFALSE;
		}

		ymChip.reset();
		bMusicOk = YMTRUE;
		bPause = YMFALSE;
		return YMTRUE;
 }

void	CYmMusic::unLoad(void)
{

		bMusicOk = YMFALSE;
		bPause = YMTRUE;
		bMusicOver = YMFALSE;
		myFree((void**)&pSongName);
		myFree((void**)&pSongAuthor);
		myFree((void**)&pSongComment);
		myFree((void**)&pSongType);
		myFree((void**)&pSongPlayer);
		myFree((void**)&pBigMalloc);
		if (nbDrum>0)
		{
			for (ymint i=0;i<nbDrum;i++)
			{
				myFree((void**)&pDrumTab[i].pData);
			}
			nbDrum = 0;
			myFree((void**)&pDrumTab);
		}
		myFree((void**)&pBigSampleBuffer);
		myFree((void**)&pMixBlock);

		myFree((void**)&m_pTimeInfo);

}

void	CYmMusic::stop(void)
{
	bPause = YMTRUE;
	currentFrame = 0;
	m_iMusicPosInMs = 0;
	m_iMusicPosAccurateSample = 0;
	mixPos = -1;
}

void	CYmMusic::play(void)
{
	bPause = YMFALSE;
}

void	CYmMusic::pause(void)
{
	bPause = YMTRUE;
}
