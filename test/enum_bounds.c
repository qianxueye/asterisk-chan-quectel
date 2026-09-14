/* Exercise the production enum helper with the installed Asterisk macros. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "mutils.h"

int main(void)
{
    static const char* const names[] = {"first", NULL, "", "last"};
    static const char fallback[]     = "Unknown";
    const unsigned count             = sizeof(names) / sizeof(names[0]);
    const int invalid[]              = {-1, (int)count, INT_MIN, INT_MAX};

    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(enum2str_def(invalid[i], names, count, fallback) == fallback);
        assert(strcmp(enum2str(invalid[i], names, count), "unknown") == 0);
    }
    assert(enum2str_def(UINT_MAX, names, count, fallback) == fallback);
    assert(enum2str_def(0, names, count, fallback) == names[0]);
    assert(enum2str_def(count - 1, names, count, fallback) == names[count - 1]);
    assert(enum2str_def(1, names, count, fallback) == fallback);
    assert(enum2str_def(2, names, count, fallback) == fallback);
    /* An empty table must never be indexed, even for index zero. */
    assert(enum2str_def(0, NULL, 0, fallback) == fallback);
    puts("Enum bounds regression: invalid, boundary, empty-table and fallback cases passed");
    return 0;
}
