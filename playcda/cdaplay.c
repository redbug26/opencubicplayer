/* OpenCP Module Player
 * copyright (c) '94-'10 Niklas Beisert <nbeisert@physik.tu-muenchen.de>
 *
 * Audio CD player
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
 *  -ss040907   Stian Skjelstad <stian@nixia.no>
 *    -complete rewrite for linux
 */

#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "types.h"
#include "boot/psetting.h"
#include "cdaudio.h"
#include "cpiface/cpiface.h"
#include "dev/deviplay.h"
#include "dev/devisamp.h"
#include "dev/player.h"
#include "dev/plrasm.h"
#include "dev/ringbuffer.h"
#include "dev/sampler.h"
#include "filesel/cdrom.h"
#include "filesel/filesystem.h"
#include "stuff/imsrtns.h"
#include "stuff/poll.h"

static int inpause; /* digital playback */
/* INPUT buffer (ioctl buffer-ring)       OUTPUT buffer (includes pitch adjustment)     BUFFER in the devp device
      64 frames (853 ms)                      0.5 seconds                                    ...
   rip_ioctl_buf                           cdbuf : cdbuflen (bytes)                     plrbuf : buflen (samples)
      ^rip_pcm_write (bytes; write-head)                                                    ^ bufpos (samples; write-head)
   ^rip_pcm_read (bytes; read-head)
                                                 : cdbufrate (made from speed)                 : stereo
                                                                                               : bit16
                                                                                               : signedout
                                                                                               : reversestereo
*/


/* devp buffer zone */
static uint32_t bufpos; /* devp write head location */
static uint32_t buflen; /* devp buffer-size in samples */
static void *plrbuf; /* the devp buffer */
static int stereo; /* boolean */
static int bit16; /* boolean */
static int signedout; /* boolean */
static int reversestereo; /* boolean */

/* cdIdler dumping locations */
static int16_t *cdbuf=NULL; /* the buffer */
static uint32_t cdbuflen;  /* total buffer size */
static struct ringbuffer_t *cdbufpos = 0;
static uint32_t cdbuffpos; /* read fine-pos.. when cdbufrate has a fraction */
static uint32_t cdbufrate; /* re-sampling rate.. fixed point 0x10000 => 1.0 */
static volatile int clipbusy;
static int speed;
static int looped;
static int donotloop;

/* cdIdle dumping location */
static int16_t *buf16=NULL;

static int lba_start, lba_stop, lba_next;
struct ocpfilehandle_t *fh;

#ifdef CD_FRAMESIZE_RAW
#undef CD_FRAMESIZE_RAW
#endif
#define CD_FRAMESIZE_RAW 2352

#define BUFFER_SLOTS 64
#define REQUEST_SLOTS 16
/* TODO, add circular rip buffer */
static unsigned char rip_ioctl_buf[CD_FRAMESIZE_RAW*BUFFER_SLOTS];
static uint32_t rip_sectors[BUFFER_SLOTS];
static unsigned int rip_pcm_read;
static unsigned int rip_pcm_write;

#warning TODO pan, voll, volr, srnd
static uint8_t pan = 64, srnd = 0;
static unsigned long voll = 256, volr = 256;

#define PANPROC \
do { \
	float _rs = rs, _ls = ls; \
	if(pan==-64) \
	{ \
		float t=_ls; \
		_ls = _rs; \
		_rs = t; \
	} else if(pan==64) \
	{ \
	} else if(pan==0) \
		_rs=_ls=(_rs+_ls) / 2.0; \
	else if(pan<0) \
	{ \
		_ls = _ls / (-pan/-64.0+2.0) + _rs*(64.0+pan)/128.0; \
		_rs = _rs / (-pan/-64.0+2.0) + _ls*(64.0+pan)/128.0; \
	} else if(pan<64) \
	{ \
		_ls = _ls / (pan/-64.0+2.0) + _rs*(64.0-pan)/128.0; \
		_rs = _rs / (pan/-64.0+2.0) + _ls*(64.0-pan)/128.0; \
	} \
	rs = _rs * volr / 256.0; \
	ls = _ls * voll / 256.0; \
	if (srnd) \
	{ \
		ls ^= 0xffff; \
	} \
} while(0)

static void cdIdler(void)
{
	while (1)
	{
		size_t read;
		int pos1, pos2;
		int length1, length2;
		int emptyframes;
		int temp;
		int rip_pcm_left;

	/* First we keep the rip_ioctl_buf full with data from the CD */
		if (lba_next == lba_stop)
		{
			if (!donotloop)
			{
				looped |= 1;
				return;
			} else {
				looped &= ~1;
				lba_next = lba_start;
			}
		}

		if (rip_pcm_read == rip_pcm_write)
		{
			emptyframes = REQUEST_SLOTS + 1;
		} else {
			emptyframes = (CD_FRAMESIZE_RAW*BUFFER_SLOTS + rip_pcm_read - rip_pcm_write) / CD_FRAMESIZE_RAW % BUFFER_SLOTS;
		}

		if (emptyframes > REQUEST_SLOTS)
		{
			struct ioctl_cdrom_readaudio_request_t req;

			emptyframes = REQUEST_SLOTS;

			/* check against track end */
			temp = lba_stop - lba_next;
			if (emptyframes > temp)
			{
				emptyframes = temp;
			}

			/* check against buffer-end */
			if (emptyframes * CD_FRAMESIZE_RAW + rip_pcm_write > (CD_FRAMESIZE_RAW * BUFFER_SLOTS))
			{
				emptyframes = (CD_FRAMESIZE_RAW * BUFFER_SLOTS - rip_pcm_write) / CD_FRAMESIZE_RAW;
			}

			req.lba_addr = lba_next;
			req.lba_count = emptyframes;
			req.ptr = rip_ioctl_buf + rip_pcm_write;
			assert ((rip_pcm_write + emptyframes * CD_FRAMESIZE_RAW) <= sizeof (rip_ioctl_buf));
			if (fh->ioctl (fh, IOCTL_CDROM_READAUDIO, &req))
			{
				return;
			}
			for (temp = 0; temp < req.lba_count; temp++)
			{
				rip_sectors[(rip_pcm_write%CD_FRAMESIZE_RAW)+temp] = lba_next + temp;
			}
			rip_pcm_write += req.lba_count * CD_FRAMESIZE_RAW;
			lba_next += req.lba_count;
			if (rip_pcm_write >= CD_FRAMESIZE_RAW*BUFFER_SLOTS)
			{
				rip_pcm_write = 0;
			}
		}
		if (rip_pcm_write > rip_pcm_read)
		{
			rip_pcm_left = rip_pcm_write - rip_pcm_read;
			assert ((rip_pcm_read + rip_pcm_left) <= sizeof (rip_ioctl_buf));

		} else {
			rip_pcm_left = CD_FRAMESIZE_RAW*BUFFER_SLOTS - rip_pcm_read;
			assert ((rip_pcm_read + rip_pcm_left) <= sizeof (rip_ioctl_buf));
		}

	/* Secondly we top up the cdbufpos, temporary inbetween buffer, can probably be removed
	 *
	 * transfer value from previous buffer is rip_pcm_read, is either ends at the head buffer or the buffer-wrap
	 */
#warning remove cdbufpos, and take advantage of rip_sectors[] for time to UI
#warning we need to update IOCTL to be async (and threaded for real hardware)

		ringbuffer_get_head_samples (cdbufpos, &pos1, &length1, &pos2, &length2);

		if (!length1)
		{
			return;
		}
		read = length1<<2;

		if (read > rip_pcm_left)
			read = rip_pcm_left;

		if (!read)
		{
			break;
		}

		assert ((rip_pcm_read + read) <= sizeof (rip_ioctl_buf));
		memcpy (cdbuf + (pos1<<1), rip_ioctl_buf + rip_pcm_read, read);
		rip_pcm_read += read;
		if (rip_pcm_read >= CD_FRAMESIZE_RAW * BUFFER_SLOTS)
		{
			rip_pcm_read = 0;
		}

		ringbuffer_head_add_bytes (cdbufpos, read);
	}
}

void __attribute__ ((visibility ("internal"))) cdIdle(void)
{
	uint32_t bufplayed;
	uint32_t bufdelta;
	uint32_t pass2;

	if (clipbusy++)
	{
		clipbusy--;
		return;
	}

	/* Where is our devp reading head? */
	bufplayed=plrGetBufPos()>>(stereo+bit16);
	bufdelta=(buflen+bufplayed-bufpos)%buflen;

	/* No delta on the devp? */
	if (!bufdelta)
	{
		clipbusy--;
		if (plrIdle)
			plrIdle();
		return;
	}

	/* fill up our buffers */
	cdIdler();

	if (inpause)
	{ /* If we are in pause, we fill buffer with the correct type of zeroes */
		if ((bufpos+bufdelta)>buflen)
			pass2=bufpos+bufdelta-buflen;
		else
			pass2=0;
		if (bit16)
		{
			plrClearBuf((uint16_t *)plrbuf+(bufpos<<stereo), (bufdelta-pass2)<<stereo, signedout);
			if (pass2)
				plrClearBuf((uint16_t *)plrbuf, pass2<<stereo, signedout);
		} else {
			plrClearBuf(buf16, bufdelta<<stereo, signedout);
			plr16to8((uint8_t *)plrbuf+(bufpos<<stereo), (uint16_t *)buf16, (bufdelta-pass2)<<stereo);
			if (pass2)
				plr16to8((uint8_t *)plrbuf, (uint16_t *)buf16+((bufdelta-pass2)<<stereo), pass2<<stereo);
		}
		bufpos+=bufdelta;
		if (bufpos>=buflen)
			bufpos-=buflen;
	} else {
		int pos1, length1, pos2, length2;
		int i;
		int buf16_filled = 0;

		/* how much data is available.. we are using a ringbuffer, so we might receive two fragments */
		ringbuffer_get_tail_samples (cdbufpos, &pos1, &length1, &pos2, &length2);

		/* are the speed 1:1, if so filling up buf16 is very easy */
		if (cdbufrate==0x10000)
		{
			int16_t *t = buf16;

			if (bufdelta>(length1+length2))
			{
				bufdelta=(length1+length2);
				looped |= 2;
			} else {
				looped &= ~2;
			}

			for (buf16_filled=0; buf16_filled<bufdelta; buf16_filled++)
			{
				int16_t rs, ls;

				if (!length1)
				{
					pos1 = pos2;
					length1 = length2;
					pos2 = 0;
					length2 = 0;
				}
				assert (length1);

				rs = cdbuf[pos1<<1];
				ls = cdbuf[(pos1<<1) + 1];

				PANPROC;

				*(t++) = rs;
				*(t++) = ls;

				pos1++;
				length1--;
			}

			ringbuffer_tail_consume_samples (cdbufpos, buf16_filled); /* add this rate buf16_filled == tail_used */
		} else {
			/* We are going to perform cubic interpolation of rate conversion... this bit is tricky */
			unsigned int accumulated_progress = 0;

			looped &= ~2;

			for (buf16_filled=0; buf16_filled<bufdelta; buf16_filled++)
			{
				uint32_t wpm1, wp0, wp1, wp2;
				int32_t rc0, rc1, rc2, rc3, rvm1,rv1,rv2;
				int32_t lc0, lc1, lc2, lc3, lvm1,lv1,lv2;
				unsigned int progress;
				int16_t rs, ls;

				/* will the interpolation overflow? */
				if ((length1+length2) <= 3)
				{
					looped |= 2;
					break;
				}
				/* will we overflow the wavebuf if we advance? */
				if ((length1+length2) < ((cdbufrate+cdbuffpos)>>16))
				{
					looped |= 2;
					break;
				}

				switch (length1) /* if we are close to the wrap between buffer segment 1 and 2, len1 will grow down to a small number */
				{
					case 1:
						wpm1 = pos1;
						wp0  = pos2;
						wp1  = pos2+1;
						wp2  = pos2+2;
						break;
					case 2:
						wpm1 = pos1;
						wp0  = pos1+1;
						wp1  = pos2;
						wp2  = pos2+1;
						break;
					case 3:
						wpm1 = pos1;
						wp0  = pos1+1;
						wp1  = pos1+2;
						wp2  = pos2;
						break;
					default:
						wpm1 = pos1;
						wp0  = pos1+1;
						wp1  = pos1+2;
						wp2  = pos1+3;
						break;
				}


				rvm1 = (uint16_t)cdbuf[(wpm1<<1)+0]^0x8000; /* we temporary need data to be unsigned - hence the ^0x8000 */
				lvm1 = (uint16_t)cdbuf[(wpm1<<1)+1]^0x8000;
				 rc0 = (uint16_t)cdbuf[(wp0<<1)+0]^0x8000;
				 lc0 = (uint16_t)cdbuf[(wp0<<1)+1]^0x8000;
				 rv1 = (uint16_t)cdbuf[(wp1<<1)+0]^0x8000;
				 lv1 = (uint16_t)cdbuf[(wp1<<1)+1]^0x8000;
				 rv2 = (uint16_t)cdbuf[(wp2<<1)+0]^0x8000;
				 lv2 = (uint16_t)cdbuf[(wp2<<1)+1]^0x8000;

				rc1 = rv1-rvm1;
				rc2 = 2*rvm1-2*rc0+rv1-rv2;
				rc3 = rc0-rvm1-rv1+rv2;
				rc3 =  imulshr16(rc3,cdbuffpos);
				rc3 += rc2;
				rc3 =  imulshr16(rc3,cdbuffpos);
				rc3 += rc1;
				rc3 =  imulshr16(rc3,cdbuffpos);
				rc3 += rc0;
				if (rc3<0)
					rc3=0;
				if (rc3>65535)
					rc3=65535;

				lc1 = lv1-lvm1;
				lc2 = 2*lvm1-2*lc0+lv1-lv2;
				lc3 = lc0-lvm1-lv1+lv2;
				lc3 =  imulshr16(lc3,cdbuffpos);
				lc3 += lc2;
				lc3 =  imulshr16(lc3,cdbuffpos);
				lc3 += lc1;
				lc3 =  imulshr16(lc3,cdbuffpos);
				lc3 += lc0;
				if (lc3<0)
					lc3=0;
				if (lc3>65535)
					lc3=65535;

				rs = rc3 ^ 0x8000;
				ls = lc3 ^ 0x8000;

				PANPROC;

				buf16[(buf16_filled<<1)+0] = rs;
				buf16[(buf16_filled<<1)+1] = ls;

				cdbuffpos+=cdbufrate;
				progress = cdbuffpos>>16;
				cdbuffpos &= 0xffff;

				accumulated_progress += progress;

				/* did we wrap? if so, progress up to the wrapping point */
				if (progress >= length1)
				{
					progress -= length1;
					pos1 = pos2;
					length1 = length2;
					pos2 = 0;
					length2 = 0;
				}
				if (progress)
				{
					pos1 += progress;
					length1 -= progress;
				}
			}
			ringbuffer_tail_consume_samples (cdbufpos, accumulated_progress);
		}

		bufdelta=buf16_filled;

		if ((bufpos+bufdelta)>buflen)
			pass2=bufpos+bufdelta-buflen;
		else
			pass2=0;
		bufdelta-=pass2;

		if (bit16)
		{
			if (stereo)
			{
				int16_t *p=(int16_t *)plrbuf+2*bufpos;
				int16_t *b=buf16;
				if (signedout)
				{
					for (i=0; i<bufdelta; i++)
					{
						p[0]=b[0];
						p[1]=b[1];
						p+=2;
						b+=2;
					}
					p=(int16_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=b[0];
						p[1]=b[1];
						p+=2;
						b+=2;
					}
				} else {
					for (i=0; i<bufdelta; i++)
					{
						p[0]=b[0]^0x8000;
						p[1]=b[1]^0x8000;
						p+=2;
						b+=2;
					}
					p=(int16_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=b[0]^0x8000;
						p[1]=b[1]^0x8000;
						p+=2;
						b+=2;
					}
				}
			} else {
				int16_t *p=(int16_t *)plrbuf+bufpos;
				int16_t *b=buf16;
				if (signedout)
				{
					for (i=0; i<bufdelta; i++)
					{
						p[0]=b[0];
						p++;
						b++;
					}
					p=(int16_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=b[0];
						p++;
						b++;
					}
				} else {
					for (i=0; i<bufdelta; i++)
					{
						p[0]=b[0]^0x8000;
						p++;
						b++;
					}
					p=(int16_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=b[0]^0x8000;
						p++;
						b++;
					}
				}
			}
		} else {
			if (stereo)
			{
				uint8_t *p=(uint8_t *)plrbuf+2*bufpos;
				uint8_t *b=(uint8_t *)buf16;
				if (signedout)
				{
					for (i=0; i<bufdelta; i++)
					{
						p[0]=b[1];
						p[1]=b[3];
						p+=2;
						b+=4;
					}
					p=(uint8_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=b[1];
						p[1]=b[3];
						p+=2;
						b+=4;
					}
				} else {
					for (i=0; i<bufdelta; i++)
					{
						p[0]=b[1]^0x80;
						p[1]=b[3]^0x80;
						p+=2;
						b+=4;
					}
					p=(uint8_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=b[1]^0x80;
						p[1]=b[3]^0x80;
						p+=2;
						b+=4;
					}
				}
			} else {
				uint8_t *p=(uint8_t *)plrbuf+bufpos;
				int16_t *b=buf16;
				if (signedout)
				{
					for (i=0; i<bufdelta; i++)
					{
						p[0]=(b[0]+b[1])>>9;
						p++;
						b+=2;
					}
					p=(uint8_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=(b[0]+b[1])>>9;
						p++;
						b+=2;
					}
				} else {
					for (i=0; i<bufdelta; i++)
					{
						p[0]=((b[0]+b[1])>>9)^0x80;
						p++;
						b+=2;
					}
					p=(uint8_t *)plrbuf;
					for (i=0; i<pass2; i++)
					{
						p[0]=((b[0]+b[1])>>9)^0x80;
						p++;
						b+=2;
					}
				}
			}
		}
		bufpos+=buf16_filled;
		if (bufpos>=buflen)
			bufpos-=buflen;
	}

	plrAdvanceTo(bufpos<<(stereo+bit16));

	if (plrIdle)
		plrIdle();

	clipbusy--;
}

void __attribute__ ((visibility ("internal"))) cdSetSpeed (unsigned short sp)
{

	if (sp<32)
		sp=32;

	speed=sp;

	cdbufrate=imuldiv(256*sp, 44100, plrRate);
}

void __attribute__ ((visibility ("internal"))) cdPause (void)
{
	inpause=1;
}

void __attribute__ ((visibility ("internal"))) cdClose (void)
{
	inpause=1;
	pollClose();
	plrStop();
	if (buf16)
	{
		free(buf16);
		buf16=NULL;
	}

	if (cdbuf)
	{
		free(cdbuf);
		cdbuf=NULL;
	}
	if (cdbufpos)
	{
		ringbuffer_free (cdbufpos);
		cdbufpos = 0;
	}
	if (fh)
	{
		fh->unref (fh);
		fh = 0;
	}
}

void __attribute__ ((visibility ("internal"))) cdUnpause (void)
{
	inpause=0;
}

#if 0
unsigned short __attribute__ ((visibility ("internal"))) cdGetTracks (unsigned long *starts, unsigned char *first, unsigned short maxtracks)
{
	int min=0, max=0, i;
	struct cdrom_tochdr tochdr;
	struct cdrom_tocentry tocentry;

	*first=0;
	if (!ioctl(fd, CDROMREADTOCHDR, &tochdr))
	{
		if ((min=tochdr.cdth_trk0)<0)
			min=0;
		max=tochdr.cdth_trk1;
		if (max>maxtracks)
			max=maxtracks;
		for (i=min;i<=max;i++)
		{
			tocentry.cdte_track=i;
			tocentry.cdte_format= CDROM_LBA;
			if (!ioctl(fd, CDROMREADTOCENTRY, &tocentry))
				starts[i-min]=tocentry.cdte_addr.lba;
			else {
				perror("cdaplay: ioctl(fd, CDROMREADTOCENTRY, &tocentry)");
				max=i-1;
			}
		}
		tocentry.cdte_track=CDROM_LEADOUT;
		tocentry.cdte_format= CDROM_LBA;
		if (!ioctl(fd, CDROMREADTOCENTRY, &tocentry))
			starts[max+1-min]=tocentry.cdte_addr.lba;
		else {
			perror("cdaplay: ioctl(fd, CDROMREADTOCENTRY, &tocentry)");
			max-=1;
		}
	} else
		perror("cdaplay: ioctl(fd, CDROMREADTOCHDR, &tochdr)");
	if (max<0)
		min=max=0;
	*first=min;
	return max-min;
}
#endif

void __attribute__ ((visibility ("internal"))) cdJump (unsigned long start)
{
	if (start < lba_start) start = lba_start;
	if (start > lba_stop) start = lba_stop - 1;

	lba_next = start;

	rip_pcm_read = 0;
	rip_pcm_write = 0;
}

int __attribute__ ((visibility ("internal"))) cdOpen (unsigned long start, unsigned long len, struct ocpfilehandle_t *file)
{
	inpause = 0;

	lba_next = lba_start = start;
	lba_stop = start + len;

	if (fh)
	{
		cdClose ();
	}
	fh = file;
	fh->ref (fh);

	clipbusy = 0;
	rip_pcm_read = 0;
	rip_pcm_write = 0;


	plGetMasterSample=plrGetMasterSample;
	plGetRealMasterVolume=plrGetRealMasterVolume;

	plrSetOptions(44100, (PLR_SIGNEDOUT|PLR_16BIT)|PLR_STEREO);
	stereo=!!(plrOpt&PLR_STEREO);
	bit16=!!(plrOpt&PLR_16BIT);
	signedout=!!(plrOpt&PLR_SIGNEDOUT);
	reversestereo=!!(plrOpt&PLR_REVERSESTEREO);

	if (!plrOpenPlayer(&plrbuf, &buflen, plrBufSize * plrRate / 1000, file))
	{
		return -1;
	}

	if (!(buf16=malloc(sizeof(uint16_t) * buflen * 2 /* stereo */)))
	{
		plrClosePlayer ();
		cdbuf = NULL;
		return -1;
	}
	bufpos=0;

	cdbuflen= plrRate * 4 / 2; /* stereo + 16bit, 0.5 seconds */
	if (!(cdbuf = malloc(cdbuflen)))
	{
		plrClosePlayer ();
		free (buf16);
		buf16 = 0;
		return -1;
	}

	cdbufpos = ringbuffer_new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED, cdbuflen / 4);
	if (!cdbufpos)
	{
		free(cdbuf);
		cdbuf = 0;
		plrClosePlayer ();
		free (buf16);
		buf16 = 0;
		return 0;
	}

	cdSetSpeed(256);
	looped = 0;
	donotloop = 1;

	if (!pollInit(cdIdle))
	{
		ringbuffer_free (cdbufpos);
		cdbufpos = 0;
		free(buf16);
		buf16=NULL;
		plrClosePlayer();
		free(cdbuf);
		cdbuf=NULL;
		return -1;
	}

	return 0;
}

void __attribute__ ((visibility ("internal"))) cdGetStatus (struct cdStat *stat)
{
	stat->error=0;
	stat->paused=inpause;
	stat->position=lba_next; /* TODO, needs buffer feedback */
	stat->speed=(inpause?0:speed);
	stat->looped=(lba_next==lba_stop)&&(looped==3)/*&&(rip_pcm_read == rip_pcm_write)*/;
}

void __attribute__ ((visibility ("internal"))) cdSetLoop (int loop)
{
	donotloop=!loop;
}
