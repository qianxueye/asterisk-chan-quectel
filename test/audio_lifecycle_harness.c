/* Deterministic environment for the actual production functions inserted by
 * test_audio_lifecycle.py. These tests never open audio or serial devices. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define attribute_unused __attribute__((unused))
#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif
#define AST_CAUSE_REQUESTED_CHAN_UNAVAIL 44
#define AST_CAUSE_NORMAL_UNSPECIFIED 31
#define AST_FRAME_NULL 0
#define AST_FRAME_VOICE 1
#define AST_FORMAT_CMP_EQUAL 0
#define PTIME_CAPTURE 20
#define PTIME_PLAYBACK 20
#define PIPE_READ 0
#define PIPE_WRITE 1
#define TRIBOOL_FALSE 0
#define CALL_FLAG_ACTIVATED 4
#define CALL_FLAG_NEED_HANGUP 2
#define CALL_FLAG_MASTER 32
#define CALL_FLAG_BRIDGE_LOOP 64
#define CALL_FLAG_MULTIPARTY 256
#define CALL_FLAG_LOCAL_CHANNEL 1024
#define CALL_STATE_ACTIVE 0
#define CALL_STATE_ONHOLD 1
#define CALL_STATE_RELEASED 6
#define CPVT_TEST_FLAG(c, f) ((c)->flags & (f))
#define CPVT_IS_MASTER(c) CPVT_TEST_FLAG(c, CALL_FLAG_MASTER)
#define CPVT_IS_LOCAL(c) CPVT_TEST_FLAG(c, CALL_FLAG_LOCAL_CHANNEL)
#define CPVT_IS_SOUND_SOURCE(c) ((c)->state != CALL_STATE_RELEASED)
#define CPVT_RESET_FLAG(c, f) ((c)->flags &= ~(f))
#define CPVT_RESET_FLAGS(c, f) CPVT_RESET_FLAG(c, f)
#define CPVT_SET_FLAGS(c, f) ((c)->flags |= (f))
#define CPVT_DIRECTION(c) 0
#define CONF_UNIQ(p, f) ((p)->f)
#define CONF_SHARED(p, f) ((p)->f)
#define PVT_STAT(p, f) ((p)->stat.f)
#define PVT_NO_CHANS(p) (!(p)->chansno)
#define PVT_ID(p) "test"
#define SCOPED_CPVT_TL(name, c) ((void)(c))
#define AST_LIST_TRAVERSE(h, c, e) for ((c) = (h)->first; (c); (c) = (c)->next)
#define AST_LIST_TRAVERSE_SAFE_BEGIN(h, c, e) { struct cpvt *saved_next; \
    for ((c) = (h)->first; (c) && (saved_next = (c)->next, 1); (c) = saved_next)
#define AST_LIST_TRAVERSE_SAFE_END }
#define ast_debug(...) ((void)0)
#define ast_log(...) ((void)0)
#define ast_verb(...) ((void)0)
#define pcm_show_state(...) ((void)0)
#define ast_free free

typedef long snd_pcm_sframes_t;
typedef enum { SND_PCM_STATE_SETUP, SND_PCM_STATE_PREPARED, SND_PCM_STATE_RUNNING,
    SND_PCM_STATE_XRUN, SND_PCM_STATE_SUSPENDED, SND_PCM_STATE_DISCONNECTED } snd_pcm_state_t;
#define SND_PCM_STREAM_CAPTURE 1
#define SND_PCM_STREAM_PLAYBACK 0
typedef struct { snd_pcm_state_t state; int starts, prepares, drops, writes; } snd_pcm_t;
struct ast_format { int unused; };
struct ast_timer { int fd; };
struct ast_frame {
    int frametype, samples, datalen;
    struct { void *ptr; } data;
    struct { struct ast_format *format; } subclass;
};
struct pvt;
struct cpvt;
struct ast_channel { struct cpvt *cpvt; int fd[2], fdno; };
struct cpvt {
    struct cpvt *next;
    struct pvt *pvt;
    struct ast_channel *channel;
    unsigned flags;
    int call_idx, state, rd_pipe[2], mixstream;
    void *buffer;
    struct ast_frame frame;
};
struct pvt {
    snd_pcm_t *icard, *ocard;
    int audio_fd, uac, multiparty, chansno;
    unsigned ocard_channels;
    const char *alsadev;
    struct ast_timer *a_timer;
    int write_mixb;
    struct { struct cpvt *first; } chans;
    void *silence_buf, *write_buf;
    struct { int read_frames, read_sframes, write_frames, write_tframes;
        size_t a_read_bytes, a_write_bytes; } stat;
};
static struct ast_format format;
static struct ast_frame ast_null_frame;
static snd_pcm_t capture, playback;
static int avail_result, read_result, write_results[16], write_count, write_index;
static int write_offsets[16], write_sizes[16], start_error, transient_start_errors;
static int start_calls, capture_prepare_calls, playback_prepare_calls;
static int sequence, start_sequence, fd_sequence, tty_reads, tty_writes;
static int close_calls, timer_closes, relinks, restates, link_calls, hangups;
static int disconnect_hangups, disconnect_releases;
static const int16_t *write_origin;

static int pcm_start_capture(snd_pcm_t *pcm)
{
    assert(pcm == &capture);
    ++start_calls;
    if (transient_start_errors) { --transient_start_errors; return start_error; }
    if (pcm->state != SND_PCM_STATE_RUNNING) {
        if (pcm->state != SND_PCM_STATE_PREPARED) { ++pcm->prepares; ++capture_prepare_calls; }
        ++pcm->starts;
        pcm->state = SND_PCM_STATE_RUNNING;
        start_sequence = ++sequence;
    }
    return 0;
}
static int pcm_prepare_playback(snd_pcm_t *pcm)
{
    assert(pcm == &playback);
    ++playback_prepare_calls;
    if (pcm->state != SND_PCM_STATE_PREPARED && pcm->state != SND_PCM_STATE_RUNNING) {
        ++pcm->prepares;
        pcm->state = SND_PCM_STATE_PREPARED;
    }
    return 0;
}
static int snd_pcm_drop(snd_pcm_t *pcm) { ++pcm->drops; pcm->state = SND_PCM_STATE_SETUP; return 0; }
static const char *snd_strerror(int err) { return "fake ALSA error"; }
static snd_pcm_sframes_t snd_pcm_avail_update(snd_pcm_t *pcm)
{
    if (avail_result == -EPIPE) pcm->state = SND_PCM_STATE_XRUN;
    if (avail_result == -ESTRPIPE) pcm->state = SND_PCM_STATE_SUSPENDED;
    return avail_result;
}
static int snd_pcm_mmap_readi(snd_pcm_t *pcm, void *data, size_t frames)
{
    assert(pcm->state == SND_PCM_STATE_RUNNING);
    if (read_result == -EPIPE) pcm->state = SND_PCM_STATE_XRUN;
    if (read_result == -ESTRPIPE) pcm->state = SND_PCM_STATE_SUSPENDED;
    if (read_result < 0) return read_result;
    int result = read_result ? read_result : (int)frames;
    assert(result <= (int)frames);
    for (int i = 0; i < result; ++i) ((int16_t *)data)[i] = i + 1;
    return result;
}
static int snd_pcm_mmap_writei(snd_pcm_t *pcm, const void *data, size_t frames)
{
    assert(pcm == &playback);
    assert(write_index < 16);
    write_offsets[write_index] = (const int16_t *)data - write_origin;
    write_sizes[write_index] = (int)frames;
    int result = write_index < write_count ? write_results[write_index] : (int)frames;
    ++write_index; ++pcm->writes;
    if (result == -EPIPE) pcm->state = SND_PCM_STATE_XRUN;
    if (result == -ESTRPIPE) pcm->state = SND_PCM_STATE_SUSPENDED;
    assert(result <= (int)frames);
    return result;
}
static int snd_pcm_mmap_writen(snd_pcm_t *pcm, void **data, size_t frames)
{
    assert(data[0] == data[1]);
    return snd_pcm_mmap_writei(pcm, data[0], frames);
}
static int pcm_init(const char *dev, int stream, const struct ast_format *fmt,
                    snd_pcm_t **pcm, unsigned *channels, int *fd)
{
    *pcm = stream == SND_PCM_STREAM_CAPTURE ? &capture : &playback;
    (*pcm)->state = SND_PCM_STATE_PREPARED;
    *channels = 1;
    if (fd) *fd = 9;
    return 0;
}
/* Kept as external definitions so reverting the link removal is observable. */
int snd_pcm_link(snd_pcm_t *a, snd_pcm_t *b) { ++link_calls; return 0; }
int snd_pcm_close(snd_pcm_t *pcm) { return 0; }
static void ast_channel_set_fd(struct ast_channel *channel, int index, int fd)
{
    channel->fd[index] = fd;
    if (index == 0 && fd >= 0) fd_sequence = ++sequence;
}
static int ast_channel_fd(struct ast_channel *channel, int index) { return channel->fd[index]; }
static int ast_channel_fdno(struct ast_channel *channel) { return channel->fdno; }
static struct cpvt *ast_channel_tech_pvt(struct ast_channel *channel) { return channel->cpvt; }
static const struct ast_format *pvt_get_audio_format(struct pvt *pvt) { return &format; }
static size_t pvt_get_audio_frame_size(int ptime, const struct ast_format *fmt) { return 320; }
static int ast_format_cmp(const struct ast_format *a, const struct ast_format *b) { return AST_FORMAT_CMP_EQUAL; }
static void ast_frame_byteswap_le(struct ast_frame *f) {}
static int ast_timer_fd(struct ast_timer *timer) { return timer->fd; }
static void ast_timer_close(struct ast_timer *timer) { ++timer_closes; }
static void ast_timer_ack(struct ast_timer *timer, int n) {}
static void timing_write_tty(struct pvt *pvt, size_t frame_size) { ++tty_writes; }
static void mixb_detach(int *buffer, int *stream) {}
static void mixb_attach(int *buffer, int *stream) {}
static void *cpvt_get_buffer(struct cpvt *cpvt) { return cpvt->buffer; }
static struct ast_frame *cpvt_prepare_voice_frame(struct cpvt *cpvt, void *data, int samples, const struct ast_format *fmt)
{
    cpvt->frame = (struct ast_frame) { .frametype = AST_FRAME_VOICE, .samples = samples, .datalen = samples * 2, .data.ptr = data };
    return &cpvt->frame;
}
static struct ast_frame *cpvt_prepare_silence_voice_frame(struct cpvt *cpvt, int samples, const struct ast_format *fmt)
{ return cpvt_prepare_voice_frame(cpvt, NULL, samples, fmt); }
static void write_conference(struct pvt *pvt, const char *buffer, size_t bytes) {}
static struct ast_frame *channel_read_tty(struct cpvt *cpvt, struct pvt *pvt, size_t bytes, const struct ast_format *fmt)
{ ++tty_reads; return NULL; }
static int channel_write_tty(struct ast_channel *channel, struct ast_frame *frame, struct cpvt *cpvt, struct pvt *pvt)
{ ++tty_writes; return 0; }
static int close(int fd) { ++close_calls; return 0; }
static void decrease_chan_counters(struct cpvt *cpvt, struct pvt *pvt) { assert(pvt->chansno > 0); --pvt->chansno; }
static void relink_to_sys_chan(struct cpvt *cpvt, struct pvt *pvt) { ++relinks; }
static void pvt_try_restate(struct pvt *pvt)
{ assert(relinks); assert(!pvt->silence_buf); assert(!pvt->write_buf); ++restates; }

static int channel_enqueue_hangup(struct ast_channel *channel, int cause)
{ ++hangups; return 0; }
static int at_hangup_immediately(struct cpvt *cpvt, int cause)
{ assert(cpvt->call_idx == disconnect_hangups); ++disconnect_hangups; return 0; }
static int cpvt_change_state(struct cpvt *cpvt, int state, int cause)
{
    assert(state == CALL_STATE_RELEASED);
    assert(!(cpvt->flags & CALL_FLAG_NEED_HANGUP));
    assert(cpvt->pvt->chans.first == cpvt);
    cpvt->pvt->chans.first = cpvt->next;
    --cpvt->pvt->chansno;
    free(cpvt);
    ++disconnect_releases;
    return 1;
}

/* INSERT PRODUCTION FUNCTIONS */

static void reset(struct pvt *pvt, struct cpvt *cpvt, struct ast_channel *channel)
{
    memset(&capture, 0, sizeof(capture)); memset(&playback, 0, sizeof(playback));
    capture.state = playback.state = SND_PCM_STATE_PREPARED;
    *pvt = (struct pvt) { .icard = &capture, .ocard = &playback, .audio_fd = 9,
        .uac = 1, .ocard_channels = 1, .chans.first = cpvt, .chansno = 1 };
    *cpvt = (struct cpvt) { .pvt = pvt, .channel = channel, .rd_pipe = { -1, -1 } };
    *channel = (struct ast_channel) { .cpvt = cpvt, .fd = { -1, -1 } };
    avail_result = 160; read_result = 0; write_count = write_index = 0;
    transient_start_errors = start_error = 0;
    start_calls = capture_prepare_calls = playback_prepare_calls = 0;
    sequence = start_sequence = fd_sequence = 0;
    tty_reads = tty_writes = close_calls = timer_closes = relinks = restates = link_calls = hangups = 0;
}

static void test_activation_and_handoff(void)
{
    struct pvt p; struct cpvt a, b; struct ast_channel ca, cb;
    reset(&p, &a, &ca);
    assert(soundcard_init(&p) == 0);
    assert(link_calls == 0);
    initial_audio_fd(&p, &ca);
    assert(ca.fd[0] == -1);
    cpvt_call_activate(&a);
    assert(capture.state == SND_PCM_STATE_RUNNING && capture.starts == 1);
    assert(playback.writes == 0 && playback.prepares == 0 && playback.starts == 0);
    assert(start_sequence > 0 && start_sequence < fd_sequence);
    assert(ca.fd[0] == p.audio_fd && CPVT_IS_MASTER(&a));
    cpvt_call_activate(&a);
    assert(capture.starts == 1 && capture.drops == 0);

    b = (struct cpvt) { .pvt = &p, .channel = &cb, .rd_pipe = {-1, -1} };
    cb = (struct ast_channel) { .cpvt = &b, .fd = {-1, -1} };
    a.next = &b;
    cpvt_call_activate(&b);
    assert(!CPVT_IS_MASTER(&a) && CPVT_IS_MASTER(&b));
    assert(ca.fd[0] == -1 && cb.fd[0] == p.audio_fd);
    assert(capture.drops == 1 && playback.drops == 1 && capture.starts == 2);
    cpvt_call_disactivate(&a); /* late HOLD for old sound source */
    assert(capture.drops == 1 && playback.drops == 1);
    assert(capture.state == SND_PCM_STATE_RUNNING);
    cpvt_call_disactivate(&b);
    assert(capture.drops == 2 && playback.drops == 2 && cb.fd[0] == -1);
    cpvt_call_activate(&a); /* resume earlier held call */
    assert(CPVT_IS_MASTER(&a) && ca.fd[0] == p.audio_fd);
    assert(capture.state == SND_PCM_STATE_RUNNING && capture.starts == 3);
}

static void test_start_failure_is_bounded(void)
{
    struct pvt p; struct cpvt a; struct ast_channel ca;
    reset(&p, &a, &ca);
    start_error = -EINTR; transient_start_errors = 1;
    cpvt_call_activate(&a);
    assert(start_calls == 2 && capture.state == SND_PCM_STATE_RUNNING);
    reset(&p, &a, &ca);
    start_error = -EAGAIN; transient_start_errors = 100;
    assert(cpvt_call_activate(&a) == -1);
    assert(start_calls == 2 && capture.state == SND_PCM_STATE_PREPARED);
    assert(!CPVT_IS_MASTER(&a) && ca.fd[0] == -1 && hangups == 1);
}

static void test_capture_recovery(void)
{
    struct pvt p; struct cpvt a; struct ast_channel ca; int16_t data[160];
    int errors[] = { -EPIPE, -ESTRPIPE };
    for (size_t i = 0; i < sizeof(errors)/sizeof(errors[0]); ++i) {
        reset(&p, &a, &ca); a.buffer = data; cpvt_call_activate(&a);
        playback.state = SND_PCM_STATE_RUNNING;
        avail_result = errors[i];
        assert(channel_read(&ca) == &ast_null_frame);
        assert(capture.state == SND_PCM_STATE_RUNNING && capture.starts == 2);
        assert(playback.prepares == 0 && playback.drops == 0 && playback_prepare_calls == 0);
        avail_result = 160; read_result = errors[i];
        assert(channel_read(&ca) == &ast_null_frame);
        assert(capture.state == SND_PCM_STATE_RUNNING && capture.starts == 3);
        read_result = 0;
        struct ast_frame *f = channel_read(&ca);
        assert(f->samples == 160 && data[0] == 1 && p.stat.a_read_bytes == 320);
    }
    reset(&p, &a, &ca); a.buffer = data; cpvt_call_activate(&a);
    avail_result = 40;
    assert(channel_read(&ca)->samples == 40);
    assert(p.stat.a_read_bytes == 80);
    assert(p.stat.read_sframes == 1);
    avail_result = 0;
    assert(channel_read(&ca) == &ast_null_frame);
    avail_result = 160; read_result = -EINTR;
    assert(channel_read(&ca) == &ast_null_frame);
    read_result = -EAGAIN;
    assert(channel_read(&ca) == &ast_null_frame);
    assert(hangups == 0 && CPVT_IS_MASTER(&a));
    read_result = 0; avail_result = -EAGAIN;
    assert(channel_read(&ca) == &ast_null_frame);
    avail_result = -EINTR;
    assert(channel_read(&ca) == &ast_null_frame);
    assert(hangups == 0 && CPVT_IS_MASTER(&a));
    cpvt_call_disactivate(&a);
    assert(channel_read(&ca) == &ast_null_frame && tty_reads == 0);

    reset(&p, &a, &ca); a.buffer = data; cpvt_call_activate(&a);
    capture.state = SND_PCM_STATE_XRUN;
    start_error = -EINTR; transient_start_errors = 1;
    assert(channel_read(&ca)->samples == 160);
    assert(hangups == 0 && CPVT_IS_MASTER(&a));
    capture.state = SND_PCM_STATE_SUSPENDED;
    start_error = -EAGAIN; transient_start_errors = 100;
    int before = start_calls;
    assert(channel_read(&ca) == &ast_null_frame);
    assert(start_calls == before + 2 && hangups == 1);
    assert(!CPVT_IS_MASTER(&a) && ca.fd[0] == -1);
}

static void test_playback_progress_and_recovery(void)
{
    struct pvt p; struct cpvt a; struct ast_channel ca; int16_t data[160] = {0};
    struct ast_frame f = { .frametype = AST_FRAME_VOICE, .samples = 160, .datalen = 320,
        .data.ptr = data, .subclass.format = &format };
    write_origin = data;
    int errors[] = {-EPIPE, -ESTRPIPE};
    for (size_t i = 0; i < sizeof(errors)/sizeof(errors[0]); ++i) {
        reset(&p, &a, &ca); cpvt_call_activate(&a);
        write_count = 3; write_results[0] = 80; write_results[1] = errors[i]; write_results[2] = 80;
        assert(channel_write(&ca, &f) == 0);
        assert(write_index == 3 && write_offsets[0] == 0 && write_offsets[1] == 80 && write_offsets[2] == 80);
        assert(write_sizes[0] == 160 && write_sizes[1] == 80 && write_sizes[2] == 80);
        assert(p.stat.a_write_bytes == 320 && p.stat.write_frames == 1 && p.stat.write_tframes == 0);
        assert(capture.state == SND_PCM_STATE_RUNNING && capture.prepares == 0 && capture.drops == 0 && capture.starts == 1);
    }
    reset(&p, &a, &ca); cpvt_call_activate(&a); p.ocard_channels = 2;
    write_count = 2; write_results[0] = 40; write_results[1] = 120;
    assert(channel_write(&ca, &f) == 0 && write_offsets[1] == 40);
    reset(&p, &a, &ca); cpvt_call_activate(&a);
    write_count = 2; write_results[0] = 80; write_results[1] = -EAGAIN;
    assert(channel_write(&ca, &f) == 0);
    assert(write_index == 2 && p.stat.a_write_bytes == 160 && p.stat.write_tframes == 1);
    reset(&p, &a, &ca); cpvt_call_activate(&a);
    write_count = 16; for (int i = 0; i < 16; ++i) write_results[i] = -EINTR;
    assert(channel_write(&ca, &f) == 0 && write_index == 8);
    assert(p.stat.a_write_bytes == 0 && p.stat.write_tframes == 1);
    cpvt_call_disactivate(&a);
    assert(channel_write(&ca, &f) == 0 && tty_writes == 0 && write_index == 8);
}

static void test_last_channel_cleanup(void)
{
    struct pvt p; struct cpvt unused; struct ast_channel ca; struct ast_timer timer = {7};
    reset(&p, &unused, &ca);
    p.chansno = 2; p.silence_buf = calloc(1, 64); p.write_buf = calloc(1, 64); p.a_timer = &timer;
    struct cpvt *a = calloc(1, sizeof(*a)); a->pvt = &p; a->buffer = calloc(1, 64);
    cpvt_free(a);
    assert(p.chansno == 1 && p.silence_buf && p.write_buf && timer_closes == 0 && restates == 0);
    a = calloc(1, sizeof(*a)); a->pvt = &p; a->buffer = calloc(1, 64);
    cpvt_free(a);
    assert(p.chansno == 0 && !p.silence_buf && !p.write_buf && !p.a_timer);
    assert(timer_closes == 1 && restates == 1 && relinks == 2);
    pvt_on_remove_last_channel(&p);
    assert(timer_closes == 1);
}

static void test_disconnect_frees_all_channels(void)
{
    struct pvt p = { .chansno = 3 };
    struct cpvt **tail = &p.chans.first;
    disconnect_hangups = disconnect_releases = 0;
    for (int i = 0; i < 3; ++i) {
        *tail = calloc(1, sizeof(**tail));
        (*tail)->pvt = &p;
        (*tail)->call_idx = i;
        (*tail)->flags = CALL_FLAG_NEED_HANGUP;
        tail = &(*tail)->next;
    }
    pvt_disconnect(&p);
    assert(!p.chans.first && p.chansno == 0);
    assert(disconnect_hangups == 3 && disconnect_releases == 3);
}

int main(void)
{
    test_activation_and_handoff();
    test_start_failure_is_bounded();
    test_capture_recovery();
    test_playback_progress_and_recovery();
    test_last_channel_cleanup();
    test_disconnect_frees_all_channels();
    puts("Audio lifecycle regressions: 6 scenario groups passed");
    return 0;
}
