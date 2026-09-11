#include <assert.h>
#include <check.h>
#include <fcntl.h>
#include <unistd.h>

#include "beep/beeplib.h"

START_TEST(urlparse_test) {
    char scheme[URLPARSE_SCHEME_LEN];
    char host[URLPARSE_HOST_LEN];
    int port;
    char path[URLPARSE_PATH_LEN];

    // full url
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com:91/a/path", scheme, host,
                &port, path));
    ck_assert_str_eq("http", scheme);
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(91, port);
    ck_assert_str_eq("a/path", path);

    // no path, trailing slash.
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com:91/", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(91, port);
    ck_assert_str_eq("", path);

    // no path, no trailing slash.
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com:91", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(91, port);
    ck_assert_str_eq("", path);

    // no path, no port, trailing colon.
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com:91:", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(91, port);
    ck_assert_str_eq("", path);

    // no path, no port, trailing colon, trailing slash.
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com:/", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(80, port);
    ck_assert_str_eq("", path);

    // no port, includes path.
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com/a/path", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(80, port);
    ck_assert_str_eq("a/path", path);

    // no port, no path, trailing slash
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com/", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(80, port);
    ck_assert_str_eq("", path);

    // no port, no path
    ck_assert_int_eq(1,
            beep_urlparse("http://example.com", scheme, host,
                &port, path));
    ck_assert_str_eq("example.com", host);
    ck_assert_int_eq(80, port);
    ck_assert_str_eq("", path);

    // https
    ck_assert_int_eq(1,
            beep_urlparse("https://example.com", scheme, host,
                &port, path));
    ck_assert_str_eq("https", scheme);

    // invalid scheme
    ck_assert_int_eq(0,
            beep_urlparse("attps://example.com", scheme, host,
                &port, path));
}
END_TEST


Suite *beeplib_suite(void) {
    Suite *s = suite_create("beeplib");

    TCase *tc_core = tcase_create("core");

    tcase_add_test(tc_core, urlparse_test);

    suite_add_tcase(s, tc_core);

    return s;
}

void cleanup_beeplib_suite(void) {
}
