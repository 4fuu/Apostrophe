/*
 * pak.json reader and self-check contract tests.
 *
 * Host-only. Uses fixture files under tests/fixtures/pak_json/ and does not
 * link NextUI or any system overlay code.
 */

#define AP_PAK_IMPLEMENTATION
#include "apostrophe_pak.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    checks++; \
    const char *a_ = (actual); \
    const char *e_ = (expected); \
    if (!a_ || !e_ || strcmp(a_, e_) != 0) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s == %s (got %s)\n", \
                __FILE__, __LINE__, #actual, #expected, a_ ? a_ : "(null)"); \
    } \
} while (0)

static void test_defaults(void) {
    ap_pak_info info;
    ap_pak_info_init(&info, "fallback");
    CHECK_STR(info.id, "fallback");
    CHECK_STR(info.name, "fallback");
    CHECK_STR(info.version, "");
    CHECK_STR(info.min_system, "");
    CHECK(info.capability_count == 0);
    CHECK(!info.has_id && !info.has_name && !info.has_version && !info.has_min_system);
    CHECK(!info.capabilities_truncated);

    CHECK(ap_pak_min_system_ok(&info, NULL) == 1);
    CHECK(ap_pak_min_system_ok(NULL, "1.0") == -1);
    CHECK(!ap_pak_has_capability(&info, "saves"));
    CHECK(!ap_pak_has_capability(&info, ""));
    CHECK(!ap_pak_has_capability(NULL, "saves"));

    const char *missing = "sentinel";
    CHECK(ap_pak_check_capabilities(&info, NULL, 0, &missing) == 1);
    CHECK(missing == NULL);

    const char *wanted[] = { "saves" };
    CHECK(ap_pak_check_capabilities(&info, wanted, 1, &missing) == 0);
    CHECK_STR(missing, "saves");
    CHECK(ap_pak_check_capabilities(NULL, wanted, 1, &missing) == 0);
    CHECK_STR(missing, "saves");
    CHECK(ap_pak_check_capabilities(NULL, NULL, 0, &missing) == 1);

    ap_pak_info_init(&info, "");
    CHECK_STR(info.id, "unknown");
    CHECK_STR(info.name, "unknown");
    ap_pak_info_init(NULL, "ignored");
}

static void test_missing_file(void) {
    ap_pak_info info;
    CHECK(ap_pak_load("tests/fixtures/pak_json/does-not-exist", &info) == AP_ERROR);
    CHECK_STR(info.id, "does-not-exist");
    CHECK_STR(info.name, "does-not-exist");
    CHECK_STR(info.version, "");
    CHECK_STR(info.min_system, "");
    CHECK(info.capability_count == 0);
    CHECK(!info.has_id && !info.has_name);

    CHECK(ap_pak_load(NULL, &info) == AP_ERROR);
    CHECK_STR(info.id, "unknown");
    CHECK(ap_pak_load("", &info) == AP_ERROR);
    CHECK_STR(info.id, "unknown");

    CHECK(ap_pak_load_file(NULL, "fallback", &info) == AP_ERROR);
    CHECK_STR(info.id, "fallback");
    CHECK(ap_pak_parse(NULL, "fallback", &info) == AP_ERROR);
    CHECK_STR(info.id, "fallback");
}

static void test_full_manifest(void) {
    ap_pak_info info;
    CHECK(ap_pak_load("tests/fixtures/pak_json/full", &info) == AP_OK);
    CHECK_STR(info.id, "fixture.app");
    CHECK_STR(info.name, "Fixture App");
    CHECK_STR(info.version, "v1.4.2");
    CHECK_STR(info.min_system, "v6.15.0");
    CHECK(info.has_id && info.has_name && info.has_version && info.has_min_system);
    CHECK(info.capability_count == 2);
    CHECK(!info.capabilities_truncated);
    CHECK(ap_pak_has_capability(&info, "saves"));
    CHECK(ap_pak_has_capability(&info, "overlay"));
    CHECK(!ap_pak_has_capability(&info, "led.control"));
    CHECK(!ap_pak_has_capability(&info, "nested-name"));

    CHECK(ap_pak_min_system_ok(&info, "6.15.0") == 1);
    CHECK(ap_pak_min_system_ok(&info, "v6.15.0") == 1);
    CHECK(ap_pak_min_system_ok(&info, "6.15.1") == 1);
    CHECK(ap_pak_min_system_ok(&info, "6.16") == 1);
    CHECK(ap_pak_min_system_ok(&info, "v6.16.0-rc1") == 1);
    CHECK(ap_pak_min_system_ok(&info, "7.0.0") == 1);
    CHECK(ap_pak_min_system_ok(&info, "6.14.99") == 0);
    CHECK(ap_pak_min_system_ok(&info, "v6.14.0") == 0);
    CHECK(ap_pak_min_system_ok(&info, "6.15") == 1);
    CHECK(ap_pak_min_system_ok(&info, "6.15.0.1") == -1);
    CHECK(ap_pak_min_system_ok(&info, "banana") == -1);
    CHECK(ap_pak_min_system_ok(&info, NULL) == -1);

    ap_pak_info file_info;
    CHECK(ap_pak_load_file("tests/fixtures/pak_json/full/pak.json", "fallback", &file_info) == AP_OK);
    CHECK_STR(file_info.id, "fixture.app");
    CHECK_STR(file_info.name, "Fixture App");

    CHECK(ap_pak_load("tests/fixtures/pak_json/full/", &file_info) == AP_OK);
    CHECK_STR(file_info.id, "fixture.app");
}

static void test_minimal_manifest(void) {
    ap_pak_info info;
    CHECK(ap_pak_load("tests/fixtures/pak_json/minimal/", &info) == AP_OK);
    CHECK_STR(info.id, "minimal");
    CHECK_STR(info.name, "minimal");
    CHECK_STR(info.version, "");
    CHECK_STR(info.min_system, "");
    CHECK(info.capability_count == 0);
    CHECK(!info.has_id && !info.has_name && !info.has_version && !info.has_min_system);
    CHECK(ap_pak_min_system_ok(&info, NULL) == 1);
}

static void test_malformed_file(void) {
    ap_pak_info info;
    CHECK(ap_pak_load("tests/fixtures/pak_json/malformed", &info) == AP_ERROR);
    CHECK_STR(info.id, "malformed");
    CHECK_STR(info.name, "malformed");
    CHECK_STR(info.version, "");
    CHECK(!info.has_id && !info.has_name);
}

static void test_missing_and_wrong_fields(void) {
    ap_pak_info info;
    CHECK(ap_pak_parse("{\"type\":\"TOOL\"}", "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "fallback");
    CHECK_STR(info.name, "fallback");
    CHECK_STR(info.version, "");
    CHECK_STR(info.min_system, "");
    CHECK(info.capability_count == 0);
    CHECK(!info.has_id && !info.has_name);

    CHECK(ap_pak_parse("{\"id\":123,\"name\":false,\"version\":[\"1\"],"
                       "\"min_system\":null,\"capabilities\":\"saves\","
                       "\"device\":{\"id\":\"nested\",\"name\":\"nested\"},\"extra\":true}",
                       "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "fallback");
    CHECK_STR(info.name, "fallback");
    CHECK_STR(info.version, "");
    CHECK_STR(info.min_system, "");
    CHECK(info.capability_count == 0);
    CHECK(!info.has_id && !info.has_name && !info.has_version && !info.has_min_system);

    CHECK(ap_pak_parse("{\"id\":\"only.id\"}", "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "only.id");
    CHECK_STR(info.name, "only.id");
    CHECK(info.has_id);
    CHECK(!info.has_name);

    CHECK(ap_pak_parse("{\"id\":\"\",\"name\":\"\"}", "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "fallback");
    CHECK_STR(info.name, "fallback");
    CHECK(!info.has_id && !info.has_name);

    CHECK(ap_pak_parse("{}", NULL, &info) == AP_OK);
    CHECK_STR(info.id, "unknown");
    CHECK_STR(info.name, "unknown");
}

static void test_escapes(void) {
    ap_pak_info info;
    const char *json =
        "{\"id\":\"app\\\"one\",\"name\":\"\\u4f60\\u597d \\uD83D\\uDE00\\nLine\","
        "\"version\":\"1.0\",\"capabilities\":[\"sa\\u0076es\","
        "\"over\\u006cay\",\"over\\/lay\"]}";
    CHECK(ap_pak_parse(json, "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "app\"one");
    CHECK_STR(info.name, "\xE4\xBD\xA0\xE5\xA5\xBD \xF0\x9F\x98\x80\nLine");
    CHECK_STR(info.version, "1.0");
    CHECK(info.capability_count == 3);
    CHECK(ap_pak_has_capability(&info, "saves"));
    CHECK(ap_pak_has_capability(&info, "overlay"));
    CHECK(ap_pak_has_capability(&info, "over/lay"));
}

static void test_nested_values_ignored(void) {
    ap_pak_info info;
    const char *json =
        "{\"meta\":{\"name\":\"wrong\",\"capabilities\":[\"bad\"]},"
        "\"list\":[{\"id\":\"x\"},1,true,null],"
        "\"id\":\"real\",\"capabilities\":[\"saves\",1,null,\"overlay\"]}";
    CHECK(ap_pak_parse(json, "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "real");
    CHECK_STR(info.name, "real");
    CHECK(info.capability_count == 2);
    CHECK(ap_pak_has_capability(&info, "saves"));
    CHECK(ap_pak_has_capability(&info, "overlay"));
    CHECK(!ap_pak_has_capability(&info, "bad"));
}

static void test_duplicate_fields(void) {
    ap_pak_info info;
    CHECK(ap_pak_parse("{\"id\":\"a\",\"id\":\"b\","
                       "\"capabilities\":[\"saves\"],"
                       "\"capabilities\":[\"overlay\"]}",
                       "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "b");
    CHECK_STR(info.name, "b");
    CHECK(info.capability_count == 1);
    CHECK(ap_pak_has_capability(&info, "overlay"));
    CHECK(!ap_pak_has_capability(&info, "saves"));

    CHECK(ap_pak_parse("{\"capabilities\":[\"saves\"],\"capabilities\":null}",
                       "fallback", &info) == AP_OK);
    CHECK(info.capability_count == 0);
    CHECK(!ap_pak_has_capability(&info, "saves"));
}

static void test_capability_limit(void) {
    char json[1024];
    int written = snprintf(json, sizeof(json), "{\"capabilities\":[");
    CHECK(written > 0);
    for (int i = 0; i < AP_PAK_MAX_CAPABILITIES + 2; i++) {
        written += snprintf(json + written, sizeof(json) - (size_t)written,
                            "%s\"cap%d\"", i ? "," : "", i);
    }
    written += snprintf(json + written, sizeof(json) - (size_t)written, "]}");
    CHECK(written > 0 && (size_t)written < sizeof(json));

    ap_pak_info info;
    CHECK(ap_pak_parse(json, "fallback", &info) == AP_OK);
    CHECK(info.capability_count == AP_PAK_MAX_CAPABILITIES);
    CHECK(info.capabilities_truncated);
    CHECK(ap_pak_has_capability(&info, "cap0"));
    CHECK(ap_pak_has_capability(&info, "cap7"));
    CHECK(!ap_pak_has_capability(&info, "cap8"));
}

static void test_malformed_json(void) {
    const char *bad[] = {
        "",
        "{",
        "{\"id\":}",
        "{\"id\":\"x\",}",
        "{\"id\":\"x\"} trailing",
        "[]",
        "{\"id\":\"x\"",
        "{\"id\":\"x\",\"n\":01}",
        "{\"id\":\"x\\q\"}",
        "{\"id\":\"x\\uD83D\\uD\"}",
        "{\"id\":\"x\"} {}",
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ap_pak_info info;
        CHECK(ap_pak_parse(bad[i], "fallback", &info) == AP_ERROR);
        CHECK_STR(info.id, "fallback");
        CHECK_STR(info.name, "fallback");
        CHECK(!info.has_id && !info.has_name);
    }

    ap_pak_info info;
    CHECK(ap_pak_parse("\xEF\xBB\xBF{\"id\":\"bom\"}", "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "bom");
    CHECK(ap_pak_parse(" \r\n\t{ } \n", "fallback", &info) == AP_OK);
    CHECK_STR(info.id, "fallback");
}

static void test_min_system_table(void) {
    struct {
        const char *min;
        const char *system;
        int expected;
    } cases[] = {
        { "", "anything", 1 },
        { "", NULL, 1 },
        { "6.15.0", "6.15.0", 1 },
        { "v6.15.0", "6.15.0", 1 },
        { "6.15.0", "v6.15.0", 1 },
        { "6.15.0", "6.15", 1 },
        { "6.15", "6.15.0", 1 },
        { "6", "6.0.0", 1 },
        { "6.15.0", "6.14.99", 0 },
        { "6.15.0", "5.99.99", 0 },
        { "6.15.0", "7.0.0", 1 },
        { "6.15.0-rc1", "6.15.0", 1 },
        { "6.15.0", "6.15.0-rc1", 1 },
        { "6.15.0", "6.15.0.1", -1 },
        { "6.15.0.1", "6.15.0", -1 },
        { "6.15.0", "banana", -1 },
        { "banana", "6.15.0", -1 },
        { "6.15.0", NULL, -1 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ap_pak_info info;
        ap_pak_info_init(&info, "fallback");
        snprintf(info.min_system, sizeof(info.min_system), "%s", cases[i].min);
        int got = ap_pak_min_system_ok(&info, cases[i].system);
        checks++;
        if (got != cases[i].expected) {
            failures++;
            fprintf(stderr, "FAIL min=%s system=%s expected=%d got=%d\n",
                    cases[i].min, cases[i].system ? cases[i].system : "(null)",
                    cases[i].expected, got);
        }
    }
}

static void test_capability_checks(void) {
    ap_pak_info info;
    CHECK(ap_pak_parse("{\"capabilities\":[\"saves\",\"overlay\"]}", "fallback", &info) == AP_OK);

    const char *all[] = { "saves", "overlay" };
    const char *missing = "sentinel";
    CHECK(ap_pak_check_capabilities(&info, all, 2, &missing) == 1);
    CHECK(missing == NULL);

    const char *partial[] = { "saves", "led.control" };
    CHECK(ap_pak_check_capabilities(&info, partial, 2, &missing) == 0);
    CHECK_STR(missing, "led.control");

    const char *partial2[] = { "led.control", "saves" };
    CHECK(ap_pak_check_capabilities(&info, partial2, 2, NULL) == 0);

    CHECK(ap_pak_check_capabilities(&info, NULL, 0, &missing) == 1);
    CHECK(missing == NULL);
}

int main(void) {
    test_defaults();
    test_missing_file();
    test_full_manifest();
    test_minimal_manifest();
    test_malformed_file();
    test_missing_and_wrong_fields();
    test_escapes();
    test_nested_values_ignored();
    test_duplicate_fields();
    test_capability_limit();
    test_malformed_json();
    test_min_system_table();
    test_capability_checks();

    if (failures) {
        fprintf(stderr, "%d checks, %d failures\n", checks, failures);
        return 1;
    }
    printf("%d checks, 0 failures\n", checks);
    return 0;
}
