/* test_core_state.c — unit tests for cross-session state persistence API.
 *
 * Exercises src/core/state.c:
 *   - save/load round-trip with envelope format
 *   - multi-segment namespaces (extensions/<name>)
 *   - clear semantics
 *   - master / per-namespace kill switches
 *   - 1 MiB cap
 */

#include "../test_utils.h"
#include "../test_registry.h"
#include "state.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static void setup_isolated_cache(char *out_dir, size_t out_sz) {
    /* Build a unique per-process cache root under /tmp so tests never
     * touch the user's real $HOME/.cache. */
    int n = snprintf(out_dir, out_sz, "/tmp/muemacs-state-test-%d", (int)getpid());
    (void)n;
    (void)mkdir(out_dir, 0700);
    setenv("XDG_CACHE_HOME", out_dir, 1);
    /* Force re-resolution: state_get_dir caches once. Tests live in a
     * separate process, so a first use here is the first ever. */
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int rm_rf(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    return system(cmd);
}

static int test_state_save_load_roundtrip(void) {
    int ok = 1;
    PHASE_START("STATE: ROUNDTRIP", "save/load byte-for-byte round-trip");

    const char *payload = "hello, state!";
    size_t len = strlen(payload);

    if (state_save("core", "rt_test", payload, len) != 0) {
        LOG_ERRORF("[FAIL] state_save failed: %s", strerror(errno));
        ok = 0;
    }

    char buf[128] = { 0 };
    int n = state_load("core", "rt_test", buf, sizeof(buf));
    if (n != (int)len) {
        LOG_ERRORF("[FAIL] state_load returned %d, expected %zu", n, len);
        ok = 0;
    } else if (memcmp(buf, payload, len) != 0) {
        LOG_ERRORF("[FAIL] payload mismatch after round-trip");
        ok = 0;
    }

    (void)state_clear("core", "rt_test");

    PHASE_END("STATE: ROUNDTRIP", ok);
    return ok;
}

static int test_state_envelope_format(void) {
    int ok = 1;
    PHASE_START("STATE: ENVELOPE", "on-disk STT1 header format");

    const char *payload = "envelope-probe";
    size_t len = strlen(payload);
    if (state_save("core", "envelope", payload, len) != 0) {
        LOG_ERRORF("[FAIL] save failed");
        ok = 0;
        goto done;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/core/envelope.bin", state_get_dir());
    if (!file_exists(path)) {
        LOG_ERRORF("[FAIL] state file not present at %s", path);
        ok = 0;
        goto done;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERRORF("[FAIL] fopen failed: %s", strerror(errno));
        ok = 0;
        goto done;
    }
    unsigned char hdr[24];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        LOG_ERROR("[FAIL] short header read");
        fclose(f);
        ok = 0;
        goto done;
    }
    fclose(f);

    if (memcmp(hdr, "STT1", 4) != 0) {
        LOG_ERRORF("[FAIL] bad magic: %02x %02x %02x %02x",
                   hdr[0], hdr[1], hdr[2], hdr[3]);
        ok = 0;
    }
    uint32_t ver = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8)
                 | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    if (ver != 1) {
        LOG_ERRORF("[FAIL] version %u, expected 1", ver);
        ok = 0;
    }
    uint64_t plen = 0;
    for (int i = 0; i < 8; i++) plen |= ((uint64_t)hdr[8 + i]) << (i * 8);
    if (plen != (uint64_t)len) {
        LOG_ERRORF("[FAIL] payload_len %llu, expected %zu",
                   (unsigned long long)plen, len);
        ok = 0;
    }
    uint64_t rsv = 0;
    for (int i = 0; i < 8; i++) rsv |= ((uint64_t)hdr[16 + i]) << (i * 8);
    if (rsv != 0) {
        LOG_ERRORF("[FAIL] reserved field nonzero: %llu",
                   (unsigned long long)rsv);
        ok = 0;
    }

    (void)state_clear("core", "envelope");

done:
    PHASE_END("STATE: ENVELOPE", ok);
    return ok;
}

static int test_state_extension_namespace(void) {
    int ok = 1;
    PHASE_START("STATE: EXT_NS", "multi-segment namespace (extensions/<name>)");

    const char *payload = "c_evil-blob";
    size_t len = strlen(payload);

    /* This mirrors what api_state_save does internally. */
    if (state_save("extensions/c_evil", "dot-repeat", payload, len) != 0) {
        LOG_ERRORF("[FAIL] save under extensions/c_evil failed: %s",
                   strerror(errno));
        ok = 0;
        goto done;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/extensions/c_evil/dot-repeat.bin",
             state_get_dir());
    if (!file_exists(path)) {
        LOG_ERRORF("[FAIL] expected file absent: %s", path);
        ok = 0;
    }

    char buf[64] = { 0 };
    int n = state_load("extensions/c_evil", "dot-repeat", buf, sizeof(buf));
    if (n != (int)len || memcmp(buf, payload, len) != 0) {
        LOG_ERRORF("[FAIL] extension round-trip failed (n=%d)", n);
        ok = 0;
    }

    (void)state_clear("extensions/c_evil", "dot-repeat");

done:
    PHASE_END("STATE: EXT_NS", ok);
    return ok;
}

static int test_state_clear_semantics(void) {
    int ok = 1;
    PHASE_START("STATE: CLEAR", "clear removes file; load-missing returns -1");

    (void)state_save("core", "clear_probe", "x", 1);

    if (state_clear("core", "clear_probe") != 0) {
        LOG_ERROR("[FAIL] state_clear on present file returned non-zero");
        ok = 0;
    }

    char buf[4];
    if (state_load("core", "clear_probe", buf, sizeof(buf)) >= 0) {
        LOG_ERROR("[FAIL] state_load after clear returned success");
        ok = 0;
    }

    PHASE_END("STATE: CLEAR", ok);
    return ok;
}

static int test_state_master_kill_switch(void) {
    int ok = 1;
    PHASE_START("STATE: KILL_SWITCH", "[state] enabled=false => save no-op");

    /* Populate a baseline file first while enabled. */
    (void)state_save("core", "kill_probe", "baseline", 8);

    state_set_enabled(false);

    /* Save should silent no-op; existing file on disk is untouched since
     * we never rename a new tmp over it when disabled. */
    int rc = state_save("core", "kill_probe", "should-not-write", 16);
    if (rc != 0) {
        LOG_ERRORF("[FAIL] save while disabled returned %d (expected 0 silent)", rc);
        ok = 0;
    }

    /* Load should see master-disabled and return -1 (module skips read). */
    char buf[32];
    int rn = state_load("core", "kill_probe", buf, sizeof(buf));
    if (rn >= 0) {
        LOG_ERRORF("[FAIL] load while disabled returned %d (expected -1)", rn);
        ok = 0;
    }

    state_set_enabled(true);

    /* Baseline must still be readable after re-enabling. */
    rn = state_load("core", "kill_probe", buf, sizeof(buf));
    if (rn != 8 || memcmp(buf, "baseline", 8) != 0) {
        LOG_ERRORF("[FAIL] baseline gone after re-enable (rn=%d)", rn);
        ok = 0;
    }

    (void)state_clear("core", "kill_probe");

    PHASE_END("STATE: KILL_SWITCH", ok);
    return ok;
}

static int test_state_namespace_kill_switch(void) {
    int ok = 1;
    PHASE_START("STATE: NS_KILL", "per-namespace enable toggles");

    state_set_namespace_enabled("core/ns_test", false);

    /* Namespace-disabled — but the save API only checks the actual
     * `ns` argument ("core"), not the "core/ns_test" grouping flag.
     * state_core.c is responsible for consulting the grouping flag
     * before calling save. This test just verifies the API surface. */
    if (!state_namespace_enabled("core/ns_test")) {
        /* flag took — fine */
    } else {
        LOG_ERROR("[FAIL] namespace flag did not take");
        ok = 0;
    }

    state_set_namespace_enabled("core/ns_test", true);
    if (!state_namespace_enabled("core/ns_test")) {
        LOG_ERROR("[FAIL] namespace flag did not re-enable");
        ok = 0;
    }

    PHASE_END("STATE: NS_KILL", ok);
    return ok;
}

static int test_state_payload_cap(void) {
    int ok = 1;
    PHASE_START("STATE: CAP", "1 MiB payload cap rejects oversize");

    size_t too_big = STATE_MAX_PAYLOAD + 1;
    void *big = malloc(too_big);
    if (!big) {
        LOG_ERROR("[FAIL] OOM allocating probe buffer");
        ok = 0;
    } else {
        memset(big, 'A', too_big);
        int rc = state_save("core", "too_big", big, too_big);
        free(big);
        if (rc != -1) {
            LOG_ERRORF("[FAIL] oversize save returned %d (expected -1)", rc);
            ok = 0;
        }
    }

    PHASE_END("STATE: CAP", ok);
    return ok;
}

static int test_state_invalid_names(void) {
    int ok = 1;
    PHASE_START("STATE: NAMES", "invalid names rejected");

    /* Keys cannot contain slash. */
    if (state_save("core", "a/b", "x", 1) != -1) {
        LOG_ERROR("[FAIL] key with slash accepted");
        ok = 0;
    }
    /* Namespace may be nested (extensions/c_evil) but cannot have
     * leading dot components. */
    if (state_save(".hidden", "k", "x", 1) != -1) {
        LOG_ERROR("[FAIL] ns with leading dot accepted");
        ok = 0;
    }
    if (state_save("foo/.hidden", "k", "x", 1) != -1) {
        LOG_ERROR("[FAIL] ns segment with leading dot accepted");
        ok = 0;
    }
    if (state_save("foo//bar", "k", "x", 1) != -1) {
        LOG_ERROR("[FAIL] empty ns segment accepted");
        ok = 0;
    }

    PHASE_END("STATE: NAMES", ok);
    return ok;
}

int test_core_state(void) {
    char cache_root[256];
    setup_isolated_cache(cache_root, sizeof(cache_root));

    /* Master on — most tests need it; individual tests toggle as needed. */
    state_set_enabled(true);

    int ok = 1;
    ok &= test_state_save_load_roundtrip();
    ok &= test_state_envelope_format();
    ok &= test_state_extension_namespace();
    ok &= test_state_clear_semantics();
    ok &= test_state_master_kill_switch();
    ok &= test_state_namespace_kill_switch();
    ok &= test_state_payload_cap();
    ok &= test_state_invalid_names();

    /* Best-effort cleanup so /tmp doesn't accumulate. */
    (void)rm_rf(cache_root);

    return ok;
}
