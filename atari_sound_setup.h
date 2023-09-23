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

/*
 * TODO: external DSP clock (also supported by "McSn" cookie)
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	/* "enumeration value 'AudioFormatCount' not handled in switch" */
		switch (desired->format) {
			case AudioFormatSigned8:
			case AudioFormatUnsigned8:
				if (formatsAvailable[i] == AudioFormatUnsigned8
					|| formatsAvailable[i] == AudioFormatSigned8) {
					obtained->format = formatsAvailable[i];
					found = 1;
				}
				break;

			case AudioFormatSigned16LSB:
			case AudioFormatUnsigned16LSB:
				if (formatsAvailable[i] == AudioFormatUnsigned16LSB
					|| formatsAvailable[i] == AudioFormatSigned16LSB) {
					obtained->format = formatsAvailable[i];
					found = 1;
				}
				break;

			case AudioFormatSigned16MSB:
			case AudioFormatUnsigned16MSB:
				if (formatsAvailable[i] == AudioFormatUnsigned16MSB
					|| formatsAvailable[i] == AudioFormatSigned16MSB) {
					obtained->format = formatsAvailable[i];
					found = 1;
				}
				break;
		}
#pragma GCC diagnostic pop
	}

	/* prefer the same sign */
	for (int i = 0; !found && i < AudioFormatCount; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"	/* "enumeration value 'AudioFormatCount' not handled in switch" */
		switch (desired->format) {
			case AudioFormatSigned8:
			case AudioFormatSigned16LSB:
			case AudioFormatSigned16MSB:
				if (formatsAvailable[i] == AudioFormatSigned16MSB
					|| formatsAvailable[i] == AudioFormatSigned16LSB) {
					obtained->format = formatsAvailable[i];
					found = 1;
				}
				break;

			case AudioFormatUnsigned8:
			case AudioFormatUnsigned16LSB:
			case AudioFormatUnsigned16MSB:
				if (formatsAvailable[i] == AudioFormatUnsigned16MSB
					|| formatsAvailable[i] == AudioFormatUnsigned16LSB) {
					obtained->format = formatsAvailable[i];
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
		/* take the first available 16-bit format */
		if (formatsAvailable[i] != AudioFormatSigned8
			&& formatsAvailable[i] != AudioFormatUnsigned8) {
			obtained->format = formatsAvailable[i];
			found = 1;
		}
	}

	if (!found) {
		/* prefer the same sign while downgrading to 8-bit */
		if (formatsAvailable[AudioFormatSigned8]
			&& (desired->format == AudioFormatSigned16LSB
				|| desired->format == AudioFormatSigned16MSB)) {
			obtained->format = formatsAvailable[AudioFormatSigned8];
			found = 1;
		} else if (formatsAvailable[AudioFormatUnsigned8]
			&& (desired->format == AudioFormatUnsigned16LSB
				|| desired->format == AudioFormatUnsigned16MSB)) {
			obtained->format = formatsAvailable[AudioFormatUnsigned8];
			found = 1;
		}

		for (int i = 0; !found && i < AudioFormatCount; i++) {
			/* take the first available (8-bit) format */
			if (formatsAvailable[i]) {
				obtained->format = formatsAvailable[i];
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

	long snd;
	if (Getcookie(C__SND, &snd) != C_FOUND) {
		long mcsn;
		if (Getcookie(C_McSn, &mcsn) == C_FOUND) {
			/* X-Sound doesn't set _SND (MacSound does) */
			snd = SND_PSG | SND_8BIT;
		} else  {
			return 0;
		}
	}

	int formatsAvailable[AudioFormatCount] = { 0 };
	int has16bitMono = 0;

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

	if (mch == MCH_FALCON || mch == MCH_ARANYM) {
		/* use Falcon frequencies */
		int clk;
		int diff50 = abs(49170 - desired->frequency);
		int diff33 = abs(32780 - desired->frequency);
		int diff25 = abs(24585 - desired->frequency);
		int diff20 = abs(19668 - desired->frequency);
		int diff16 = abs(16390 - desired->frequency);
		int diff12 = abs(12292 - desired->frequency);
		int diff10 = abs(9834  - desired->frequency);
		int diff8  = abs(8195  - desired->frequency);

		if (diff50 < diff33) {
			obtained->frequency = 49170;
			clk = CLK50K;
		} else if (diff33 < diff25) {
			obtained->frequency = 32780;
			clk = CLK33K;
		} else if (diff25 < diff20) {
			obtained->frequency = 24585;
			clk = CLK25K;
		} else if (diff20 < diff16) {
			obtained->frequency = 19668;
			clk = CLK20K;
		} else if (diff16 < diff12) {
			obtained->frequency = 16390;
			clk = CLK16K;
		} else if (diff12 < diff10) {
			obtained->frequency = 12292;
			clk = CLK12K;
		} else if (diff10 < diff8) {
			obtained->frequency = 9834;
			clk = CLK10K;
		} else {
			obtained->frequency = 8195;
			clk = CLK8K;
		}

		Devconnect(DMAPLAY, DAC, CLK25M, clk, NO_SHAKE);
	} else {
		/* use STE/TT frequencies */
		int clk;
		int diff50 = abs(50066 - desired->frequency);
		int diff25 = abs(25033 - desired->frequency);
		int diff12 = abs(12517 - desired->frequency);
		int diff6  = abs(6258  - desired->frequency);

		if (diff50 < diff25) {
			obtained->frequency = 50066;
			clk = PRE160;
		} else if (diff25 < diff12) {
			obtained->frequency = 25033;
			clk = PRE320;
		} else if (diff12 < diff6) {
			obtained->frequency = 12517;
			clk = PRE640;
		} else {
			obtained->frequency = 6258;
			clk = PRE1280;
		}

		Soundcmd(SETPRESCALE, clk);
	}

	if (desired->channels == 1
		&& obtained->format != AudioFormatSigned8
		&& obtained->format != AudioFormatUnsigned8
		&& !has16bitMono) {
		/* Falcon lacks 16-bit mono */
		obtained->channels = 2;
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
