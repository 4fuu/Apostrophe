/*
 * Apostrophe pak.json reader — optional, header-only.
 *
 * Reads the small, de-facto pak.json fields an Apostrophe application needs
 * for self-checks: id, name, version, min_system, and capabilities. It does
 * not install, update, validate a package, or read NextUI system files.
 *
 * Define AP_PAK_IMPLEMENTATION in exactly one .c file before including this
 * header. A normal Apostrophe application already has AP_IMPLEMENTATION for
 * apostrophe.h in that file (or in another linked file):
 *
 *     #define AP_IMPLEMENTATION
 *     #include "apostrophe.h"
 *     #define AP_PAK_IMPLEMENTATION
 *     #include "apostrophe_pak.h"
 *
 * Missing files, missing fields, wrong field types, and malformed JSON leave
 * safe defaults in ap_pak_info. The caller provides the system version for a
 * min_system check; this helper does not assume where the desktop stores it.
 *
 * License: MIT (same as Apostrophe).
 */

#ifndef APOSTROPHE_PAK_H
#define APOSTROPHE_PAK_H

#include "apostrophe.h"

#define AP_PAK_ID_MAX 64
#define AP_PAK_NAME_MAX 128
#define AP_PAK_VERSION_MAX 32
#define AP_PAK_MIN_SYSTEM_MAX 32
#define AP_PAK_MAX_CAPABILITIES 8
#define AP_PAK_CAPABILITY_MAX 32
#define AP_PAK_JSON_MAX (64 * 1024)
#define AP_PAK_PATH_MAX 1024

typedef struct {
    char id[AP_PAK_ID_MAX];
    char name[AP_PAK_NAME_MAX];
    char version[AP_PAK_VERSION_MAX];
    char min_system[AP_PAK_MIN_SYSTEM_MAX];
    char capabilities[AP_PAK_MAX_CAPABILITIES][AP_PAK_CAPABILITY_MAX];
    int  capability_count;
    bool has_id;
    bool has_name;
    bool has_version;
    bool has_min_system;
    bool capabilities_truncated;
} ap_pak_info;

/* Fill info with safe defaults. id and name use fallback_id (or "unknown");
 * version/min_system are empty and there are no capabilities. */
void ap_pak_info_init(ap_pak_info *info, const char *fallback_id);

/* Parse a complete JSON object. Returns AP_OK for a valid object (even when
 * fields are missing or have the wrong type) and AP_ERROR for malformed JSON.
 * On AP_ERROR, info is left with the safe defaults. */
int ap_pak_parse(const char *json, const char *fallback_id, ap_pak_info *info);

/* Read and parse one pak.json file. Missing/oversized/unreadable files return
 * AP_ERROR and leave info with the safe defaults. */
int ap_pak_load_file(const char *path, const char *fallback_id, ap_pak_info *info);

/* Read <pak_dir>/pak.json. The fallback id/name is the last path component of
 * pak_dir. A missing file returns AP_ERROR but still leaves that fallback in
 * info, so an app can continue with its directory name. */
int ap_pak_load(const char *pak_dir, ap_pak_info *info);

/* Compare info.min_system with a caller-supplied system version.
 * Returns 1 when compatible or when min_system is absent, 0 when the system is
 * older, and -1 when either version cannot be compared. Versions accept an
 * optional "v", one to three numeric components, and a semver suffix after
 * the core version. */
int ap_pak_min_system_ok(const ap_pak_info *info, const char *system_version);

/* Exact, case-sensitive capability lookup. Missing info, empty name, or an
 * absent capability returns 0. */
int ap_pak_has_capability(const ap_pak_info *info, const char *capability);

/* Return 1 when every required capability is present, 0 otherwise. When
 * missing_capability is non-NULL it receives the first missing name or NULL. */
int ap_pak_check_capabilities(const ap_pak_info *info,
                              const char *const *required,
                              int required_count,
                              const char **missing_capability);

#endif /* APOSTROPHE_PAK_H */

#ifdef AP_PAK_IMPLEMENTATION
#ifndef APOSTROPHE_PAK_IMPLEMENTED
#define APOSTROPHE_PAK_IMPLEMENTED

static bool ap__pak_is_sep(char c) {
    return c == '/' || c == '\\';
}

static void ap__pak_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t len = strlen(src);
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void ap__pak_basename(const char *path, char *out, size_t cap) {
    if (!path || !path[0]) {
        ap__pak_copy(out, cap, "unknown");
        return;
    }

    size_t len = strlen(path);
    while (len > 0 && ap__pak_is_sep(path[len - 1])) len--;
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (ap__pak_is_sep(path[i])) start = i + 1;
    }
    size_t name_len = len - start;
    if (name_len == 0) {
        ap__pak_copy(out, cap, "unknown");
        return;
    }
    if (name_len >= cap) name_len = cap - 1;
    memcpy(out, path + start, name_len);
    out[name_len] = '\0';
}

void ap_pak_info_init(ap_pak_info *info, const char *fallback_id) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    ap__pak_copy(info->id, sizeof(info->id),
                 (fallback_id && fallback_id[0]) ? fallback_id : "unknown");
    ap__pak_copy(info->name, sizeof(info->name), info->id);
}

static const char *ap__pak_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static int ap__pak_hex4(const char *p, uint32_t *out) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)p[i];
        uint32_t digit;
        if (c == '\0') return 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        value = (value << 4) | digit;
    }
    *out = value;
    return 1;
}

static void ap__pak_put(char *out, size_t cap, size_t *pos,
                        unsigned char byte, bool *truncated) {
    if (out && cap > 0 && *pos < cap - 1) {
        out[(*pos)++] = (char)byte;
    } else if (truncated) {
        *truncated = true;
    }
}

static void ap__pak_put_utf8(char *out, size_t cap, size_t *pos,
                             uint32_t cp, bool *truncated) {
    if (cp <= 0x7F) {
        ap__pak_put(out, cap, pos, (unsigned char)cp, truncated);
    } else if (cp <= 0x7FF) {
        ap__pak_put(out, cap, pos, (unsigned char)(0xC0 | (cp >> 6)), truncated);
        ap__pak_put(out, cap, pos, (unsigned char)(0x80 | (cp & 0x3F)), truncated);
    } else if (cp <= 0xFFFF) {
        ap__pak_put(out, cap, pos, (unsigned char)(0xE0 | (cp >> 12)), truncated);
        ap__pak_put(out, cap, pos, (unsigned char)(0x80 | ((cp >> 6) & 0x3F)), truncated);
        ap__pak_put(out, cap, pos, (unsigned char)(0x80 | (cp & 0x3F)), truncated);
    } else if (cp <= 0x10FFFF) {
        ap__pak_put(out, cap, pos, (unsigned char)(0xF0 | (cp >> 18)), truncated);
        ap__pak_put(out, cap, pos, (unsigned char)(0x80 | ((cp >> 12) & 0x3F)), truncated);
        ap__pak_put(out, cap, pos, (unsigned char)(0x80 | ((cp >> 6) & 0x3F)), truncated);
        ap__pak_put(out, cap, pos, (unsigned char)(0x80 | (cp & 0x3F)), truncated);
    } else {
        ap__pak_put(out, cap, pos, '?', truncated);
    }
}

static bool ap__pak_parse_string(const char **pp, char *out, size_t cap,
                                 bool *truncated) {
    const char *p = ap__pak_skip_ws(*pp);
    if (*p != '"') return false;
    p++;

    size_t pos = 0;
    if (out && cap > 0) out[0] = '\0';
    if (truncated) *truncated = false;

    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            if (out && cap > 0) out[pos < cap ? pos : cap - 1] = '\0';
            *pp = p + 1;
            return true;
        }
        if (c == '\\') {
            p++;
            switch (*p) {
                case '"':  ap__pak_put(out, cap, &pos, '"', truncated);  p++; break;
                case '\\': ap__pak_put(out, cap, &pos, '\\', truncated); p++; break;
                case '/':  ap__pak_put(out, cap, &pos, '/', truncated);  p++; break;
                case 'b':  ap__pak_put(out, cap, &pos, '\b', truncated); p++; break;
                case 'f':  ap__pak_put(out, cap, &pos, '\f', truncated); p++; break;
                case 'n':  ap__pak_put(out, cap, &pos, '\n', truncated); p++; break;
                case 'r':  ap__pak_put(out, cap, &pos, '\r', truncated); p++; break;
                case 't':  ap__pak_put(out, cap, &pos, '\t', truncated); p++; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!ap__pak_hex4(p + 1, &cp)) return false;
                    p += 5;

                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t low = 0;
                        if (p[0] == '\\' && p[1] == 'u' &&
                            ap__pak_hex4(p + 2, &low) &&
                            low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            p += 6;
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }

                    ap__pak_put_utf8(out, cap, &pos, cp, truncated);
                    break;
                }
                default:
                    return false;
            }
            continue;
        }
        if (c < 0x20) return false;
        ap__pak_put(out, cap, &pos, c, truncated);
        p++;
    }
    return false;
}

static bool ap__pak_skip_value(const char **pp, int depth);

static bool ap__pak_skip_object(const char **pp, int depth) {
    const char *p = *pp;
    if (*p != '{') return false;
    p = ap__pak_skip_ws(p + 1);
    if (*p == '}') {
        *pp = p + 1;
        return true;
    }

    while (*p) {
        if (!ap__pak_parse_string(&p, NULL, 0, NULL)) return false;
        p = ap__pak_skip_ws(p);
        if (*p != ':') return false;
        p = ap__pak_skip_ws(p + 1);
        if (!ap__pak_skip_value(&p, depth + 1)) return false;
        p = ap__pak_skip_ws(p);
        if (*p == ',') {
            p = ap__pak_skip_ws(p + 1);
            continue;
        }
        if (*p == '}') {
            *pp = p + 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool ap__pak_skip_array(const char **pp, int depth) {
    const char *p = *pp;
    if (*p != '[') return false;
    p = ap__pak_skip_ws(p + 1);
    if (*p == ']') {
        *pp = p + 1;
        return true;
    }

    while (*p) {
        if (!ap__pak_skip_value(&p, depth + 1)) return false;
        p = ap__pak_skip_ws(p);
        if (*p == ',') {
            p = ap__pak_skip_ws(p + 1);
            continue;
        }
        if (*p == ']') {
            *pp = p + 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool ap__pak_skip_number(const char **pp) {
    const char *p = *pp;
    if (*p == '-') p++;
    if (*p == '0') {
        p++;
    } else if (*p >= '1' && *p <= '9') {
        while (*p >= '0' && *p <= '9') p++;
    } else {
        return false;
    }

    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') p++;
    }
    *pp = p;
    return true;
}

static bool ap__pak_skip_value(const char **pp, int depth) {
    if (depth > 32) return false;
    const char *p = ap__pak_skip_ws(*pp);

    if (*p == '"') return ap__pak_parse_string(pp, NULL, 0, NULL);
    if (*p == '{') return ap__pak_skip_object(pp, depth);
    if (*p == '[') return ap__pak_skip_array(pp, depth);
    if (strncmp(p, "true", 4) == 0) { *pp = p + 4; return true; }
    if (strncmp(p, "false", 5) == 0) { *pp = p + 5; return true; }
    if (strncmp(p, "null", 4) == 0) { *pp = p + 4; return true; }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        if (!ap__pak_skip_number(&p)) return false;
        *pp = p;
        return true;
    }
    return false;
}

static bool ap__pak_add_capability(ap_pak_info *info, const char *capability) {
    if (!capability || !capability[0]) return true;
    if (info->capability_count >= AP_PAK_MAX_CAPABILITIES) {
        info->capabilities_truncated = true;
        return true;
    }
    ap__pak_copy(info->capabilities[info->capability_count],
                 AP_PAK_CAPABILITY_MAX, capability);
    info->capability_count++;
    return true;
}

static bool ap__pak_parse_capabilities(const char **pp, ap_pak_info *info) {
    const char *p = *pp;
    if (*p != '[') return false;
    p = ap__pak_skip_ws(p + 1);

    info->capability_count = 0;
    info->capabilities_truncated = false;
    if (*p == ']') {
        *pp = p + 1;
        return true;
    }

    while (*p) {
        if (*p == '"') {
            char capability[AP_PAK_CAPABILITY_MAX];
            if (!ap__pak_parse_string(&p, capability, sizeof(capability), NULL))
                return false;
            ap__pak_add_capability(info, capability);
        } else {
            if (!ap__pak_skip_value(&p, 1)) return false;
        }

        p = ap__pak_skip_ws(p);
        if (*p == ',') {
            p = ap__pak_skip_ws(p + 1);
            continue;
        }
        if (*p == ']') {
            *pp = p + 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool ap__pak_string_field(const char **pp, char *out, size_t cap, bool *has) {
    const char *p = ap__pak_skip_ws(*pp);
    if (*p != '"') return ap__pak_skip_value(pp, 1);

    if (!ap__pak_parse_string(pp, out, cap, NULL)) return false;
    *has = true;
    return true;
}

static bool ap__pak_parse_root(const char *json, ap_pak_info *info) {
    const char *p = json;
    if ((unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) {
        p += 3;
    }
    p = ap__pak_skip_ws(p);
    if (*p != '{') return false;
    p = ap__pak_skip_ws(p + 1);
    if (*p == '}') {
        p = ap__pak_skip_ws(p + 1);
        return *p == '\0';
    }

    while (*p) {
        char key[64];
        if (!ap__pak_parse_string(&p, key, sizeof(key), NULL)) return false;
        p = ap__pak_skip_ws(p);
        if (*p != ':') return false;
        p = ap__pak_skip_ws(p + 1);

        if (strcmp(key, "id") == 0) {
            if (!ap__pak_string_field(&p, info->id, sizeof(info->id), &info->has_id))
                return false;
        } else if (strcmp(key, "name") == 0) {
            if (!ap__pak_string_field(&p, info->name, sizeof(info->name), &info->has_name))
                return false;
        } else if (strcmp(key, "version") == 0) {
            if (!ap__pak_string_field(&p, info->version, sizeof(info->version), &info->has_version))
                return false;
        } else if (strcmp(key, "min_system") == 0) {
            if (!ap__pak_string_field(&p, info->min_system, sizeof(info->min_system),
                                      &info->has_min_system))
                return false;
        } else if (strcmp(key, "capabilities") == 0) {
            info->capability_count = 0;
            info->capabilities_truncated = false;
            if (*p == '[') {
                if (!ap__pak_parse_capabilities(&p, info)) return false;
            } else if (!ap__pak_skip_value(&p, 1)) {
                return false;
            }
        } else {
            if (!ap__pak_skip_value(&p, 1)) return false;
        }

        p = ap__pak_skip_ws(p);
        if (*p == ',') {
            p = ap__pak_skip_ws(p + 1);
            continue;
        }
        if (*p == '}') {
            p = ap__pak_skip_ws(p + 1);
            return *p == '\0';
        }
        return false;
    }
    return false;
}

int ap_pak_parse(const char *json, const char *fallback_id, ap_pak_info *info) {
    if (!info) return AP_ERROR;
    ap_pak_info_init(info, fallback_id);
    if (!json) return AP_ERROR;

    ap_pak_info parsed;
    ap_pak_info_init(&parsed, fallback_id);
    if (!ap__pak_parse_root(json, &parsed)) return AP_ERROR;

    if (parsed.has_id && !parsed.id[0]) {
        ap__pak_copy(parsed.id, sizeof(parsed.id),
                     (fallback_id && fallback_id[0]) ? fallback_id : "unknown");
        parsed.has_id = false;
    }
    if (parsed.has_name && !parsed.name[0]) {
        ap__pak_copy(parsed.name, sizeof(parsed.name), parsed.id);
        parsed.has_name = false;
    }
    if (!parsed.has_name)
        ap__pak_copy(parsed.name, sizeof(parsed.name), parsed.id);

    *info = parsed;
    return AP_OK;
}

int ap_pak_load_file(const char *path, const char *fallback_id, ap_pak_info *info) {
    if (!info) return AP_ERROR;
    ap_pak_info_init(info, fallback_id);
    if (!path || !path[0]) return AP_ERROR;

    FILE *fp = fopen(path, "rb");
    if (!fp) return AP_ERROR;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return AP_ERROR;
    }
    long size = ftell(fp);
    if (size <= 0 || size > AP_PAK_JSON_MAX) {
        fclose(fp);
        return AP_ERROR;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return AP_ERROR;
    }

    char *json = (char *)malloc((size_t)size + 1);
    if (!json) {
        fclose(fp);
        return AP_ERROR;
    }
    size_t read = fread(json, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) {
        free(json);
        return AP_ERROR;
    }
    json[size] = '\0';
    if (strlen(json) != (size_t)size) {
        free(json);
        return AP_ERROR;
    }

    int rc = ap_pak_parse(json, fallback_id, info);
    free(json);
    return rc;
}

int ap_pak_load(const char *pak_dir, ap_pak_info *info) {
    if (!info) return AP_ERROR;

    char fallback[AP_PAK_ID_MAX];
    ap__pak_basename(pak_dir, fallback, sizeof(fallback));
    ap_pak_info_init(info, fallback);
    if (!pak_dir || !pak_dir[0]) return AP_ERROR;

    char path[AP_PAK_PATH_MAX];
    size_t len = strlen(pak_dir);
    bool needs_sep = len > 0 && !ap__pak_is_sep(pak_dir[len - 1]);
    int written = snprintf(path, sizeof(path), "%s%spak.json",
                           pak_dir, needs_sep ? "/" : "");
    if (written < 0 || (size_t)written >= sizeof(path)) return AP_ERROR;

    return ap_pak_load_file(path, fallback, info);
}

static bool ap__pak_version_parse(const char *version, int *major, int *minor, int *patch) {
    if (!version) return false;
    const char *p = ap__pak_skip_ws(version);
    if (*p == 'v' || *p == 'V') p++;
    if (*p < '0' || *p > '9') return false;

    int parts[3] = {0, 0, 0};
    int count = 0;
    while (count < 3) {
        long value = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            if (value > 100000000L) return false;
            p++;
        }
        parts[count++] = (int)value;
        if (*p == '.') {
            p++;
            if (*p < '0' || *p > '9') return false;
            continue;
        }
        break;
    }
    if (*p == '.') return false; /* more than three components */

    if (*p == '-' || *p == '+') {
        p++;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    }

    p = ap__pak_skip_ws(p);
    if (*p != '\0') return false;

    *major = parts[0];
    *minor = parts[1];
    *patch = parts[2];
    return true;
}

int ap_pak_min_system_ok(const ap_pak_info *info, const char *system_version) {
    if (!info) return -1;
    if (!info->min_system[0]) return 1;
    if (!system_version || !system_version[0]) return -1;

    int required_major, required_minor, required_patch;
    int current_major, current_minor, current_patch;
    if (!ap__pak_version_parse(info->min_system,
                               &required_major, &required_minor, &required_patch) ||
        !ap__pak_version_parse(system_version,
                               &current_major, &current_minor, &current_patch)) {
        return -1;
    }

    if (current_major != required_major) return current_major > required_major ? 1 : 0;
    if (current_minor != required_minor) return current_minor > required_minor ? 1 : 0;
    return current_patch >= required_patch ? 1 : 0;
}

int ap_pak_has_capability(const ap_pak_info *info, const char *capability) {
    if (!info || !capability || !capability[0]) return 0;
    for (int i = 0; i < info->capability_count; i++) {
        if (strcmp(info->capabilities[i], capability) == 0) return 1;
    }
    return 0;
}

int ap_pak_check_capabilities(const ap_pak_info *info,
                              const char *const *required,
                              int required_count,
                              const char **missing_capability) {
    if (missing_capability) *missing_capability = NULL;
    if (required_count <= 0) return 1;
    if (!required) return 0;
    if (!info) {
        if (missing_capability) *missing_capability = required[0];
        return 0;
    }
    for (int i = 0; i < required_count; i++) {
        if (!ap_pak_has_capability(info, required[i])) {
            if (missing_capability) *missing_capability = required[i];
            return 0;
        }
    }
    return 1;
}

#endif /* APOSTROPHE_PAK_IMPLEMENTED */
#endif /* AP_PAK_IMPLEMENTATION */
