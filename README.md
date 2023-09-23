# atari_sound_setup

This is a header-only implementation of a system-friendly audio XBIOS setup. It should also work on most of the [XBIOS emulators](https://mikrosk.github.io/xbios) available.

It consists of an `enum`, `struct` and two functions:
```C
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

int AtariSoundSetupInitXbios(const AudioSpec* desired, AudioSpec* obtained);
int AtariSoundSetupDeinitXbios();
```
If you worked with SDL-1.2's [SDL_OpenAudio](https://www.libsdl.org/release/SDL-1.2.15/docs/html/sdlopenaudio.html) this should feel familiar. The biggest difference here is that the `obtained` parameter is mandatory, i.e. built-in conversion is not available.

`AtariSoundSetupInitXbios` / `AtariSoundSetupDeinitXbios` return `1` (true) on success and `0` (false) on failure. `AtariSoundSetupInitXbios` may change `frequency`, `channels` and `format` parameters so always check them before usage!
