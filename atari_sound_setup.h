/*
 * Copyright 2023 Miro Kropacek <miro.kropacek@gmail.com>
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

#ifndef ATARI_SOUND_SETUP_H
#define ATARI_SOUND_SETUP_H

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// additional SND_EXT mode for Setmode()
#define MODE_MONO16 3
// SND_EXT bits for Soundcmd() and Sndstatus()
#define SND_SIGNED			(1<<0)
#define SND_UNSIGNED		(1<<1)
#define SND_BIG_ENDIAN		(1<<2)
#define SND_LITTLE_ENDIAN	(1<<3)
// SND_EXT and MacSound command for Soundcmd() (direct setting of the sample rate)
#define	SETSMPFREQ	7

typedef enum {
	AudioFormatSigned8,
	AudioFormatSigned16LSB,
	AudioFormatSigned16MSB,
	AudioFormatUnsigned8,
	AudioFormatUnsigned16LSB,
	AudioFormatUnsigned16MSB,

	AudioFormatCount
} AudioFormat;

typedef struct {
	uint16_t	frequency;	// in samples per second
	uint8_t		channels;	// 1: mono, 2: stereo
	AudioFormat	format;		// see AudioFormat
	uint16_t	samples;	// number of samples to process (2^N)
	uint32_t	size;		// buffer size (calculated )
} AudioSpec;

int AtariSoundSetupInitXbios(const AudioSpec* desired, AudioSpec* obtained);
int AtariSoundSetupDeinitXbios(void);

////////////////////////////////////////////////////////////////////////////////

/*
 * Find out the speed of external clock
 * even for a dual external clock !!!
 * (FDI supported)
 *
 * Copyright STGHOST/SECTOR ONE 1999
 */
static inline int externalClockTest(void) {
	register int ret __asm__("d0");

	__asm__ volatile(
		"	.equ	noextlow,35\n"	// [1-35] U [42-50] 49 kHz (type 0)
		"	.equ	noexthi,42\n"	// [36-38] 48 kHz (type 2)
		"	.equ	bilimit,38\n"	// [39-41] 44.1kHz (type 1)

		"	move.w	#0x2500,%%sr\n"
		"	lea		0xffff8901.w,%%a2\n"
		"	lea		0x4ba.w,%%a3\n"
		"	moveq	#2,%%d0\n"
		"	moveq	#50,%%d1\n"
		"	add.l	(%%a3),%%d0\n"
		"	add.l	%%d0,%%d1\n"
		"tstart:\n"
		"	cmp.l	(%%a3),%%d0\n"	// time to start ?
		"	bne.s	tstart\n"
		"	move.b	#1,(%%a2)\n"	// start replay
		"	nop\n"
		"tloop:\n"
		"	tst.b	(%%a2)\n"		// end of buffer ?
		"	beq.s	tstop\n"
		"	cmp.l	(%%a3),%%d1\n"	// time limit reached ?
		"	bne.s	tloop\n"
		"	clr.b	(%%a2)\n"		// turn off replay
		"tstop:\n"
		"	move.l	(%%a3),%%d2\n"	// stop time
		"	sub.l	%%d0,%%d2\n"	// timelength

		// now compute the clock type from the timelength
		"	moveq	#0,%0\n"		// type=int49 (default)
		"	cmp.w	#noextlow,%%d2\n"
		"	bls.s	typ0\n"
		"	cmp.w	#noexthi,%%d2\n"
		"	bhs.s	typ0\n"
		"	cmp.w	#bilimit,%%d2\n"
		"	bls.s	typ2\n"
		"	moveq	#1,%0\n"		// type=ext44
		"	bra.s	typ0\n"
		"typ2:\n"
		"	moveq	#2,%0\n"		// type=ext48
		"typ0:\n"
		"	move.w	#0x2300,%%sr\n"
		: "=r"(ret)	// outputs
		: // inputs
		: __CLOBBER_RETURN("d0") "d1", "d2", "a2", "a3", "cc" AND_MEMORY
	);

	return ret;
}

static inline int detectFalconClocks(int *extClock1, int *extClock2) {
	const int TEST_BUFSIZE = 8820;
	char* bufs;
	char* bufe;

	bufs = (char*)Mxalloc(TEST_BUFSIZE, MX_STRAM);
	if(!bufs)
		return 0;

	bufe = bufs + TEST_BUFSIZE;
	memset(bufs, 0, TEST_BUFSIZE);

	Sndstatus(SND_RESET);
	Devconnect(DMAPLAY, DAC, CLKEXT, CLK50K, NO_SHAKE);
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
	*extClock2 = Supexec(externalClockTest);

	/*
	 * bit #0: 0 (external clock 1)
	 * bit #1: 1 (set mode to play in FDI)
	 * bit #2: 0 (no FDI reset)
	 */
	Gpio(GPIO_WRITE, 0x02);
	*extClock1 = Supexec(externalClockTest);

	Mfree(bufs);

	return 1;
}

static inline int detectFormat(
	const int formatsAvailable[AudioFormatCount],
	const AudioSpec* desired,
	AudioSpec* obtained) {
	if (formatsAvailable[desired->format]) {
		obtained->format = desired->format;
		return 1;
	}

	int found = 0;

	// prefer the same bit-depth & endianness
	for (int i = 0; !found && i < AudioFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	// "enumeration value 'AudioFormatCount' not handled in switch"
		switch (desired->format) {
			case AudioFormatSigned8:
			case AudioFormatUnsigned8:
				if (i == AudioFormatUnsigned8 || i == AudioFormatSigned8) {
					obtained->format = (AudioFormat)i;
					found = 1;
				}
				break;

			case AudioFormatSigned16LSB:
			case AudioFormatUnsigned16LSB:
				if (i == AudioFormatUnsigned16LSB || i == AudioFormatSigned16LSB) {
					obtained->format = (AudioFormat)i;
					found = 1;
				}
				break;

			case AudioFormatSigned16MSB:
			case AudioFormatUnsigned16MSB:
				if (i == AudioFormatUnsigned16MSB || i == AudioFormatSigned16MSB) {
					obtained->format = (AudioFormat)i;
					found = 1;
				}
				break;
		}
#pragma GCC diagnostic pop
	}

	// prefer the same sign
	for (int i = 0; !found && i < AudioFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	// "enumeration value 'AudioFormatCount' not handled in switch"
		switch (desired->format) {
			case AudioFormatSigned8:
			case AudioFormatSigned16LSB:
			case AudioFormatSigned16MSB:
				if (i == AudioFormatSigned16MSB || i == AudioFormatSigned16LSB) {
					obtained->format = (AudioFormat)i;
					found = 1;
				}
				break;

			case AudioFormatUnsigned8:
			case AudioFormatUnsigned16LSB:
			case AudioFormatUnsigned16MSB:
				if (i == AudioFormatUnsigned16MSB || i == AudioFormatUnsigned16LSB) {
					obtained->format = (AudioFormat)i;
					found = 1;
				}
				break;
		}
#pragma GCC diagnostic pop
	}

	/*
	 * this handles:
	 * 	- desired 8-bit, available 16-bit (non-matching sign)
	 * 	- desired 16-bit, available 16-bit (non-matching sign & endianness)
	 */
	for (int i = 0; !found && i < AudioFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

		// take the first available 16-bit format
		if (i != AudioFormatSigned8 && i != AudioFormatUnsigned8) {
			obtained->format = (AudioFormat)i;
			found = 1;
		}
	}

	if (!found) {
		// prefer the same sign while downgrading to 8-bit
		if (formatsAvailable[AudioFormatSigned8]
			&& (desired->format == AudioFormatSigned16LSB
				|| desired->format == AudioFormatSigned16MSB)) {
			obtained->format = AudioFormatSigned8;
			found = 1;
		} else if (formatsAvailable[AudioFormatUnsigned8]
			&& (desired->format == AudioFormatUnsigned16LSB
				|| desired->format == AudioFormatUnsigned16MSB)) {
			obtained->format = AudioFormatUnsigned8;
			found = 1;
		}

		for (int i = 0; !found && i < AudioFormatCount; i++) {
			// take the first available (8-bit) format
			if (formatsAvailable[i]) {
				obtained->format = (AudioFormat)i;
				found = 1;
			}
		}
	}

	return found;
}

enum {
	MCH_ST = 0,
	MCH_STE,
	MCH_TT_OR_HADES,
	MCH_FALCON,
	MCH_MILAN,
	MCH_ARANYM
};
static long mch;

static int locked;
static int oldGpio;
static int oldLtAtten;
static int oldRtAtten;
static int oldLtGain;
static int oldRtGain;
static int oldAdderIn;
static int oldAdcInput;
static int oldPrescale;

int AtariSoundSetupInitXbios(const AudioSpec* desired, AudioSpec* obtained) {
	if (!desired || !obtained)
		return 0;

	if (desired->frequency == 0 || desired->frequency > 64000
		|| desired->channels == 0 || desired->channels > 2
		|| desired->format >= AudioFormatCount
		|| desired->samples == 0)
		return 0;

	// this tests presence of an XBIOS, too
	if (Locksnd() != 1)
		return 0;

	locked = 1;
	oldLtAtten = Soundcmd(LTATTEN, SND_INQUIRE);
	oldRtAtten = Soundcmd(RTATTEN, SND_INQUIRE);
	oldLtGain = Soundcmd(LTGAIN, SND_INQUIRE);
	oldRtGain = Soundcmd(RTGAIN, SND_INQUIRE);
	oldAdderIn = Soundcmd(ADDERIN, SND_INQUIRE);
	oldAdcInput = Soundcmd(ADCINPUT, SND_INQUIRE);
	oldPrescale = Soundcmd(SETPRESCALE, SND_INQUIRE);
	// we could save also SND_EXT Soundcmd() modes here but that's perhaps overkill

	int formatsAvailable[AudioFormatCount] = { 0 };
	int has8bitStereo = 1;
	int has16bitMono = 0;
	int hasFreeFrequency = 0;
	int extClock1 = 0;
	int extClock2 = 0;

	mch = MCH_ST<<16;
	Getcookie(C__MCH, &mch);
	mch >>= 16;

	if (mch == MCH_FALCON) {
		oldGpio = Gpio(GPIO_READ, SND_INQUIRE);	// 'data' is ignored

		if (!detectFalconClocks(&extClock1, &extClock2)) {
			AtariSoundSetupDeinitXbios();
			return 0;
		}
	}

	long snd = 0;
	Getcookie(C__SND, &snd);

	long mcsn = 0;
	if (Getcookie(C_McSn, &mcsn) == C_FOUND) {
		struct McSnCookie {
			uint16_t vers;		// version in BCD
			uint16_t size;		// struct size
			uint16_t play;		// playback availability
			uint16_t record;	// recording availability
			uint16_t dsp;		// DSP availability
			uint16_t pint;		// end-of-frame interrupt by playback availability
			uint16_t rint;		// end-of-frame interrupt by recording availability

			uint32_t res1;		// external clock for Devconnect(x,x,1,x,x)
			uint32_t res2;
			uint32_t res3;
			uint32_t res4;
		} __attribute__((packed));

		// check whether 8-bit stereo is available
		struct McSnCookie* mcsnCookie = (struct McSnCookie*)mcsn;
		has8bitStereo = (mcsnCookie->play == 1 || mcsnCookie->play == 2);	// STE/TT or Falcon

		// If Falcon frequencies are available
		if (mcsnCookie->play == 2) {
			// MacSound offers an emulated external 44.1 kHz clock
			if (extClock1 == 0 && extClock2 == 0)
				extClock1 = 1;

			hasFreeFrequency = 1;
		}

		// X-Sound doesn't set _SND (MacSound does)
		if (!snd)
			snd = SND_PSG | SND_8BIT;
	}

	if (!snd) {
		AtariSoundSetupDeinitXbios();
		return 0;
	}

	long stfa = 0;
	if (Getcookie(C_STFA, &stfa) == C_FOUND) {
		// see http://removers.free.fr/softs/stfa.php#STFA
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
		} __attribute__((packed));

		// check whether SND_16BIT isn't emulated
		struct STFA_control* stfaControl = (struct STFA_control*)stfa;
		if (stfaControl->version >= 0x0200 && !stfaControl->old_bit_2_of_cookie_snd) {
			snd &= ~SND_16BIT;
		}

		// also, don't attempt to emulate any frequency not available on STE/TT
	}

	if (snd & SND_EXT) {
		has16bitMono = 1;
		hasFreeFrequency = 1;
		if (extClock1 == 0 && extClock2 == 0) {
			// this is not really used (thanks to hasFreeFrequency) but may come in handy in the future
			extClock1 = 1;	// 22.5792 MHz (max 44100 Hz)
			extClock2 = 2;	// 24.576 MHz (max 48000 Hz); unsupported in GSXB
		}

		unsigned short bitDepth = Sndstatus(2);

		if (bitDepth & 0x01) {
			// 8-bit
			unsigned short formats = Sndstatus(8);

			if (formats & SND_SIGNED)
				formatsAvailable[AudioFormatSigned8]   = 1;

			if (formats & SND_UNSIGNED)
				formatsAvailable[AudioFormatUnsigned8] = 1;
		}

		if (bitDepth & 0x02) {
			// 16-bit
			unsigned short formats = Sndstatus(9);

			if (formats & SND_SIGNED) {
				if (formats & SND_BIG_ENDIAN)
					formatsAvailable[AudioFormatSigned16MSB] = 1;
				if (formats & SND_LITTLE_ENDIAN)
					formatsAvailable[AudioFormatSigned16LSB] = 1;
			}

			if (formats & SND_UNSIGNED) {
				if (formats & SND_BIG_ENDIAN)
					formatsAvailable[AudioFormatUnsigned16MSB] = 1;
				if (formats & SND_LITTLE_ENDIAN)
					formatsAvailable[AudioFormatUnsigned16LSB] = 1;
			}
		}
	} else {
		// by default assume just signed 8-bit and/or 16-bit big endian
		formatsAvailable[AudioFormatSigned8]     = (snd & SND_8BIT) != 0;
		formatsAvailable[AudioFormatSigned16MSB] = (snd & SND_16BIT) != 0;
	}

	if (!detectFormat(formatsAvailable, desired, obtained)) {
		AtariSoundSetupDeinitXbios();
		return 0;
	}

	// reset connection matrix (and other settings)
	Sndstatus(SND_RESET);

	if (hasFreeFrequency) {
		Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);
		obtained->frequency = Soundcmd(SETSMPFREQ, desired->frequency);
	} else {
		struct FrequencySetting {
			int frequency;
			int clk;			// clock for Devconnect()
			int prescale;		// prescale for Devconnect()
			int prescaleOld;	// prescale for Soundcmd(SETPRESCALE), -1 if prescale != CLKOLD
			int clkType;		// 0: internal, 1: external 44.1 kHz, 2: external 48 kHz
		};

		static const struct FrequencySetting frequencies[] = {
			// STE/TT
			{ 50066, CLK25M, CLKOLD,  PRE160, 0 },
			{ 25033, CLK25M, CLKOLD,  PRE320, 0 },
			{ 12517, CLK25M, CLKOLD,  PRE640, 0 },
			{  6258, CLK25M, CLKOLD, PRE1280, 0 },
			// Falcon
			{ 49170, CLK25M, CLK50K, -1, 0 },
			{ 32780, CLK25M, CLK33K, -1, 0 },
			{ 24585, CLK25M, CLK25K, -1, 0 },
			{ 19668, CLK25M, CLK20K, -1, 0 },
			{ 16390, CLK25M, CLK16K, -1, 0 },
			{ 12292, CLK25M, CLK12K, -1, 0 },
			{  9834, CLK25M, CLK10K, -1, 0 },
			{  8195, CLK25M, CLK8K,  -1, 0 },
			// CD
			{ 44100, CLKEXT, CLK50K, -1, 1 },
			{ 29400, CLKEXT, CLK33K, -1, 1 },
			{ 22050, CLKEXT, CLK25K, -1, 1 },
			{ 17640, CLKEXT, CLK20K, -1, 1 },
			{ 14700, CLKEXT, CLK16K, -1, 1 },
			{ 11025, CLKEXT, CLK12K, -1, 1 },
			{  8820, CLKEXT, CLK10K, -1, 1 },
			{  7350, CLKEXT, CLK8K,  -1, 1 },
			// DAT
			{ 48000, CLKEXT, CLK50K, -1, 2 },
			{ 32000, CLKEXT, CLK33K, -1, 2 },
			{ 24000, CLKEXT, CLK25K, -1, 2 },
			{ 19200, CLKEXT, CLK20K, -1, 2 },
			{ 16000, CLKEXT, CLK16K, -1, 2 },
			{ 12000, CLKEXT, CLK12K, -1, 2 },
			{  9600, CLKEXT, CLK10K, -1, 2 },
			{  8000, CLKEXT, CLK8K,  -1, 2 }
		};
		static const int frequenciesCount = sizeof(frequencies) / sizeof(frequencies[0]);

		struct FrequencySetting frequencySetting = { 0, 0, 0, 0, 0 };
		for (int i = 0; i < frequenciesCount; i++) {
			// assume that SND_16BIT implies availability of Falcon frequencies
			if (frequencies[i].prescale != CLKOLD && !(snd & SND_16BIT))
				continue;

			// skip external clock frequencies if not present
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
						frequencySetting.prescale = 15;	// "6146 Hz" (illegal on Falcon)"
						break;
					}
					frequencySetting.prescaleOld = -1;
				}
			}
		}

		if (!frequencySetting.frequency) {
			AtariSoundSetupDeinitXbios();
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
		Devconnect(DMAPLAY, DAC, frequencySetting.clk, frequencySetting.prescale, NO_SHAKE);
		if (mch == MCH_FALCON && frequencySetting.clk == CLKEXT) {
			/*
			 * if DAC is using CLKEXT, ADC has to follow the suit
			 *
			 * NOTE: the value set for the ADC in 0xffff8930 is in fact
			 *       0b11, i.e. not CLKEXT (0b01); it is an undocumented
			 *       hardware quirk.
			 */
			Devconnect(ADC, 0x00, CLKEXT, frequencySetting.prescale, NO_SHAKE);
		}
		if (frequencySetting.prescale == CLKOLD)
			Soundcmd(SETPRESCALE, frequencySetting.prescaleOld);
	}

	if (desired->channels == 1
		&& obtained->format != AudioFormatSigned8
		&& obtained->format != AudioFormatUnsigned8
		&& !has16bitMono) {
		// Falcon lacks 16-bit mono
		obtained->channels = 2;
	} else if (desired->channels == 2
		&& (obtained->format == AudioFormatSigned8 || obtained->format == AudioFormatUnsigned8)
		&& !has8bitStereo) {
		// ST emulation lacks 8-bit stereo
		obtained->channels = 1;
	} else {
		obtained->channels = desired->channels;
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	// "enumeration value 'AudioFormatCount' not handled in switch"
	switch (obtained->format) {
		case AudioFormatSigned8:
		case AudioFormatUnsigned8:
			if (obtained->channels == 1)
				Setmode(MODE_MONO);
			else
				Setmode(MODE_STEREO8);
			break;

		case AudioFormatSigned16LSB:
		case AudioFormatSigned16MSB:
		case AudioFormatUnsigned16LSB:
		case AudioFormatUnsigned16MSB:
			if (obtained->channels == 1)
				Setmode(MODE_MONO16);
			else
				Setmode(MODE_STEREO16);
			break;
	}

	if (snd & SND_EXT) {
		switch (obtained->format) {
			case AudioFormatSigned8:
				Soundcmd(8, SND_SIGNED);
				break;
			case AudioFormatUnsigned8:
				Soundcmd(8, SND_UNSIGNED);
				break;
			case AudioFormatSigned16LSB:
				Soundcmd(9, SND_SIGNED | SND_LITTLE_ENDIAN);
				break;
			case AudioFormatSigned16MSB:
				Soundcmd(9, SND_SIGNED | SND_BIG_ENDIAN);
				break;
			case AudioFormatUnsigned16LSB:
				Soundcmd(9, SND_UNSIGNED | SND_LITTLE_ENDIAN);
				break;
			case AudioFormatUnsigned16MSB:
				Soundcmd(9, SND_UNSIGNED | SND_BIG_ENDIAN);
				break;
		}
	}
#pragma GCC diagnostic pop

	Soundcmd(ADDERIN, MATIN);	// set matrix to the adder

	// (lag in ms) = (samples / frequency) * 1000
	obtained->samples = desired->samples;
	while (obtained->samples * 16 > obtained->frequency * 2)
		obtained->samples >>= 1;

	obtained->size = obtained->samples * obtained->channels;
	if (obtained->format != AudioFormatSigned8
		&& obtained->format != AudioFormatUnsigned8) {
		// 16-bit samples
		obtained->size *= 2;
	}

	return 1;
}

int AtariSoundSetupDeinitXbios(void) {
	if (locked) {
		locked = 0;

		// for cases when playback is still running
		Buffoper(0x00);
		Sndstatus(SND_RESET);

		if (mch == MCH_FALCON)
			Gpio(GPIO_WRITE, oldGpio);

		Soundcmd(LTATTEN, oldLtAtten);
		Soundcmd(RTATTEN, oldRtAtten);
		Soundcmd(LTGAIN, oldLtGain);
		Soundcmd(RTGAIN, oldRtGain);
		Soundcmd(ADDERIN, oldAdderIn);
		Soundcmd(ADCINPUT, oldAdcInput);
		Soundcmd(SETPRESCALE, oldPrescale);

		Unlocksnd();
		return 1;
	}

	return 0;
}

#endif
