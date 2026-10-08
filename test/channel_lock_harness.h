#include <time.h>

/* Real pthread locks; the rendezvous replaces only the channel-yield timing. */
typedef pthread_mutex_t ast_mutex_t;
typedef int call_state_t;
struct device_list { pthread_rwlock_t lock; struct pvt *first; };
struct public_state { struct device_list devices; };
static struct public_state public_state;
static struct public_state *gpublic = &public_state;
#define AST_RWLIST_TRYRDLOCK(h) pthread_rwlock_tryrdlock(&(h)->lock)
#define AST_RWLIST_UNLOCK(h) pthread_rwlock_unlock(&(h)->lock)
#define AST_RWLIST_TRAVERSE(h, p, e) for ((p) = (h)->first; (p); (p) = (p)->next)
#define ast_mutex_trylock pthread_mutex_trylock
#define ast_mutex_unlock pthread_mutex_unlock

void pvt_unlock(struct pvt *pvt);
void cpvt_try_lock(struct cpvt *cpvt);
void cpvt_unlock(struct cpvt *cpvt);
static void cleanup_pvt(struct pvt *const *pvt) { pvt_unlock(*pvt); }
static void cleanup_cpvt(struct cpvt **cpvt) { cpvt_unlock(*cpvt); }
#define RAII_VAR(type, name, value, dtor) type name __attribute__((cleanup(cleanup_pvt))) = (value)
#define SCOPED_CPVT_TL(name, call) \
    struct cpvt *name __attribute__((cleanup(cleanup_cpvt))) = (cpvt_try_lock(call), (call))
static int activations;
static int at_enqueue_activate(struct cpvt *cpvt) { ++activations; return 0; }
static const char *call_state2str(call_state_t state) { return state == CALL_STATE_ACTIVE ? "active" : "held"; }
static void ast_copy_string(char *dst, const char *src, size_t length) { snprintf(dst, length, "%s", src); }

static pthread_mutex_t rendezvous_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rendezvous_cond = PTHREAD_COND_INITIALIZER;
static int waiting, proceed, yield_count;
static void wait_for_flag(int *flag, const char *phase)
{
    struct timespec deadline;
    assert(!clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += 5;
    while (!*flag) {
        int result = pthread_cond_timedwait(&rendezvous_cond, &rendezvous_lock, &deadline);
        if (result) fprintf(stderr, "Rendezvous failed during %s: %d; waiting=%d proceed=%d yields=%d\n",
                            phase, result, waiting, proceed, yield_count);
        assert(result == 0);
    }
}
static void channel_yield(struct ast_channel *channel)
{
    assert(!pthread_mutex_lock(&rendezvous_lock));
    ++yield_count;
    waiting = 1;
    assert(!pthread_cond_broadcast(&rendezvous_cond));
    wait_for_flag(&proceed, "callback resume");
    assert(!pthread_mutex_unlock(&rendezvous_lock));
}
#define CHANNEL_DEADLOCK_AVOIDANCE(channel) channel_yield(channel)

static struct ast_frame *channel_read(struct ast_channel *channel);
static int channel_write(struct ast_channel *channel, struct ast_frame *frame);
static int channel_func_read(struct ast_channel *channel, const char *function, char *data, char *buf, size_t len);
static int channel_func_write(struct ast_channel *channel, const char *function, char *data, const char *value);

enum callback { READ_AUDIO, WRITE_AUDIO, READ_STATE, WRITE_STATE };
struct invocation { struct ast_channel *channel; enum callback which; int result; char state[16]; const char *field, *value; };
static void *invoke(void *data)
{
    struct invocation *arg = data;
    switch (arg->which) {
    case READ_AUDIO:
        arg->result = channel_read(arg->channel) == &ast_null_frame ? 0 : 1;
        break;
    case WRITE_AUDIO: {
        struct ast_frame frame = { .frametype = AST_FRAME_NULL };
        arg->result = channel_write(arg->channel, &frame);
        break;
    }
    case READ_STATE:
        arg->result = channel_func_read(arg->channel, "CHANNEL", (char *)(arg->field ? arg->field : "callstate"), arg->state, sizeof(arg->state));
        break;
    case WRITE_STATE:
        arg->result = channel_func_write(arg->channel, "CHANNEL", (char *)(arg->field ? arg->field : "callstate"), arg->value ? arg->value : "active");
        break;
    }
    return NULL;
}

static void init_device(struct pvt *pvt, struct cpvt *call, struct ast_channel *channel)
{
    *pvt = (struct pvt) { .uac = 1, .audio_fd = -1, .chans.first = call, .chansno = 1 };
    *call = (struct cpvt) { .pvt = pvt, .channel = channel, .state = CALL_STATE_ONHOLD };
    *channel = (struct ast_channel) { .cpvt = call };
    pthread_mutexattr_t attr;
    assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&pvt->lock, &attr));
    assert(!pthread_mutexattr_destroy(&attr));
    public_state.devices.first = pvt;
    waiting = proceed = yield_count = activations = 0;
}

static void await_yield(void)
{
    assert(!pthread_mutex_lock(&rendezvous_lock));
    wait_for_flag(&waiting, "main awaiting channel yield");
    assert(!pthread_mutex_unlock(&rendezvous_lock));
}
static void resume_callback(void)
{
    assert(!pthread_mutex_lock(&rendezvous_lock));
    proceed = 1;
    assert(!pthread_cond_broadcast(&rendezvous_cond));
    assert(!pthread_mutex_unlock(&rendezvous_lock));
}
static void assert_released(struct pvt *pvt)
{
    int result = pthread_mutex_trylock(&pvt->lock);
    if (result) fprintf(stderr, "Device mutex orphaned after callback owner exited: %d\n", result);
    assert(result == 0);
    assert(!pthread_mutex_unlock(&pvt->lock));
    assert(!pthread_mutex_destroy(&pvt->lock));
}

static void test_call_change(enum callback which, int replacement)
{
    struct pvt pvt;
    struct cpvt old, next;
    struct ast_channel channel;
    init_device(&pvt, &old, &channel);
    assert(!pthread_mutex_lock(&pvt.lock));
    struct invocation arg = { .channel = &channel, .which = which };
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, invoke, &arg));
    await_yield();
    assert(!pthread_rwlock_trywrlock(&public_state.devices.lock));
    assert(!pthread_rwlock_unlock(&public_state.devices.lock));
    /* Model detach and deterministic reuse of the old allocation. Keeping
     * the storage makes the stale cleanup reproducible without relying on
     * a particular malloc free-list representation. */
    pvt.chans.first = NULL;
    channel.cpvt = NULL;
    old.pvt = NULL;
    old.channel = NULL;
    if (replacement) {
        next = (struct cpvt) { .pvt = &pvt, .channel = &channel, .state = CALL_STATE_ONHOLD };
        pvt.chans.first = &next;
        channel.cpvt = &next;
    }
    assert(!pthread_mutex_unlock(&pvt.lock));
    resume_callback();
    assert(!pthread_join(worker, NULL));
    assert_released(&pvt);
    assert(yield_count > 0);
    assert(arg.result == ((which >= READ_STATE && !replacement) ? -1 : 0));
    if (replacement && which == READ_STATE) assert(!strcmp(arg.state, "held"));
    if (which == WRITE_STATE) assert(activations == replacement);
}

static void test_delayed_acquisition(enum callback which)
{
    struct pvt pvt;
    struct cpvt call;
    struct ast_channel channel;
    init_device(&pvt, &call, &channel);
    assert(!pthread_mutex_lock(&pvt.lock));
    struct invocation arg = { .channel = &channel, .which = which };
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, invoke, &arg));
    await_yield();
    assert(!pthread_mutex_unlock(&pvt.lock));
    resume_callback();
    assert(!pthread_join(worker, NULL));
    assert(arg.result == 0);
    assert_released(&pvt);
}

static void test_device_removal(void)
{
    struct pvt *pvt = malloc(sizeof(*pvt));
    struct cpvt *call = malloc(sizeof(*call));
    struct ast_channel channel;
    init_device(pvt, call, &channel);
    assert(!pthread_rwlock_wrlock(&public_state.devices.lock));
    struct invocation arg = { .channel = &channel, .which = READ_STATE };
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, invoke, &arg));
    await_yield();
    public_state.devices.first = NULL;
    channel.cpvt = NULL;
    assert(!pthread_mutex_destroy(&pvt->lock));
    free(call);
    free(pvt);
    assert(!pthread_rwlock_unlock(&public_state.devices.lock));
    resume_callback();
    assert(!pthread_join(worker, NULL));
    assert(arg.result == -1);
}

static void test_absent_call_with_busy_device(void)
{
    struct pvt pvt;
    struct cpvt call;
    struct ast_channel other, missing = {0};
    init_device(&pvt, &call, &other);
    assert(!pthread_mutex_lock(&pvt.lock));
    struct invocation arg = { .channel = &missing, .which = READ_STATE };
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, invoke, &arg));
    await_yield();
    assert(!pthread_mutex_unlock(&pvt.lock));
    resume_callback();
    assert(!pthread_join(worker, NULL));
    assert(arg.result == -1);
    assert_released(&pvt);
}

static void test_early_returns(void)
{
    for (int scenario = 0; scenario < 7; ++scenario) {
        struct pvt pvt;
        struct cpvt call;
        struct ast_channel channel;
        init_device(&pvt, &call, &channel);
        struct invocation arg = { .channel = &channel };
        switch (scenario) {
        case 0: arg.which = READ_AUDIO; call.flags = CALL_FLAG_LOCAL_CHANNEL; break;
        case 1: arg.which = WRITE_AUDIO; call.flags = CALL_FLAG_LOCAL_CHANNEL; break;
        case 2: arg.which = WRITE_AUDIO; call.flags = CALL_FLAG_BRIDGE_LOOP; break;
        case 3: arg.which = READ_STATE; arg.field = "unknown"; break;
        case 4: arg.which = WRITE_STATE; arg.value = "unknown"; break;
        case 5: arg.which = WRITE_STATE; call.state = CALL_STATE_ACTIVE; break;
        case 6: arg.which = WRITE_STATE; call.state = CALL_STATE_RELEASED; break;
        }
        pthread_t worker;
        assert(!pthread_create(&worker, NULL, invoke, &arg));
        assert(!pthread_join(worker, NULL));
        assert(yield_count == 0);
        assert(arg.result == ((scenario == 3 || scenario == 4 || scenario == 6) ? -1 : 0));
        assert(activations == 0);
        assert_released(&pvt);
    }
}

static void test_detached_publication(void)
{
    struct pvt pvt;
    struct cpvt call;
    struct ast_channel channel;
    init_device(&pvt, &call, &channel);
    channel.cpvt = NULL;
    struct invocation arg = { .channel = &channel, .which = READ_STATE };
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, invoke, &arg));
    assert(!pthread_join(worker, NULL));
    assert(arg.result == -1 && yield_count == 0);
    assert_released(&pvt);
}

int main(void)
{
    assert(!pthread_rwlock_init(&public_state.devices.lock, NULL));
    for (enum callback callback = READ_AUDIO; callback <= WRITE_STATE; ++callback) {
        fprintf(stderr, "callback %d: detach\n", callback);
        test_call_change(callback, 0);
        fprintf(stderr, "callback %d: replacement\n", callback);
        test_call_change(callback, 1);
        fprintf(stderr, "callback %d: delayed acquisition\n", callback);
        test_delayed_acquisition(callback);
    }
    fprintf(stderr, "device removal\n");
    test_device_removal();
    fprintf(stderr, "absent call with busy device\n");
    test_absent_call_with_busy_device();
    fprintf(stderr, "early returns\n");
    test_early_returns();
    fprintf(stderr, "detached publication\n");
    test_detached_publication();
    assert(!pthread_rwlock_destroy(&public_state.devices.lock));
    puts("Channel lock regressions: 22 interleavings and callback paths passed");
    return 0;
}
