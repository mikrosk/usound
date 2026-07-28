/*
 * Copyright 2023-2026 Miro Kropacek <miro.kropacek@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef USOUND_H
#define USOUND_H

#define USOUND_VERSION 2

#include <mint/cookie.h>
#include <mint/errno.h>
#include <mint/falcon.h>
#include <mint/osbind.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* additional SND_EXT mode for Setmode() */
#ifndef MODE_MONO16
#define MODE_MONO16 3
#endif
/* SND_EXT bits for Soundcmd() and Sndstatus() */
#ifndef SND_FORMATSIGNED
#define SND_FORMATSIGNED		(1<<0)
#define SND_FORMATUNSIGNED		(1<<1)
#define SND_FORMATBIGENDIAN		(1<<2)
#define SND_FORMATLITTLEENDIAN	(1<<3)
#endif

/* SND_EXT and MacSound command for Soundcmd() (direct setting of the sample rate) */
#ifndef SETSMPFREQ
#define	SETSMPFREQ	7
#endif

typedef enum {
	USoundFormatSigned8,
	USoundFormatSigned16LSB,
	USoundFormatSigned16MSB,
	USoundFormatUnsigned8,
	USoundFormatUnsigned16LSB,
	USoundFormatUnsigned16MSB,

	USoundFormatCount
} USoundFormat;

typedef struct {
	uint16_t		frequency;	/* in samples per second */
	uint8_t			channels;	/* 1: mono, 2: stereo */
	USoundFormat	format;		/* see USoundFormat */
	uint16_t		samples;	/* number of samples to process (2^N) */
	uint32_t		size;		/* buffer size (calculated) */
} USoundSpec;

typedef struct {
	int locked;
	int oldGpio;
	int oldLtAtten;
	int oldRtAtten;
	int oldLtGain;
	int oldRtGain;
	int oldAdderIn;
	int oldAdcInput;
	int oldPrescale;
} USoundContext;

#ifndef USOUND_DEF
#ifdef __GNUC__
/* a translation unit doesn't have to use both of them */
#define USOUND_DEF	static __attribute__((__unused__))
#else
#define USOUND_DEF	static
#endif
#endif	/* USOUND_DEF */

USOUND_DEF int USoundInitXbios(const USoundSpec* desired, USoundSpec* obtained, USoundContext* context);
USOUND_DEF int USoundDeinitXbios(USoundContext* context);

/******************************************************************************/

#ifndef __mcoldfire__
/* Set src_pathclk to the external clock, return recv_pathclk */
static long USoundSetSrcExtPathclk(void) {
	register long recvPathclk __asm__("d0");

	__asm__ volatile(
		"	and.w	#0x0FFF,0xFFFF8930:w\n"
		/* Done only in TOS 4.04 */
		"	or.w	#0x6000,0xFFFF8930:w\n"
		"	move.w	0xFFFF8932:w,%%d0\n"

		: "=r"(recvPathclk)	/* outputs */
		: /* inputs */
		: __CLOBBER_RETURN("d0") "cc" AND_MEMORY
	);

	return recvPathclk;
}

static void USoundFalconDevconnectExtClk(short src, short dst, short pre, short proto) {
	const long recvPathclk = Supexec(USoundSetSrcExtPathclk);

	__asm__ volatile(
		/* Devconnect() */
		"	move.w	%5,%%sp@-\n"
		"	move.w	%4,%%sp@-\n"
		"	move.w	%3,%%sp@-\n"
		"	move.w	%2,%%sp@-\n"
		"	move.w	%1,%%sp@-\n"
		"	move.w	#139,%%sp@-\n"
		/* Devconnect() on TOS 4.0x needs content of recv_pathclk in d2 due to a bug */
		"	move.w	%0,%%d2\n"
		"	trap	#14\n"
		"	lea		(12,%%sp),%%sp\n"

		: /* outputs */
		: "r"(recvPathclk), "ri"(src), "ri"(dst), "ri"(CLKEXT), "ri"(pre), "ri"(proto)	/* inputs */
		: "d0", "d1", "d2", "a0", "a1", "a2", "cc" AND_MEMORY
	);

	/* Return value of Devconnect() is broken on Falcon */
}

/*
 * Find out the speed of external clock
 * even for a dual external clock !!!
 * (Steinberg's FDI and SoundPool's SPDIF are also supported)
 *
 * Copyright STGHOST/SECTOR ONE 1999
 */
static long USoundExternalClockTest(void) {
	register int ret __asm__("d0");

	__asm__ volatile(
		"	move.w	#0x2500,%%sr\n"
		"	lea		0xffff8901.w,%%a1\n"
		"	lea		0x4ba.w,%%a0\n"
		"	moveq	#2,%%d2\n"
		"	moveq	#50,%%d1\n"
		"	add.l	(%%a0),%%d2\n"
		"	add.l	%%d2,%%d1\n"
		"tstart%=:\n"
		"	cmp.l	(%%a0),%%d2\n"	/* time to start ? */
		"	bne.s	tstart%=\n"
		"	move.b	#1,(%%a1)\n"	/* SB_PLA_ENA; start replay */
		"	nop\n"
		"tloop%=:\n"
		"	tst.b	(%%a1)\n"		/* end of buffer ? */
		"	beq.s	tstop%=\n"
		"	cmp.l	(%%a0),%%d1\n"	/* time limit reached ? */
		"	bne.s	tloop%=\n"
		"	clr.b	(%%a1)\n"		/* turn off replay */
		"tstop%=:\n"
		"	move.l	(%%a0),%%d0\n"	/* stop time */
		"	sub.l	%%d2,%%d0\n"	/* timelength */
		"	move.w	#0x2300,%%sr\n"

		: "=r"(ret)	/* outputs */
		: /* inputs */
		: __CLOBBER_RETURN("d0") "d1", "d2", "a0", "a1", "cc" AND_MEMORY
	);

	return ret;
}

static int USoundClockType(long ticks)
{
	if (ticks <= 35)	/* [1-35] U [42-50] 49 kHz (type 0), 179 ms = 35 ticks */
		return 0;
	if (ticks >= 42)
		return 0;
	if (ticks <= 38)	/* [36-38] 48 kHz (type 2), 183 ms = 36 ticks */
		return 2;
	return 1;			/* [39-41] 44.1kHz (type 1), 200 ms = 40 ticks */
}

static int USoundDetectFalconClocks(int *extClock1, int *extClock2) {
	const int TEST_BUFSIZE = 8820;
	char* bufs;
	char* bufe;

	bufs = (char*)Mxalloc(TEST_BUFSIZE, MX_STRAM);
	if((long) bufs == -ENOSYS)
		bufs = (char*)Malloc(TEST_BUFSIZE);
	if(!bufs)
		return 0;

	bufe = bufs + TEST_BUFSIZE;
	memset(bufs, 0, TEST_BUFSIZE);

	Sndstatus(SND_RESET);
	USoundFalconDevconnectExtClk(DMAPLAY, DAC, CLK50K, NO_SHAKE);
	Setmode(MODE_MONO);
	Soundcmd(ADDERIN, MATIN);
	Setbuffer(SR_PLAY, bufs, bufe);

	/*
	 * bit #0: 1 (enable clock selection for newclock)
	 * bit #1: 1 (enable direction control for FDI)
	 * bit #2: 1 (enable reset control for FDI)
	 */
	Gpio(GPIO_SET, 0x07);

	/*
	 * bit #0: 1 (external clock 2)
	 * bit #1: 1 (set mode to play in FDI)
	 * bit #2: 0 (no FDI reset)
	 */
	Gpio(GPIO_WRITE, 0x03);
	*extClock2 = USoundClockType(Supexec(USoundExternalClockTest));

	/*
	 * bit #0: 0 (external clock 1)
	 * bit #1: 1 (set mode to play in FDI)
	 * bit #2: 0 (no FDI reset)
	 */
	Gpio(GPIO_WRITE, 0x02);
	*extClock1 = USoundClockType(Supexec(USoundExternalClockTest));

	Mfree(bufs);

	return 1;
}
#endif	/* !__mcoldfire__ */

static int USoundDetectFormat(
	const int formatsAvailable[USoundFormatCount],
	const USoundSpec* desired,
	USoundSpec* obtained) {
	int found;
	int i;

	if (formatsAvailable[desired->format]) {
		obtained->format = desired->format;
		return 1;
	}

	found = 0;

	/* prefer the same bit-depth & endianness */
	for (i = 0; !found && i < USoundFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

		switch (desired->format) {
			case USoundFormatSigned8:
			case USoundFormatUnsigned8:
				if (i == USoundFormatUnsigned8 || i == USoundFormatSigned8) {
					obtained->format = (USoundFormat)i;
					found = 1;
				}
				break;

			case USoundFormatSigned16LSB:
			case USoundFormatUnsigned16LSB:
				if (i == USoundFormatUnsigned16LSB || i == USoundFormatSigned16LSB) {
					obtained->format = (USoundFormat)i;
					found = 1;
				}
				break;

			case USoundFormatSigned16MSB:
			case USoundFormatUnsigned16MSB:
				if (i == USoundFormatUnsigned16MSB || i == USoundFormatSigned16MSB) {
					obtained->format = (USoundFormat)i;
					found = 1;
				}
				break;
			case USoundFormatCount:
				break;
		}
	}

	/* prefer the same sign */
	for (i = 0; !found && i < USoundFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

		switch (desired->format) {
			case USoundFormatSigned8:
			case USoundFormatSigned16LSB:
			case USoundFormatSigned16MSB:
				if (i == USoundFormatSigned16MSB || i == USoundFormatSigned16LSB) {
					obtained->format = (USoundFormat)i;
					found = 1;
				}
				break;

			case USoundFormatUnsigned8:
			case USoundFormatUnsigned16LSB:
			case USoundFormatUnsigned16MSB:
				if (i == USoundFormatUnsigned16MSB || i == USoundFormatUnsigned16LSB) {
					obtained->format = (USoundFormat)i;
					found = 1;
				}
				break;
			case USoundFormatCount:
				break;
		}
	}

	/*
	 * this handles:
	 * 	- desired 8-bit, available 16-bit (non-matching sign)
	 * 	- desired 16-bit, available 16-bit (non-matching sign & endianness)
	 */
	for (i = 0; !found && i < USoundFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

		/* take the first available 16-bit format */
		if (i != USoundFormatSigned8 && i != USoundFormatUnsigned8) {
			obtained->format = (USoundFormat)i;
			found = 1;
		}
	}

	if (!found) {
		/* prefer the same sign while downgrading to 8-bit */
		if (formatsAvailable[USoundFormatSigned8]
			&& (desired->format == USoundFormatSigned16LSB
				|| desired->format == USoundFormatSigned16MSB)) {
			obtained->format = USoundFormatSigned8;
			found = 1;
		} else if (formatsAvailable[USoundFormatUnsigned8]
			&& (desired->format == USoundFormatUnsigned16LSB
				|| desired->format == USoundFormatUnsigned16MSB)) {
			obtained->format = USoundFormatUnsigned8;
			found = 1;
		}

		for (i = 0; !found && i < USoundFormatCount; i++) {
			/* take the first available (8-bit) format */
			if (formatsAvailable[i]) {
				obtained->format = (USoundFormat)i;
				found = 1;
			}
		}
	}

	return found;
}

USOUND_DEF int USoundInitXbios(const USoundSpec* desired, USoundSpec* obtained, USoundContext* context) {
	enum {
		MCH_ST = 0,
		MCH_STE,
		MCH_TT_OR_HADES,
		MCH_FALCON,
		MCH_MILAN,
		MCH_ARANYM
	};
	long mch;
	long snd;
	long mcsn = 0;
	long stfa = 0;
	int formatsAvailable[USoundFormatCount] = { 0 };
	int has8bitStereo = 1;
	int has16bitMono = 0;
	int hasFreeFrequency = 0;
	int extClock1 = 0;
	int extClock2 = 0;

	if (!desired || !obtained || !context)
		return 0;

	memset(context, 0, sizeof(*context));

	if (desired->frequency == 0 || desired->frequency > 64000
		|| desired->channels == 0 || desired->channels > 2
		|| desired->format >= USoundFormatCount
		|| desired->samples == 0)
		return 0;

	/* this tests presence of an XBIOS, too */
	if (Locksnd() != 1)
		return 0;

	context->locked = 1;
	context->oldLtAtten = Soundcmd(LTATTEN, SND_INQUIRE);
	context->oldRtAtten = Soundcmd(RTATTEN, SND_INQUIRE);
	context->oldLtGain = Soundcmd(LTGAIN, SND_INQUIRE);
	context->oldRtGain = Soundcmd(RTGAIN, SND_INQUIRE);
	context->oldAdderIn = Soundcmd(ADDERIN, SND_INQUIRE);
	context->oldAdcInput = Soundcmd(ADCINPUT, SND_INQUIRE);
	context->oldPrescale = Soundcmd(SETPRESCALE, SND_INQUIRE);
	context->oldGpio = Gpio(GPIO_READ, SND_INQUIRE);	/* 'data' is ignored */
	/* we could save also SND_EXT Soundcmd() modes here but that's perhaps overkill */

	mch = MCH_ST<<16;
	Getcookie(C__MCH, &mch);
	mch >>= 16;

#ifndef __mcoldfire__
	if (mch == MCH_FALCON /*|| mch == MCH_ARANYM*/) {	/* hangs in Aranym */
		if (!USoundDetectFalconClocks(&extClock1, &extClock2)) {
			USoundDeinitXbios(context);
			return 0;
		}
	}
#endif

	snd = SND_PSG;
	Getcookie(C__SND, &snd);

	if (Getcookie(C_McSn, &mcsn) == C_FOUND) {
		struct McSnCookie {
			uint16_t vers;		/* version in BCD */
			uint16_t size;		/* struct size */
			uint16_t play;		/* playback availability */
			uint16_t record;	/* recording availability */
			uint16_t dsp;		/* DSP availability */
			uint16_t pint;		/* end-of-frame interrupt by playback availability */
			uint16_t rint;		/* end-of-frame interrupt by recording availability */

			uint32_t res1;		/* external clock for Devconnect(x,x,1,x,x) */
			uint32_t res2;
			uint32_t res3;
			uint32_t res4;
		};

		/* check whether 8-bit stereo is available */
		struct McSnCookie* mcsnCookie = (struct McSnCookie*)mcsn;
		has8bitStereo = (mcsnCookie->play == 1 || mcsnCookie->play == 2);	/* STE/TT or Falcon */

		/* If Falcon frequencies are available */
		if (mcsnCookie->play == 2) {
			/* MacSound offers an emulated external 44.1 kHz clock */
			if (extClock1 == 0 && extClock2 == 0)
				extClock1 = 1;

			hasFreeFrequency = 1;
		}

		/* X-Sound doesn't set _SND (MacSound does) */
		snd |= SND_8BIT;
	}

	if (!(snd & (SND_8BIT | SND_16BIT))) {
		USoundDeinitXbios(context);
		return 0;
	}

	if (Getcookie(C_STFA, &stfa) == C_FOUND) {
		/* see http://removers.free.fr/softs/stfa.php#STFA */
		struct STFA_control {
			uint16_t sound_enable;
			uint16_t sound_control;
			uint16_t sound_output;
			uint32_t sound_start;
			uint32_t sound_current;
			uint32_t sound_end;
			uint16_t version;
			uint32_t old_vbl;
			uint32_t old_timerA;
			uint32_t old_mfp_status;
			uint32_t stfa_vbl;
			uint32_t drivers_list;
			uint32_t play_stop;
			uint16_t timer_a_setting;
			uint32_t set_frequency;
			uint16_t frequency_treshold;
			uint32_t custom_freq_table;
			int16_t stfa_on_off;
			uint32_t new_drivers_list;
			uint32_t old_bit_2_of_cookie_snd;
			uint32_t it;
		};

		/* check whether SND_16BIT isn't emulated */
		struct STFA_control* stfaControl = (struct STFA_control*)stfa;
		if (stfaControl->version >= 0x0200 && !stfaControl->old_bit_2_of_cookie_snd) {
			snd &= ~SND_16BIT;
		}

		/* also, don't attempt to emulate any frequency not available on STE/TT */
	}

	if (snd & SND_EXT) {
		unsigned short bitDepth;

#ifndef __mcoldfire__
		has16bitMono = 1;
#endif
		hasFreeFrequency = 1;
		if (extClock1 == 0 && extClock2 == 0) {
			/* this is not really used (thanks to hasFreeFrequency) but may come in handy in the future */
			extClock1 = 1;	/* 22.5792 MHz (max 44100 Hz) */
			extClock2 = 2;	/* 24.576 MHz (max 48000 Hz); unsupported in GSXB */
		}

		bitDepth = Sndstatus(2);

		if (bitDepth & 0x01) {
			/* 8-bit */
			unsigned short formats = Sndstatus(8);

			if (formats & SND_FORMATSIGNED)
				formatsAvailable[USoundFormatSigned8]   = 1;

			if (formats & SND_FORMATUNSIGNED)
				formatsAvailable[USoundFormatUnsigned8] = 1;
		}

		if (bitDepth & 0x02) {
			/* 16-bit */
			unsigned short formats = Sndstatus(9);

			if (formats & SND_FORMATSIGNED) {
				if (formats & SND_FORMATBIGENDIAN)
					formatsAvailable[USoundFormatSigned16MSB] = 1;
				if (formats & SND_FORMATLITTLEENDIAN)
					formatsAvailable[USoundFormatSigned16LSB] = 1;
			}

			if (formats & SND_FORMATUNSIGNED) {
				if (formats & SND_FORMATBIGENDIAN)
					formatsAvailable[USoundFormatUnsigned16MSB] = 1;
				if (formats & SND_FORMATLITTLEENDIAN)
					formatsAvailable[USoundFormatUnsigned16LSB] = 1;
			}
		}
	} else {
		/* by default assume just signed 8-bit and/or 16-bit big endian */
		formatsAvailable[USoundFormatSigned8]     = (snd & SND_8BIT) != 0;
		formatsAvailable[USoundFormatSigned16MSB] = (snd & SND_16BIT) != 0;
	}

	if (!USoundDetectFormat(formatsAvailable, desired, obtained)) {
		USoundDeinitXbios(context);
		return 0;
	}

	/* reset connection matrix (and other settings) */
	Sndstatus(SND_RESET);

	if (hasFreeFrequency) {
		Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);
		obtained->frequency = Soundcmd(SETSMPFREQ, desired->frequency);
	} else {
		struct FrequencySetting {
			int frequency;
			int clk;			/* clock for Devconnect() */
			int prescale;		/* prescale for Devconnect() */
			int prescaleOld;	/* prescale for Soundcmd(SETPRESCALE), -1 if prescale != CLKOLD */
			int clkType;		/* 0: internal, 1: external 44.1 kHz, 2: external 48 kHz */
		};

		static const struct FrequencySetting frequencies[] = {
			/* STE/TT */
			{ 50066, CLK25M, CLKOLD,  PRE160, 0 },
			{ 25033, CLK25M, CLKOLD,  PRE320, 0 },
			{ 12517, CLK25M, CLKOLD,  PRE640, 0 },
			{  6258, CLK25M, CLKOLD, PRE1280, 0 },
			/* Falcon */
			{ 49170, CLK25M, CLK50K, -1, 0 },
			{ 32780, CLK25M, CLK33K, -1, 0 },
			{ 24585, CLK25M, CLK25K, -1, 0 },
			{ 19668, CLK25M, CLK20K, -1, 0 },
			{ 16390, CLK25M, CLK16K, -1, 0 },
			{ 12292, CLK25M, CLK12K, -1, 0 },
			{  9834, CLK25M, CLK10K, -1, 0 },
			{  8195, CLK25M, CLK8K,  -1, 0 },
			/* CD */
			{ 44100, CLKEXT, CLK50K, -1, 1 },
			{ 29400, CLKEXT, CLK33K, -1, 1 },
			{ 22050, CLKEXT, CLK25K, -1, 1 },
			{ 17640, CLKEXT, CLK20K, -1, 1 },
			{ 14700, CLKEXT, CLK16K, -1, 1 },
			{ 11025, CLKEXT, CLK12K, -1, 1 },
			{  8820, CLKEXT, CLK10K, -1, 1 },
			{  7350, CLKEXT, CLK8K,  -1, 1 },
			/* DAT */
			{ 48000, CLKEXT, CLK50K, -1, 2 },
			{ 32000, CLKEXT, CLK33K, -1, 2 },
			{ 24000, CLKEXT, CLK25K, -1, 2 },
			{ 19200, CLKEXT, CLK20K, -1, 2 },
			{ 16000, CLKEXT, CLK16K, -1, 2 },
			{ 12000, CLKEXT, CLK12K, -1, 2 },
			{  9600, CLKEXT, CLK10K, -1, 2 },
			{  8000, CLKEXT, CLK8K,  -1, 2 }
		};
		struct FrequencySetting frequencySetting = { 0, 0, 0, 0, 0 };
		int i;

		for (i = 0; i < (int)(sizeof(frequencies) / sizeof(frequencies[0])); i++) {
			/* assume that SND_16BIT implies availability of Falcon frequencies */
			if (frequencies[i].prescale != CLKOLD && !(snd & SND_16BIT))
				continue;

			/* skip 6258 Hz if on Falcon */
			if ((mch == MCH_FALCON || mch == MCH_ARANYM) && frequencies[i].prescale == CLKOLD && frequencies[i].prescaleOld == PRE1280)
				continue;

			/* skip external clock frequencies if not present */
			if (frequencies[i].clkType != 0 && frequencies[i].clkType != extClock1 && frequencies[i].clkType != extClock2)
				continue;

			if (frequencySetting.frequency == 0
				|| abs(frequencies[i].frequency - desired->frequency) < abs(frequencySetting.frequency - desired->frequency)) {
				frequencySetting = frequencies[i];

				if (mcsn && frequencySetting.prescale == CLKOLD && !(snd & SND_16BIT)) {
					/*
					 * hack for X-SOUND which doesn't understand SETPRESCALE
					 * and yet happily pretends that Falcon frequencies are
					 * STE/TT ones
					 */
					switch (frequencySetting.prescaleOld) {
					case PRE160:
						frequencySetting.prescale = CLK50K;
						break;
					case PRE320:
						frequencySetting.prescale = CLK25K;
						break;
					case PRE640:
						frequencySetting.prescale = CLK12K;
						break;
					case PRE1280:
						frequencySetting.prescale = 15;	/* "6146 Hz" (illegal on Falcon)" */
						break;
					}
					frequencySetting.prescaleOld = -1;
				}
			}
		}

		if (!frequencySetting.frequency) {
			USoundDeinitXbios(context);
			return 0;
		}

		obtained->frequency = frequencySetting.frequency;

		if (frequencySetting.clkType != 0) {
			if (frequencySetting.clkType == extClock1) {
				Gpio(GPIO_WRITE, 0x02);
			} else if (frequencySetting.clkType == extClock2) {
				Gpio(GPIO_WRITE, 0x03);
			}
		}
#ifndef __mcoldfire__
		if ((mch == MCH_FALCON || mch == MCH_ARANYM) && frequencySetting.clk == CLKEXT) {
			USoundFalconDevconnectExtClk(DMAPLAY, DAC, frequencySetting.prescale, NO_SHAKE);
		} else
#endif
		{
			Devconnect(DMAPLAY, DAC, frequencySetting.clk, frequencySetting.prescale, NO_SHAKE);
		}
		if (frequencySetting.prescale == CLKOLD)
			Soundcmd(SETPRESCALE, frequencySetting.prescaleOld);
	}

	if (desired->channels == 1
		&& obtained->format != USoundFormatSigned8
		&& obtained->format != USoundFormatUnsigned8
		&& !has16bitMono) {
		/* Falcon and FireBee lack 16-bit mono */
		obtained->channels = 2;
	} else if (desired->channels == 2
		&& (obtained->format == USoundFormatSigned8 || obtained->format == USoundFormatUnsigned8)
		&& !has8bitStereo) {
		/* ST emulation lacks 8-bit stereo */
		obtained->channels = 1;
	} else {
		obtained->channels = desired->channels;
	}

	switch (obtained->format) {
		case USoundFormatSigned8:
		case USoundFormatUnsigned8:
			if (obtained->channels == 1)
				Setmode(MODE_MONO);
			else
				Setmode(MODE_STEREO8);
			break;

		case USoundFormatSigned16LSB:
		case USoundFormatSigned16MSB:
		case USoundFormatUnsigned16LSB:
		case USoundFormatUnsigned16MSB:
			if (obtained->channels == 1)
				Setmode(MODE_MONO16);
			else
				Setmode(MODE_STEREO16);
			break;
		case USoundFormatCount:
			break;
	}

	if (snd & SND_EXT) {
		switch (obtained->format) {
			case USoundFormatSigned8:
				Soundcmd(8, SND_FORMATSIGNED);
				break;
			case USoundFormatUnsigned8:
				Soundcmd(8, SND_FORMATUNSIGNED);
				break;
			case USoundFormatSigned16LSB:
				Soundcmd(9, SND_FORMATSIGNED | SND_FORMATLITTLEENDIAN);
				break;
			case USoundFormatSigned16MSB:
				Soundcmd(9, SND_FORMATSIGNED | SND_FORMATBIGENDIAN);
				break;
			case USoundFormatUnsigned16LSB:
				Soundcmd(9, SND_FORMATUNSIGNED | SND_FORMATLITTLEENDIAN);
				break;
			case USoundFormatUnsigned16MSB:
				Soundcmd(9, SND_FORMATUNSIGNED | SND_FORMATBIGENDIAN);
				break;
			case USoundFormatCount:
				break;
		}
	}

	Soundcmd(ADDERIN, MATIN);	/* set matrix to the adder */

	/* (lag in ms) = (samples / frequency) * 1000 */
	obtained->samples = desired->samples;
	while (obtained->samples * 16 > obtained->frequency * 2)
		obtained->samples >>= 1;

	obtained->size = obtained->samples * obtained->channels;
	if (obtained->format != USoundFormatSigned8
		&& obtained->format != USoundFormatUnsigned8) {
		/* 16-bit samples */
		obtained->size *= 2;
	}

	return 1;
}

USOUND_DEF int USoundDeinitXbios(USoundContext* context) {
	if (context && context->locked) {
		context->locked = 0;

		/* for cases when playback is still running */
		Buffoper(0x00);
		Sndstatus(SND_RESET);

		Gpio(GPIO_WRITE, context->oldGpio);
		Soundcmd(LTATTEN, context->oldLtAtten);
		Soundcmd(RTATTEN, context->oldRtAtten);
		Soundcmd(LTGAIN, context->oldLtGain);
		Soundcmd(RTGAIN, context->oldRtGain);
		Soundcmd(ADDERIN, context->oldAdderIn);
		Soundcmd(ADCINPUT, context->oldAdcInput);
		Soundcmd(SETPRESCALE, context->oldPrescale);

		Unlocksnd();
		return 1;
	}

	return 0;
}

#endif
