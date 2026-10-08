# Channel callback lock lifetime

The media read/write and channel-function read/write callbacks must find their
call object while its device mutex is held. They must not retain a call or device
pointer across `CHANNEL_DEADLOCK_AVOIDANCE`: that operation releases the Asterisk
channel lock, and modem teardown can detach and free the call. Device removal can
then free the device as well.

`channel_lock_pvt` uses the device-list read lock to keep devices alive while it
tries each device mutex. It reads the call list only under that mutex and checks
both the call's channel and the channel's published private pointer. On success,
the acquired device mutex prevents device removal after the list lock is
released. The callback releases that mutex directly through `pvt_unlock`.

Both list and device acquisition use try-locks. Before yielding the channel lock,
the helper releases every list/device lock and discards all candidate pointers.
It then searches again. Blocking on the list lock while holding the channel lock
would conflict with device destruction, which can wait for monitor teardown and
channel hangup while holding the list write lock.

If no matching call exists but an unrelated device is busy, the search retries
until it can rule out that device. This can delay an unreferenced-channel result.
The change covers the four callbacks that previously used `SCOPED_CPVT_TL`; it
does not redesign the other channel callbacks or the plugin's full shutdown
protocol.

## Regression

Run `python3 test/test_channel_lock.py`. The test compiles the production helper
and all four production callbacks with pthread recursive mutexes and controlled
thread scheduling. It verifies call detach/replacement during a wait, delayed
acquisition, list-writer contention followed by actual device deallocation,
publication mismatch, and cleanup on early returns. The media dependencies use
the existing audio lifecycle fixture. No modem or running Asterisk is required.

The detach fixture explicitly reuses the retired call storage to make stale
cleanup deterministic. Against the old source, the callback thread exits while
the original device mutex remains busy. To reproduce against a separate source
checkout, run `python3 test/test_channel_lock.py --source-dir /path/to/old/source`.

For memory and undefined-behavior checks, run:

```sh
LOCK_TEST_CFLAGS='-fsanitize=address,undefined -g' python3 test/test_channel_lock.py
```

CTest and the existing GitHub build workflow also run the regression. The test
proves this lock-lifetime failure and its fix; an exited production thread's
stack cannot be reconstructed from the later orphaned-mutex snapshot alone.
