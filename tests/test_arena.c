/*
 * test_arena.c - dependency-free test suite for arena.h / arena.c
 *
 * Build (MSVC, from "x64 Native Tools Command Prompt for VS"):
 *     cl /W4 /std:c11 test_arena.c arena.c /Fe:test_arena.exe
 *     cl /W4 /std:c11 /fsanitize=address /Zi test_arena.c arena.c   (with ASan)
 *
 * Build (MinGW-w64 / MSYS2):
 *     gcc -Wall -Wextra -std=c11 -g test_arena.c arena.c -o test_arena.exe
 *
 * To also run the alignment tests once you add arena_alloc_aligned():
 *     add /DARENA_HAS_ALIGNED (MSVC) or -DARENA_HAS_ALIGNED (gcc)
 *     and declare it in arena.h:
 *         void *arena_alloc_aligned(arena_t *arena, size_t size, size_t align);
 *
 * Exit code is 0 if every test passes, 1 otherwise (CI friendly).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "arena.h"

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                  */
/* ------------------------------------------------------------------ */

static int g_tests_run = 0;
static int g_tests_failed = 0;
static int g_cur_failures = 0;

/* Records a failure and continues the test. */
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_cur_failures++;                                             \
        }                                                                 \
    } while (0)

/* Records a failure and aborts the current test (use for preconditions). */
#define REQUIRE(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("    FAIL %s:%d: %s (required)\n",                     \
                   __FILE__, __LINE__, #cond);                            \
            g_cur_failures++;                                             \
            return;                                                       \
        }                                                                 \
    } while (0)

#define RUN_TEST(fn)                                                      \
    do {                                                                  \
        g_cur_failures = 0;                                               \
        g_tests_run++;                                                    \
        fn();                                                             \
        if (g_cur_failures == 0) {                                        \
            printf("[ PASS ] %s\n", #fn);                                 \
        } else {                                                          \
            printf("[ FAIL ] %s\n", #fn);                                 \
            g_tests_failed++;                                             \
        }                                                                 \
    } while (0)

/* ------------------------------------------------------------------ */
/* Init / free                                                        */
/* ------------------------------------------------------------------ */

static void test_init_sets_fields(void) {
    arena_t *a = arena_init(1024);
    REQUIRE(a != NULL);
    CHECK(a->buffer != NULL);
    CHECK(a->capacity == 1024);
    CHECK(a->offset == 0);
    arena_free(a);
}

static void test_init_impossible_size_returns_null(void) {
#ifdef __SANITIZE_ADDRESS__
    /* ASan aborts on absurd malloc sizes instead of returning NULL. */
    printf("    (skipped under AddressSanitizer)\n");
#else
    /* malloc(SIZE_MAX) must fail; init should report it, not crash/leak. */
    arena_t *a = arena_init(SIZE_MAX);
    CHECK(a == NULL);
    if (a) arena_free(a);
#endif
}

static void test_free_null_is_safe(void) {
    arena_free(NULL);
    CHECK(1); /* reaching here without crashing is the test */
}

/* ------------------------------------------------------------------ */
/* Basic allocation                                                   */
/* ------------------------------------------------------------------ */

static void test_first_alloc_returns_buffer_start(void) {
    arena_t *a = arena_init(128);
    REQUIRE(a != NULL);

    void *p = arena_alloc(a, 16);
    CHECK(p == (void *)a->buffer);
    CHECK(a->offset == 16);

    arena_free(a);
}

static void test_sequential_allocs_are_contiguous(void) {
    arena_t *a = arena_init(128);
    REQUIRE(a != NULL);

    uint8_t *p1 = (uint8_t *)arena_alloc(a, 10);
    uint8_t *p2 = (uint8_t *)arena_alloc(a, 20);
    uint8_t *p3 = (uint8_t *)arena_alloc(a, 5);
    REQUIRE(p1 && p2 && p3);

    CHECK(p2 == p1 + 10);
    CHECK(p3 == p2 + 20);
    CHECK(a->offset == 35);

    arena_free(a);
}

static void test_allocations_stay_inside_buffer(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    uint8_t *lo = a->buffer;
    uint8_t *hi = a->buffer + a->capacity;

    for (int i = 0; i < 8; i++) {
        uint8_t *p = (uint8_t *)arena_alloc(a, 8);
        REQUIRE(p != NULL);
        CHECK(p >= lo);
        CHECK(p + 8 <= hi);
    }

    arena_free(a);
}

static void test_data_integrity_across_allocations(void) {
    arena_t *a = arena_init(256);
    REQUIRE(a != NULL);

    size_t sizes[4] = { 16, 32, 64, 8 };
    uint8_t *blocks[4];

    for (int i = 0; i < 4; i++) {
        blocks[i] = (uint8_t *)arena_alloc(a, sizes[i]);
        REQUIRE(blocks[i] != NULL);
        memset(blocks[i], 0xA0 + i, sizes[i]);
    }

    /* Writing to later blocks must not have clobbered earlier ones. */
    for (int i = 0; i < 4; i++) {
        int intact = 1;
        for (size_t j = 0; j < sizes[i]; j++) {
            if (blocks[i][j] != (uint8_t)(0xA0 + i)) { intact = 0; break; }
        }
        CHECK(intact);
    }

    arena_free(a);
}

static void test_can_store_typed_data(void) {
    arena_t *a = arena_init(256);
    REQUIRE(a != NULL);

    /* Allocate the widest type first so it lands on the (malloc-aligned)
       base address; this test is about storage, not alignment. */
    double *d = (double *)arena_alloc(a, sizeof(double) * 4);
    REQUIRE(d != NULL);
    for (int i = 0; i < 4; i++) d[i] = i * 1.5;
    for (int i = 0; i < 4; i++) CHECK(d[i] == i * 1.5);

    arena_free(a);
}

/* ------------------------------------------------------------------ */
/* Capacity limits                                                    */
/* ------------------------------------------------------------------ */

static void test_exact_fit_succeeds_then_next_fails(void) {
    arena_t *a = arena_init(100);
    REQUIRE(a != NULL);

    CHECK(arena_alloc(a, 100) != NULL);
    CHECK(a->offset == 100);
    CHECK(arena_alloc(a, 1) == NULL);
    CHECK(a->offset == 100); /* failed alloc must not move the cursor */

    arena_free(a);
}

static void test_one_byte_over_capacity_fails(void) {
    arena_t *a = arena_init(100);
    REQUIRE(a != NULL);

    CHECK(arena_alloc(a, 101) == NULL);
    CHECK(a->offset == 0);

    arena_free(a);
}

static void test_failed_alloc_does_not_poison_arena(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 60) != NULL);
    CHECK(arena_alloc(a, 10) == NULL);   /* too big for the remaining 4 */
    CHECK(arena_alloc(a, 4) != NULL);    /* but a smaller one still fits */
    CHECK(a->offset == 64);

    arena_free(a);
}

static void test_size_max_does_not_overflow(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    /* A naive (offset + size > capacity) check wraps around here. */
    REQUIRE(arena_alloc(a, 8) != NULL);
    CHECK(arena_alloc(a, SIZE_MAX) == NULL);
    CHECK(arena_alloc(a, SIZE_MAX - 4) == NULL);
    CHECK(arena_alloc(a, SIZE_MAX - 8) == NULL);
    CHECK(a->offset == 8);

    arena_free(a);
}

static void test_fill_arena_with_single_byte_allocs(void) {
    arena_t *a = arena_init(256);
    REQUIRE(a != NULL);

    for (int i = 0; i < 256; i++) {
        uint8_t *p = (uint8_t *)arena_alloc(a, 1);
        REQUIRE(p != NULL);
        CHECK(p == a->buffer + i);
    }
    CHECK(arena_alloc(a, 1) == NULL);

    arena_free(a);
}

/* ------------------------------------------------------------------ */
/* Edge cases                                                         */
/* ------------------------------------------------------------------ */

static void test_zero_size_alloc_does_not_advance(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 8) != NULL);
    size_t before = a->offset;

    void *p = arena_alloc(a, 0);
    CHECK(p != NULL);              /* documents current behavior */
    CHECK(a->offset == before);

    arena_free(a);
}

static void test_zero_size_alloc_on_full_arena(void) {
    arena_t *a = arena_init(16);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 16) != NULL);
    CHECK(arena_alloc(a, 0) != NULL);  /* 0 <= remaining (0), so it succeeds */

    arena_free(a);
}

static void test_null_arena_is_handled(void) {
    CHECK(arena_alloc(NULL, 16) == NULL);
    arena_reset(NULL);
    CHECK(1); /* no crash */
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */

static void test_reset_returns_offset_to_zero(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 40) != NULL);
    arena_reset(a);
    CHECK(a->offset == 0);
    CHECK(a->capacity == 64);      /* capacity must be untouched */
    CHECK(a->buffer != NULL);

    arena_free(a);
}

static void test_reset_reuses_same_memory(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    void *first = arena_alloc(a, 32);
    REQUIRE(first != NULL);
    arena_reset(a);
    void *second = arena_alloc(a, 32);

    CHECK(first == second);

    arena_free(a);
}

static void test_full_capacity_available_after_reset(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 64) != NULL);
    CHECK(arena_alloc(a, 1) == NULL);

    arena_reset(a);
    CHECK(arena_alloc(a, 64) != NULL);

    arena_free(a);
}

static void test_repeated_reset_cycles(void) {
    arena_t *a = arena_init(128);
    REQUIRE(a != NULL);

    for (int cycle = 0; cycle < 1000; cycle++) {
        uint8_t *p = (uint8_t *)arena_alloc(a, 100);
        REQUIRE(p != NULL);
        memset(p, cycle & 0xFF, 100);
        arena_reset(a);
    }
    CHECK(a->offset == 0);

    arena_free(a);
}

/* ------------------------------------------------------------------ */
/* Randomized stress test                                             */
/* ------------------------------------------------------------------ */

static void test_random_alloc_stress(void) {
    const size_t CAP = 4096;
    arena_t *a = arena_init(CAP);
    REQUIRE(a != NULL);

    /* Small LCG so results are identical on every compiler/platform
       (rand() differs between MSVC and glibc). */
    uint32_t state = 12345u;
    size_t expected = 0;

    for (int round = 0; round < 50; round++) {
        expected = 0;
        arena_reset(a);

        for (;;) {
            state = state * 1664525u + 1013904223u;
            size_t sz = (size_t)((state >> 16) % 97) + 1; /* 1..97 */

            uint8_t *p = (uint8_t *)arena_alloc(a, sz);
            if (expected + sz > CAP) {
                CHECK(p == NULL);
                break;
            }
            REQUIRE(p != NULL);
            CHECK(p == a->buffer + expected);  /* contiguous, no overlap */
            memset(p, (int)(sz & 0xFF), sz);
            expected += sz;
            CHECK(a->offset == expected);
        }
    }

    arena_free(a);
}

/* ------------------------------------------------------------------ */
/* Alignment (enabled once arena_alloc_aligned exists)                */
/* ------------------------------------------------------------------ */

#ifdef ARENA_HAS_ALIGNED

static void test_aligned_results_are_aligned(void) {
    static const size_t aligns[] = { 1, 2, 4, 8, 16, 32, 64 };
    arena_t *a = arena_init(4096);
    REQUIRE(a != NULL);

    for (size_t i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        /* Knock the cursor off-alignment first. */
        REQUIRE(arena_alloc(a, 1) != NULL);

        void *p = arena_alloc_aligned(a, 24, aligns[i]);
        REQUIRE(p != NULL);
        CHECK(((uintptr_t)p % aligns[i]) == 0);
    }

    arena_free(a);
}

static void test_aligned_rejects_bad_alignment(void) {
    arena_t *a = arena_init(256);
    REQUIRE(a != NULL);

    CHECK(arena_alloc_aligned(a, 8, 0) == NULL);   /* zero          */
    CHECK(arena_alloc_aligned(a, 8, 3) == NULL);   /* not power of 2 */
    CHECK(arena_alloc_aligned(a, 8, 12) == NULL);
    CHECK(a->offset == 0);

    arena_free(a);
}

static void test_aligned_padding_counts_toward_capacity(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 1) != NULL);
    /* 64-byte allocation can no longer fit once padding is included. */
    CHECK(arena_alloc_aligned(a, 64, 8) == NULL);

    arena_free(a);
}

static void test_aligned_size_max_does_not_overflow(void) {
    arena_t *a = arena_init(64);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 3) != NULL);
    CHECK(arena_alloc_aligned(a, SIZE_MAX, 8) == NULL);
    CHECK(arena_alloc_aligned(a, SIZE_MAX - 4, 16) == NULL);

    arena_free(a);
}

static void test_aligned_double_and_int64(void) {
    arena_t *a = arena_init(256);
    REQUIRE(a != NULL);

    REQUIRE(arena_alloc(a, 3) != NULL);
    double *d = (double *)arena_alloc_aligned(a, sizeof(double), sizeof(double));
    REQUIRE(d != NULL);
    *d = 3.14159;
    CHECK(*d == 3.14159);

    REQUIRE(arena_alloc(a, 1) != NULL);
    int64_t *n = (int64_t *)arena_alloc_aligned(a, sizeof(int64_t), sizeof(int64_t));
    REQUIRE(n != NULL);
    *n = -42;
    CHECK(*n == -42);

    arena_free(a);
}

#endif /* ARENA_HAS_ALIGNED */

/* ------------------------------------------------------------------ */
/* Runner                                                             */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== arena test suite ===\n\n");

    /* init / free */
    RUN_TEST(test_init_sets_fields);
    RUN_TEST(test_init_impossible_size_returns_null);
    RUN_TEST(test_free_null_is_safe);

    /* basic allocation */
    RUN_TEST(test_first_alloc_returns_buffer_start);
    RUN_TEST(test_sequential_allocs_are_contiguous);
    RUN_TEST(test_allocations_stay_inside_buffer);
    RUN_TEST(test_data_integrity_across_allocations);
    RUN_TEST(test_can_store_typed_data);

    /* capacity limits */
    RUN_TEST(test_exact_fit_succeeds_then_next_fails);
    RUN_TEST(test_one_byte_over_capacity_fails);
    RUN_TEST(test_failed_alloc_does_not_poison_arena);
    RUN_TEST(test_size_max_does_not_overflow);
    RUN_TEST(test_fill_arena_with_single_byte_allocs);

    /* edge cases */
    RUN_TEST(test_zero_size_alloc_does_not_advance);
    RUN_TEST(test_zero_size_alloc_on_full_arena);
    RUN_TEST(test_null_arena_is_handled);

    /* reset */
    RUN_TEST(test_reset_returns_offset_to_zero);
    RUN_TEST(test_reset_reuses_same_memory);
    RUN_TEST(test_full_capacity_available_after_reset);
    RUN_TEST(test_repeated_reset_cycles);

    /* stress */
    RUN_TEST(test_random_alloc_stress);

#ifdef ARENA_HAS_ALIGNED
    RUN_TEST(test_aligned_results_are_aligned);
    RUN_TEST(test_aligned_rejects_bad_alignment);
    RUN_TEST(test_aligned_padding_counts_toward_capacity);
    RUN_TEST(test_aligned_size_max_does_not_overflow);
    RUN_TEST(test_aligned_double_and_int64);
#else
    printf("\n(alignment tests skipped: build with ARENA_HAS_ALIGNED)\n");
#endif

    printf("\n=== %d run, %d passed, %d failed ===\n",
           g_tests_run, g_tests_run - g_tests_failed, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}