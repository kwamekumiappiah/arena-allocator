/*
 * main.c - showcase for the arena allocator
 *
 * Build (MSVC, x64 Native Tools Command Prompt):
 *     cl /W4 /std:c11 main.c arena.c /Fe:demo.exe
 *
 * Build (MinGW-w64 / MSYS2):
 *     gcc -Wall -Wextra -std=c11 main.c arena.c -o demo.exe
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "arena.h"

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * The arena has no alignment support yet, so this demo rounds every
 * request up to a multiple of 8. Because malloc returns a well-aligned
 * base, that keeps every pointer 8-byte aligned (fine for int, double,
 * pointers and our structs). Once arena_alloc_aligned() exists, replace
 * this with it.
 */
static void *arena_alloc8(arena_t *a, size_t size) {
    size_t rounded = (size + 7u) & ~(size_t)7u;
    return arena_alloc(a, rounded);
}

#define ARENA_NEW(a, T)         ((T *)arena_alloc8((a), sizeof(T)))
#define ARENA_ARRAY(a, T, n)    ((T *)arena_alloc8((a), sizeof(T) * (n)))

static void print_usage(const char *label, const arena_t *a) {
    double pct = 100.0 * (double)a->offset / (double)a->capacity;
    printf("  [%s] used %llu / %llu bytes (%.1f%%)\n",
           label,
           (unsigned long long)a->offset,
           (unsigned long long)a->capacity,
           pct);
}

static void section(const char *title) {
    printf("\n=== %s ===\n", title);
}

/* ------------------------------------------------------------------ */
/* Demo 1: basic allocation                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    char   name[16];
    int    hp;
    double speed;
} player_t;

static void demo_basic(arena_t *a) {
    section("1. Basic allocation");

    int *scores = ARENA_ARRAY(a, int, 5);
    player_t *hero = ARENA_NEW(a, player_t);
    if (!scores || !hero) { puts("  out of memory"); return; }

    for (int i = 0; i < 5; i++) scores[i] = (i + 1) * 100;

    strcpy(hero->name, "Ada");
    hero->hp = 100;
    hero->speed = 4.5;

    printf("  scores: ");
    for (int i = 0; i < 5; i++) printf("%d ", scores[i]);
    printf("\n  hero:   %s (hp=%d, speed=%.1f)\n",
           hero->name, hero->hp, hero->speed);

    printf("  scores @ +%llu, hero @ +%llu (both inside one buffer)\n",
           (unsigned long long)((uint8_t *)scores - a->buffer),
           (unsigned long long)((uint8_t *)hero - a->buffer));

    print_usage("after", a);
    /* No free() calls for scores or hero: the arena owns them. */
}

/* ------------------------------------------------------------------ */
/* Demo 2: linked list with no per-node free                          */
/* ------------------------------------------------------------------ */

typedef struct node_t {
    int value;
    struct node_t *next;
} node_t;

static void demo_linked_list(arena_t *a) {
    section("2. Linked list, zero free() calls");

    node_t *head = NULL;
    for (int i = 5; i >= 1; i--) {
        node_t *n = ARENA_NEW(a, node_t);
        if (!n) { puts("  out of memory"); return; }
        n->value = i * i;
        n->next = head;
        head = n;
    }

    printf("  list: ");
    for (node_t *n = head; n; n = n->next) {
        printf("%d%s", n->value, n->next ? " -> " : "\n");
    }
    print_usage("after", a);
}

/* ------------------------------------------------------------------ */
/* Demo 3: per-frame scratch memory with reset                        */
/* ------------------------------------------------------------------ */

static void demo_frame_loop(arena_t *a) {
    section("3. Frame loop: allocate freely, reset in O(1)");

    void *first_ptr = NULL;

    for (int frame = 1; frame <= 3; frame++) {
        /* Everything allocated here lives for exactly one frame. */
        char *msg = (char *)arena_alloc8(a, 64);
        int  *tmp = ARENA_ARRAY(a, int, 16);
        if (!msg || !tmp) { puts("  out of memory"); return; }

        for (int i = 0; i < 16; i++) tmp[i] = frame * i;
        int sum = 0;
        for (int i = 0; i < 16; i++) sum += tmp[i];

        snprintf(msg, 64, "frame %d: sum of scratch data = %d", frame, sum);
        printf("  %s\n", msg);
        print_usage("mid-frame", a);

        if (frame == 1) first_ptr = msg;
        else printf("  msg address reused from frame 1: %s\n",
                    msg == first_ptr ? "yes" : "no");

        arena_reset(a);  /* instantly frees every allocation above */
    }

    print_usage("after resets", a);
}

/* ------------------------------------------------------------------ */
/* Demo 4: handling exhaustion                                        */
/* ------------------------------------------------------------------ */

static void demo_exhaustion(void) {
    section("4. Running out of space");

    arena_t *small = arena_init(64);
    if (!small) { puts("  init failed"); return; }

    int count = 0;
    while (arena_alloc(small, 10) != NULL) count++;

    printf("  10-byte allocations that fit in 64 bytes: %d\n", count);
    print_usage("full-ish", small);

    /* A failed alloc leaves the arena intact; smaller requests still work. */
    void *big   = arena_alloc(small, 100);
    void *tiny  = arena_alloc(small, 4);
    printf("  alloc(100) -> %s\n", big  ? "ok" : "NULL (as expected)");
    printf("  alloc(4)   -> %s\n", tiny ? "ok (still usable)" : "NULL");

    /* Overflow-safe: a huge request is rejected, not wrapped around. */
    void *huge = arena_alloc(small, SIZE_MAX);
    printf("  alloc(SIZE_MAX) -> %s\n", huge ? "ok (BUG!)" : "NULL (no overflow)");

    arena_free(small);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("Arena allocator demo\n");

    arena_t *arena = arena_init(1024);
    if (!arena) {
        fprintf(stderr, "failed to create arena\n");
        return 1;
    }
    printf("Created arena with %llu bytes\n",
           (unsigned long long)arena->capacity);

    demo_basic(arena);
    demo_linked_list(arena);

    arena_reset(arena);   /* start fresh for the frame demo */
    demo_frame_loop(arena);

    demo_exhaustion();

    section("Cleanup");
    arena_free(arena);    /* one free releases everything */
    puts("  arena freed, done.");

    return 0;
}