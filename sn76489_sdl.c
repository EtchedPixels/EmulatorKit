#include <SDL2/SDL.h>
#include "emu76489.h"

#define SAMPLE_SIZE 512
#define FREQUENCY 48000
#define CHANNELS 1
#define CPU_CLK 4000000

void play_buffer(void*, unsigned char*, int);

SDL_AudioSpec spec = {
	.freq = FREQUENCY,
	.format = AUDIO_S16SYS,
	.channels = CHANNELS,
	.samples = SAMPLE_SIZE,
	.callback = play_buffer,
	.userdata = NULL
};

struct SN76489SDL {
    SDL_AudioDeviceID dev;
    SNG *sng;
};

static SNG *sng;

struct SN76489SDL *SN76489SDL_create(void)
{
    struct SN76489SDL *sn = malloc(sizeof(struct SN76489SDL));
    if (sn == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        return NULL;
    }

    sng = SNG_new(CPU_CLK, FREQUENCY);
    SNG_reset(sng);

    sn->sng = sng;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "Could not init SDL audio subsystem. %s\n", SDL_GetError());
        return NULL;
    }
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    SDL_PauseAudioDevice(dev, 0);
    return sn;
}

void play_buffer(void* userdata, unsigned char* stream, int len)
{
    static size_t total_sample_count = 0;
    Sint16 *audio_buffer = (Sint16*)stream;
    int bytes_per_sample = CHANNELS * sizeof(Sint16);
    int samples_to_write = len / bytes_per_sample;
    for (int i=0; i<samples_to_write; i++)
    {
        *audio_buffer++ = SNG_calc(sng);
        total_sample_count ++;
    }
}

void SN76489SDL_writeIO(struct SN76489SDL *sn, uint8_t val)
{
    SDL_LockAudioDevice(sn->dev);
    SNG_writeIO(sn->sng, val);
    SDL_UnlockAudioDevice(sn->dev);
}

void SN76489SDL_destroy(struct SN76489SDL *sn)
{
    SDL_CloseAudioDevice(sn->dev);
    SNG_delete(sn->sng);
    free(sn);
}
