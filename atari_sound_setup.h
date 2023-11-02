/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef ATARI_SOUND_SETUP_H
#define ATARI_SOUND_SETUP_H

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>
#include <stdint.h>
#include <stdlib.h>

/* additional SND_EXT mode for Setmode() */
#define MODE_MONO16 3
/* SND_EXT bits for Soundcmd() and Sndstatus() */
#define SND_SIGNED			(1<<0)
#define SND_UNSIGNED		(1<<1)
#define SND_BIG_ENDIAN		(1<<2)
#define SND_LITTLE_ENDIAN	(1<<3)

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
	uint16_t	frequency;	/* in samples per second */
	uint8_t		channels;	/* 1: mono, 2: stereo */
	AudioFormat	format;		/* see AudioFormat */
	uint16_t	samples;	/* number of samples to process (2^N) */
	uint16_t	size;		/* buffer size (calculated ) */
} AudioSpec;

static int detectFormat(
	const int formatsAvailable[AudioFormatCount],
	const AudioSpec* desired,
	AudioSpec* obtained) {
	if (formatsAvailable[desired->format]) {
		obtained->format = desired->format;
		return 1;
	}

	int found = 0;

	/* prefer the same bit-depth & endianness */
	for (int i = 0; !found && i < AudioFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	/* "enumeration value 'AudioFormatCount' not handled in switch" */
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

	/* prefer the same sign */
	for (int i = 0; !found && i < AudioFormatCount; i++) {
		if (!formatsAvailable[i])
			continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	/* "enumeration value 'AudioFormatCount' not handled in switch" */
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

		/* take the first available 16-bit format */
		if (i != AudioFormatSigned8 && i != AudioFormatUnsigned8) {
			obtained->format = (AudioFormat)i;
			found = 1;
		}
	}

	if (!found) {
		/* prefer the same sign while downgrading to 8-bit */
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
			/* take the first available (8-bit) format */
			if (formatsAvailable[i]) {
				obtained->format = (AudioFormat)i;
				found = 1;
			}
		}
	}

	return found;
}

int AtariSoundSetupInitXbios(const AudioSpec* desired, AudioSpec* obtained) {
	if (!desired || !obtained)
		return 0;

	if (desired->frequency == 0
		|| desired->channels == 0 || desired->channels > 2
		|| desired->format >= AudioFormatCount
		|| desired->samples == 0)
		return 0;

	/* this tests presence of an XBIOS, too */
	if (Locksnd() != 1)
		return 0;

	int formatsAvailable[AudioFormatCount] = { 0 };
	int has8bitStereo = 1;
	int has16bitMono = 0;
	int extClock1 = 0;
	int extClock2 = 0;

	enum {
		MCH_ST = 0,
		MCH_STE,
		MCH_TT_OR_HADES,
		MCH_FALCON,
		MCH_MILAN,
		MCH_ARANYM
	};
	long mch = MCH_ST<<16;
	Getcookie(C__MCH, &mch);
	mch >>= 16;

	if (mch == MCH_FALCON) {
		// TODO: clkprobe(&extClock1, &extClock2);
	}

	long snd = 0;
	Getcookie(C__SND, &snd);

	long mcsn = 0;
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
		} __attribute__((packed));

		/* check whether 8-bit stereo is available */
		struct McSnCookie* mcsnCookie = (struct McSnCookie*)mcsn;
		has8bitStereo = (mcsnCookie->play == 1 || mcsnCookie->play == 2);	/* STE/TT or Falcon */

		/* MacSound offers an emulated external 44.1 kHz clock */
		if (mcsnCookie->play == 2 && extClock1 == 0 && extClock2 == 0)
			extClock1 = 1;

		/* X-Sound doesn't set _SND (MacSound does) */
		if (!snd)
			snd = SND_PSG | SND_8BIT;
	}

	if (!snd)
		return 0;

	long stfa = 0;
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
		} __attribute__((packed));

		/* check whether SND_16BIT isn't emulated */
		struct STFA_control* stfaControl = (struct STFA_control*)stfa;
		if (stfaControl->version >= 0x0200 && !stfaControl->old_bit_2_of_cookie_snd) {
			snd &= ~SND_16BIT;
		}
	}

	if (snd & SND_EXT) {
		/* this should be safe to assume... */
		has16bitMono = 1;

		unsigned short bitDepth = Sndstatus(2);

		if (bitDepth & 0x01) {
			/* 8-bit */
			unsigned short formats = Sndstatus(8);

			if (formats & SND_SIGNED)
				formatsAvailable[AudioFormatSigned8]   = 1;

			if (formats & SND_UNSIGNED)
				formatsAvailable[AudioFormatUnsigned8] = 1;
		}

		if (bitDepth & 0x02) {
			/* 16-bit */
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
		/* by default assume just signed 8-bit and/or 16-bit big endian */
		formatsAvailable[AudioFormatSigned8]     = (snd & SND_8BIT) != 0;
		formatsAvailable[AudioFormatSigned16MSB] = (snd & SND_16BIT) != 0;
	}

	if (!detectFormat(formatsAvailable, desired, obtained))
		return 0;

	/* reset connection matrix (and other settings) */
	Sndstatus(SND_RESET);

	struct FrequencySetting {
		int frequency;
		int clk;		/* clock for Devconnect() */
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
	static const int frequenciesCount = sizeof(frequencies) / sizeof(frequencies[0]);

	struct FrequencySetting frequencySetting = { 0, 0, 0, 0, 0 };
	for (int i = 0; i < frequenciesCount; i++) {
		/* assume that SND_16BIT implies availability of Falcon frequencies */
		if (frequencies[i].prescale != CLKOLD && !(snd & SND_16BIT))
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
			}
		}
	}

	if (!frequencySetting.frequency)
		return 0;

	obtained->frequency = frequencySetting.frequency;

	Devconnect(DMAPLAY, DAC, frequencySetting.clk, frequencySetting.prescale, NO_SHAKE);
	if (frequencySetting.prescale == CLKOLD)
		Soundcmd(SETPRESCALE, frequencySetting.prescaleOld);

	if (desired->channels == 1
		&& obtained->format != AudioFormatSigned8
		&& obtained->format != AudioFormatUnsigned8
		&& !has16bitMono) {
		/* Falcon lacks 16-bit mono */
		obtained->channels = 2;
	} else if (desired->channels == 2
		&& (obtained->format == AudioFormatSigned8 || obtained->format == AudioFormatUnsigned8)
		&& !has8bitStereo) {
		/* ST emulation lacks 8-bit stereo */
		obtained->channels = 1;
	} else {
		obtained->channels = desired->channels;
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	/* "enumeration value 'AudioFormatCount' not handled in switch" */
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

	Soundcmd(ADDERIN, MATIN);

	obtained->samples = desired->samples;
	obtained->size = obtained->samples * obtained->channels;
	if (obtained->format != AudioFormatSigned8
		&& obtained->format != AudioFormatUnsigned8) {
		/* 16-bit samples */
		obtained->size *= 2;
	}

	return 1;
}

int AtariSoundSetupDeinitXbios() {
	Sndstatus(SND_RESET);
	Soundcmd(ADDERIN, ADCIN);	// restore adder with ADC+PSG
	Unlocksnd();

	return 1;
}

#endif
