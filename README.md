# uSound

This is a header-only implementation of a system-friendly audio XBIOS setup. It doesn't work out of the box on the STE/TT (unless equipped with EmuTOS) however it should work with most of the [XBIOS emulators](https://mikrosk.github.io/xbios) available.

It consists of an `enum`, `struct` and two functions:
```C
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
	uint32_t		size;		/* buffer size (calculated ) */
} USoundAudioSpec;

int USoundInitXbios(const USoundAudioSpec* desired, USoundAudioSpec* obtained);
int USoundDeinitXbios();
```
If you worked with SDL-1.2's [SDL_OpenAudio](https://www.libsdl.org/release/SDL-1.2.15/docs/html/sdlopenaudio.html) this should feel familiar. The biggest difference here is that the `obtained` parameter is mandatory, i.e. built-in conversion is not available.

`USoundInitXbios` / `USoundDeinitXbios` return `1` (true) on success and `0` (false) on failure. The return value of `1` also implies availability of the sound XBIOS API. `USoundInitXbios` may change `frequency`, `channels` and `format` parameters so always check them before usage!
