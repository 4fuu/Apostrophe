/* Host check for Apostrophe's build-time version macros and compile-time
 * compatibility checks. No implementation or display is needed. */
#include <stdio.h>
#include <string.h>

#include "apostrophe.h"

/* These must compile against the current headers. */
AP_VERSION_REQUIRE(1, 0, 0);
AP_VERSION_REQUIRE(1, 2, 0);
AP_VERSION_REQUIRE(AP_VERSION_MAJOR, AP_VERSION_MINOR, AP_VERSION_PATCH);

_Static_assert(AP_VERSION_AT_LEAST(AP_VERSION_MAJOR, AP_VERSION_MINOR,
                                   AP_VERSION_PATCH),
               "current version must satisfy itself");
_Static_assert(AP_VERSION_MAJOR < 1000 && AP_VERSION_MINOR < 1000 &&
               AP_VERSION_PATCH < 1000,
               "version components must stay below 1000 for decimal encoding");
_Static_assert(AP_VERSION_NUMBER ==
               AP_VERSION_ENCODE(AP_VERSION_MAJOR, AP_VERSION_MINOR,
                                 AP_VERSION_PATCH),
               "encoded version must match its components");
_Static_assert(!AP_VERSION_AT_LEAST(AP_VERSION_MAJOR, AP_VERSION_MINOR,
                                    AP_VERSION_PATCH + 1),
               "next patch must be newer than the current version");
_Static_assert(!AP_VERSION_AT_LEAST(999, 0, 0),
               "an unreleased major version must not satisfy the current one");

int main(void) {
    char expected[32];
    int n = snprintf(expected, sizeof(expected), "%d.%d.%d",
                     AP_VERSION_MAJOR, AP_VERSION_MINOR, AP_VERSION_PATCH);

    if (n <= 0 || (size_t)n >= sizeof(expected) ||
        strcmp(expected, AP_VERSION_STRING) != 0) {
        fprintf(stderr, "FAIL: AP_VERSION_STRING is \"%s\", expected \"%s\"\n",
                AP_VERSION_STRING, expected);
        return 1;
    }

    printf("PASS: Apostrophe %s (encoded %d)\n",
           AP_VERSION_STRING, AP_VERSION_NUMBER);
    return 0;
}
