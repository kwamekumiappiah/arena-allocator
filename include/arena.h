#ifndef ARENA_H
#define ARENA_H

#include <stdint.h> // For uint8_t (byte-level buffer pointer) 🔢
#include <stddef.h> // For size_t (memory size type) 📏

/**
 * 🧱 Represents a fixed-size arena memory allocator pool.
 * 
 * - buffer:   Pointer to the contiguous byte array allocated on the heap.
 * - capacity: Total memory capacity of the arena in bytes.
 * - offset:   Current allocation cursor (tracks how many bytes are used).
 */
typedef struct arena_t {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} arena_t;

/**
 * 🏗️ Initializes an arena pool with a given byte capacity.
 */
arena_t *arena_init(size_t capacity);

/**
 * 📦 Allocates a contiguous block of memory of requested size from the arena.
 */
void *arena_alloc(arena_t *arena, size_t size);

/**
 * 🔄 Resets the allocation cursor back to 0 without releasing the memory buffer.
 */
void arena_reset(arena_t *arena);

/**
 * 🧹 Frees the internal memory buffer and the arena handle itself.
 */
void arena_free(arena_t *arena);

#endif // ARENA_H