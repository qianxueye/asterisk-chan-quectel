/* Real ALSA null-device integration: no physical audio device or ALSA mocks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <alsa/asoundlib.h>
#include <asterisk/app.h>
#include "pcm.h"

static void start_playback(snd_pcm_t* playback)
{
    const int16_t silence[160] = {0};
    /* Normal 20 ms frames must reach the configured automatic start threshold. */
    for (unsigned int i = 0; i < 50 && snd_pcm_state(playback) == SND_PCM_STATE_PREPARED; ++i) {
        assert(snd_pcm_mmap_writei(playback, silence, 160) == 160);
    }
    assert(snd_pcm_state(playback) == SND_PCM_STATE_RUNNING);
}

int main(void)
{
    const struct ast_format format = {8000, 20};
    snd_pcm_t* capture = NULL;
    snd_pcm_t* playback = NULL;
    unsigned int capture_channels = 0;
    unsigned int playback_channels = 0;
    int capture_fd = -1;

    assert(pcm_init("null", SND_PCM_STREAM_CAPTURE, &format, &capture, &capture_channels, &capture_fd) == 0);
    assert(pcm_init("null", SND_PCM_STREAM_PLAYBACK, &format, &playback, &playback_channels, NULL) == 0);
    assert(capture_channels == 1 && playback_channels == 1 && capture_fd >= 0);
    assert(capture != playback);
    assert(snd_pcm_state(capture) == SND_PCM_STATE_PREPARED);
    assert(snd_pcm_state(playback) == SND_PCM_STATE_PREPARED);
    assert(snd_pcm_avail_update(capture) == 0);

    /* Capture must start before any playback samples have been supplied. */
    assert(pcm_start_capture(capture) == 0);
    assert(snd_pcm_state(capture) == SND_PCM_STATE_RUNNING);
    assert(snd_pcm_state(playback) == SND_PCM_STATE_PREPARED);
    int16_t input[160] = {0};
    assert(snd_pcm_mmap_readi(capture, input, 160) == 160);
    assert(snd_pcm_state(playback) == SND_PCM_STATE_PREPARED);

    for (unsigned int i = 0; i < 20; ++i) {
        start_playback(playback);
        assert(snd_pcm_state(capture) == SND_PCM_STATE_RUNNING);

        assert(snd_pcm_drop(playback) == 0);
        assert(snd_pcm_state(playback) == SND_PCM_STATE_SETUP);
        assert(snd_pcm_state(capture) == SND_PCM_STATE_RUNNING);
        assert(pcm_prepare_playback(playback) == 0);
        assert(snd_pcm_state(playback) == SND_PCM_STATE_PREPARED);
        assert(snd_pcm_state(capture) == SND_PCM_STATE_RUNNING);

        /* Capture restart must leave both prepared and running playback alone. */
        assert(snd_pcm_drop(capture) == 0);
        assert(pcm_start_capture(capture) == 0);
        assert(snd_pcm_state(capture) == SND_PCM_STATE_RUNNING);
        assert(snd_pcm_state(playback) == SND_PCM_STATE_PREPARED);
        start_playback(playback);
        assert(snd_pcm_drop(capture) == 0);
        assert(snd_pcm_state(playback) == SND_PCM_STATE_RUNNING);
        assert(pcm_start_capture(capture) == 0);
        assert(snd_pcm_state(capture) == SND_PCM_STATE_RUNNING);
        assert(snd_pcm_state(playback) == SND_PCM_STATE_RUNNING);
        assert(snd_pcm_drop(playback) == 0);
        assert(pcm_prepare_playback(playback) == 0);
    }

    assert(pcm_close("null", &capture, SND_PCM_STREAM_CAPTURE) == 0);
    assert(pcm_close("null", &playback, SND_PCM_STREAM_PLAYBACK) == 0);
    assert(capture == NULL && playback == NULL);
    printf("ALSA %s null integration passed: capture before playback, independent streams, 20 restart cycles\n",
           snd_asoundlib_version());
    return 0;
}
