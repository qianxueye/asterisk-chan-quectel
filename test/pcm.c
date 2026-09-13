/*
 * Compile with production pcm.c and real ALSA headers. Only hardware-facing
 * ALSA calls and Asterisk logging/format metadata are replaced. Run with:
 * cc -std=c99 -D_POSIX_C_SOURCE=200809L -DAST_CONFIG_H -Itest/pcm-stubs -Isrc \
 *    src/pcm.c test/pcm.c -lasound -o /tmp/quectel-pcm-test
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <asterisk/app.h>
#include "pcm.h"

struct _snd_pcm {
    snd_pcm_state_t state;
    int prepare_result;
    int start_result;
    int prepare_calls;
    int start_calls;
};

static struct _snd_pcm device;
static struct {
    unsigned int channels;
    unsigned int rate;
    snd_pcm_uframes_t period;
    snd_pcm_uframes_t buffer;
    snd_pcm_uframes_t start_threshold;
    snd_pcm_uframes_t stop_threshold;
    int any_result;
    int set_rate_result;
    int get_rate_result;
    int get_period_result;
    int get_buffer_result;
    int poll_count;
    int poll_result;
    int close_calls;
    int period_set_calls;
} fixture;

static void reset_fixture(void)
{
    memset(&fixture, 0, sizeof(fixture));
    memset(&device, 0, sizeof(device));
    fixture.channels = 1;
    fixture.rate = 8000;
    fixture.period = 160;
    fixture.buffer = 8000;
    fixture.poll_count = 1;
    fixture.poll_result = 1;
    device.state = SND_PCM_STATE_PREPARED;
}

int snd_pcm_open(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream, int mode)
{
    (void)name;
    (void)stream;
    assert(mode == SND_PCM_NONBLOCK);
    *pcm = &device;
    return 0;
}
int snd_pcm_close(snd_pcm_t* pcm) { assert(pcm == &device); fixture.close_calls++; return 0; }
snd_pcm_state_t snd_pcm_state(snd_pcm_t* pcm) { return pcm->state; }
int snd_pcm_prepare(snd_pcm_t* pcm)
{
    pcm->prepare_calls++;
    if (!pcm->prepare_result) { pcm->state = SND_PCM_STATE_PREPARED; }
    return pcm->prepare_result;
}
int snd_pcm_start(snd_pcm_t* pcm)
{
    pcm->start_calls++;
    assert(pcm->state == SND_PCM_STATE_PREPARED);
    if (!pcm->start_result) { pcm->state = SND_PCM_STATE_RUNNING; }
    return pcm->start_result;
}
int snd_pcm_hw_params_any(snd_pcm_t* pcm, snd_pcm_hw_params_t* params)
{ (void)pcm; (void)params; return fixture.any_result; }
int snd_pcm_hw_params_set_access(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_access_t access)
{ (void)pcm; (void)params; assert(access == SND_PCM_ACCESS_MMAP_INTERLEAVED); return 0; }
int snd_pcm_hw_params_set_format(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_format_t format)
{ (void)pcm; (void)params; assert(format == SND_PCM_FORMAT_S16_LE); return 0; }
int snd_pcm_hw_params_set_channels_near(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int* channels)
{ (void)pcm; (void)params; *channels = fixture.channels; return 0; }
int snd_pcm_hw_params_set_rate(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int rate, int dir)
{ (void)pcm; (void)params; (void)rate; (void)dir; return fixture.set_rate_result; }
int snd_pcm_hw_params_set_period_size_near(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames, int* dir)
{ (void)pcm; (void)params; (void)dir; fixture.period_set_calls++; *frames = fixture.period; return 0; }
int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames)
{ (void)pcm; (void)params; *frames = fixture.buffer; return 0; }
int snd_pcm_hw_params(snd_pcm_t* pcm, snd_pcm_hw_params_t* params)
{ (void)pcm; (void)params; return 0; }
int snd_pcm_hw_params_current(snd_pcm_t* pcm, snd_pcm_hw_params_t* params)
{ (void)pcm; (void)params; return 0; }
int snd_pcm_hw_params_get_channels(const snd_pcm_hw_params_t* params, unsigned int* channels)
{ (void)params; *channels = fixture.channels; return 0; }
int snd_pcm_hw_params_get_rate(const snd_pcm_hw_params_t* params, unsigned int* rate, int* dir)
{ (void)params; (void)dir; *rate = fixture.rate; return fixture.get_rate_result; }
int snd_pcm_hw_params_get_period_size(const snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames, int* dir)
{ (void)params; (void)dir; *frames = fixture.period; return fixture.get_period_result; }
int snd_pcm_hw_params_get_buffer_size(const snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames)
{ (void)params; *frames = fixture.buffer; return fixture.get_buffer_result; }
int snd_pcm_sw_params_current(snd_pcm_t* pcm, snd_pcm_sw_params_t* params)
{ (void)pcm; (void)params; return 0; }
int snd_pcm_sw_params_get_boundary(const snd_pcm_sw_params_t* params, snd_pcm_uframes_t* boundary)
{ (void)params; *boundary = 1UL << 30; return 0; }
int snd_pcm_sw_params_set_start_threshold(snd_pcm_t* pcm, snd_pcm_sw_params_t* params, snd_pcm_uframes_t frames)
{ (void)pcm; (void)params; fixture.start_threshold = frames; return 0; }
int snd_pcm_sw_params_set_stop_threshold(snd_pcm_t* pcm, snd_pcm_sw_params_t* params, snd_pcm_uframes_t frames)
{ (void)pcm; (void)params; fixture.stop_threshold = frames; return 0; }
int snd_pcm_sw_params_set_silence_threshold(snd_pcm_t* pcm, snd_pcm_sw_params_t* params, snd_pcm_uframes_t frames)
{ (void)pcm; (void)params; (void)frames; return 0; }
int snd_pcm_sw_params_set_silence_size(snd_pcm_t* pcm, snd_pcm_sw_params_t* params, snd_pcm_uframes_t frames)
{ (void)pcm; (void)params; (void)frames; return 0; }
int snd_pcm_sw_params(snd_pcm_t* pcm, snd_pcm_sw_params_t* params)
{ (void)pcm; (void)params; return 0; }
int snd_pcm_poll_descriptors_count(snd_pcm_t* pcm) { (void)pcm; return fixture.poll_count; }
int snd_pcm_poll_descriptors(snd_pcm_t* pcm, struct pollfd* pfds, unsigned int space)
{ (void)pcm; assert(space > 0); pfds[0].fd = 42; pfds[0].events = POLLIN; return fixture.poll_result; }

static int initialize(snd_pcm_stream_t stream, unsigned int ms, unsigned int rate)
{
    struct ast_format fmt = {rate, ms};
    snd_pcm_t* pcm = NULL;
    unsigned int channels = 999;
    int fd = -1;
    int result = pcm_init("mock", stream, &fmt, &pcm, &channels, &fd);
    if (result < 0) {
        assert(pcm == NULL);
        assert(channels == 999);
        assert(fd == -1);
        assert(fixture.close_calls == 1);
    } else {
        assert(result == 0);
        assert(pcm == &device);
        assert(channels == fixture.channels);
        assert(fd == (stream == SND_PCM_STREAM_CAPTURE ? 42 : -1));
        assert(fixture.close_calls == 0);
    }
    return result;
}

static void test_init_errors(void)
{
    reset_fixture(); fixture.any_result = -EIO;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EIO);
    reset_fixture(); fixture.set_rate_result = -EINVAL;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EINVAL);
    assert(fixture.period_set_calls == 0);
    reset_fixture(); fixture.get_rate_result = -EIO;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EIO);
    reset_fixture(); fixture.rate = 48000;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.channels = 2;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.channels = 3;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.channels = 0;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.poll_count = 0;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.poll_result = 0;
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.get_period_result = -EIO;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == -EIO);
    reset_fixture(); fixture.get_buffer_result = -EIO;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == -EIO);
    reset_fixture(); fixture.period = 0;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == -EINVAL);
    reset_fixture(); fixture.buffer = fixture.period - 1;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == -EINVAL);
}

static void test_thresholds(void)
{
    const unsigned int ms[] = {20, 50, 75, 125, 200};
    const unsigned int expected[] = {100, 100, 150, 250, 250};
    for (unsigned int i = 0; i < sizeof(ms) / sizeof(ms[0]); ++i) {
        reset_fixture();
        assert(initialize(SND_PCM_STREAM_PLAYBACK, ms[i], 8000) == 0);
        assert(fixture.start_threshold == expected[i] * 8);
        assert(fixture.stop_threshold == 7840);
    }
    reset_fixture(); fixture.period = 128; fixture.buffer = 512;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == 0);
    assert(fixture.start_threshold == 512);
    assert(fixture.stop_threshold == 384);
    reset_fixture(); fixture.period = fixture.buffer = 160;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == 0);
    assert(fixture.start_threshold == 160 && fixture.stop_threshold == 160);
    reset_fixture(); fixture.period = 1200;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 8000) == 0);
    assert(fixture.start_threshold == 1200);
    assert(fixture.stop_threshold == 6800);
    reset_fixture(); fixture.rate = 16000; fixture.channels = 2;
    assert(initialize(SND_PCM_STREAM_PLAYBACK, 20, 16000) == 0);
    assert(fixture.start_threshold == 1600);
    reset_fixture();
    assert(initialize(SND_PCM_STREAM_CAPTURE, 20, 8000) == 0);
}

static void test_stream_transitions(void)
{
    const snd_pcm_state_t states[] = {SND_PCM_STATE_SETUP, SND_PCM_STATE_XRUN, SND_PCM_STATE_SUSPENDED};
    for (unsigned int i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        reset_fixture(); device.state = states[i];
        assert(pcm_start_capture(&device) == 0);
        assert(device.state == SND_PCM_STATE_RUNNING);
        assert(device.prepare_calls == 1 && device.start_calls == 1);
        assert(pcm_start_capture(&device) == 0);
        assert(device.prepare_calls == 1 && device.start_calls == 1);
        reset_fixture(); device.state = states[i];
        assert(pcm_prepare_playback(&device) == 0);
        assert(device.state == SND_PCM_STATE_PREPARED);
        assert(device.prepare_calls == 1 && device.start_calls == 0);
        reset_fixture(); device.state = states[i]; device.prepare_result = -EIO;
        assert(pcm_start_capture(&device) == -EIO);
        assert(device.prepare_calls == 1 && device.start_calls == 0);
        assert(pcm_prepare_playback(&device) == -EIO);
        assert(device.start_calls == 0);
    }
    reset_fixture();
    assert(pcm_start_capture(&device) == 0);
    assert(device.prepare_calls == 0 && device.start_calls == 1);
    reset_fixture(); device.start_result = -EAGAIN;
    assert(pcm_start_capture(&device) == -EAGAIN);
    assert(device.start_calls == 1 && device.prepare_calls == 0);
    device.start_result = 0;
    assert(pcm_start_capture(&device) == 0);
    assert(device.state == SND_PCM_STATE_RUNNING && device.start_calls == 2);
    reset_fixture();
    assert(pcm_prepare_playback(&device) == 0);
    assert(device.prepare_calls == 0 && device.start_calls == 0);
    device.state = SND_PCM_STATE_RUNNING;
    assert(pcm_prepare_playback(&device) == 0);
    assert(device.prepare_calls == 0 && device.start_calls == 0);
    device.state = SND_PCM_STATE_DISCONNECTED;
    assert(pcm_start_capture(&device) == -ENODEV);
    assert(pcm_prepare_playback(&device) == -ENODEV);
    device.state = SND_PCM_STATE_OPEN;
    assert(pcm_start_capture(&device) == -EBADFD);
    assert(pcm_prepare_playback(&device) == -EBADFD);
    assert(pcm_start_capture(NULL) == -ENODEV);
    assert(pcm_prepare_playback(NULL) == -ENODEV);
    assert(device.prepare_calls == 0 && device.start_calls == 0);
}

int main(void)
{
    test_init_errors();
    test_thresholds();
    test_stream_transitions();
    puts("PCM regression tests passed: initialization, negotiated thresholds, bounded recovery/start");
    return 0;
}
