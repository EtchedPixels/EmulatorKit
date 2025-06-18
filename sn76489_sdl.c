#include <SDL2/SDL.h>
#include "emu76489.h"
#include "sn76489.h"

#define SAMPLE_SIZE 512
#define FREQUENCY 48000
#define CHANNELS 1
#define CPU_CLK 4000000

void play_buffer(void *, unsigned char *, int);

SDL_AudioSpec spec = {
	.freq = FREQUENCY,
	.format = AUDIO_S16SYS,
	.channels = CHANNELS,
	.samples = SAMPLE_SIZE,
	.callback = play_buffer,
	.userdata = NULL
};

struct sn76489 {
    SDL_AudioDeviceID dev;
    SNG *sng;
};

static SNG *sng;

struct sn76489 *sn76489_create(void)
{
    struct sn76489 *sn = malloc(sizeof(struct sn76489));
    if (sn == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    sng = SNG_new(CPU_CLK, FREQUENCY);
    SNG_set_quality(sng, 0xff);
    SNG_reset(sng);

    sn->sng = sng;

    /* TODO: We probablyt should't be doing this multiple times if
       we have multiple audio devices */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "Could not init SDL audio subsystem. %s\n", SDL_GetError());
        return NULL;
    }
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    SDL_PauseAudioDevice(dev, 0);
    return sn;
}

void play_buffer(void *userdata, unsigned char *stream, int len)
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

uint8_t sn76489_ready(struct sn76489 *sn)
{
    return sn->sng->ready;
}

void sn76489_write(struct sn76489 *sn, uint8_t val)
{
    SDL_LockAudioDevice(sn->dev);
    SNG_writeIO(sn->sng, val);
    SDL_UnlockAudioDevice(sn->dev);
}

void sn76489_destroy(struct sn76489 *sn)
{
    SDL_CloseAudioDevice(sn->dev);
    SNG_delete(sn->sng);
    free(sn);
}
