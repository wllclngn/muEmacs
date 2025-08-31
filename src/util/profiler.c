// profiler.c - Performance monitoring for μEmacs

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"

typedef struct perf_timer {
    const char* operation;
    struct timespec start;
    struct timespec end;
    uint64_t elapsed_ns;
    struct perf_timer* next;
} perf_timer_t;

typedef struct perf_counters {
    uint64_t memory_allocated;
    uint64_t memory_peak;
    uint64_t buffer_allocations;
    uint64_t line_allocations;
    uint64_t key_lookups;
    uint64_t display_updates;
    uint64_t file_reads;
    uint64_t file_writes;
    struct timespec start_time;
    perf_timer_t* timers;
} perf_counters_t;

static perf_counters_t perf_stats = {0};
static int perf_enabled = FALSE;

void perf_init(void) {
    memset(&perf_stats, 0, sizeof(perf_stats));
    clock_gettime(CLOCK_MONOTONIC, &perf_stats.start_time);
    perf_enabled = TRUE;
}

void perf_shutdown(void) {
    perf_timer_t* timer = perf_stats.timers;
    while (timer) {
        perf_timer_t* next = timer->next;
        SAFE_FREE(timer);
        timer = next;
    }
    perf_enabled = FALSE;
}

void perf_count_allocation(uint64_t size) {
    if (!perf_enabled) return;
    perf_stats.memory_allocated += size;
    if (perf_stats.memory_allocated > perf_stats.memory_peak) {
        perf_stats.memory_peak = perf_stats.memory_allocated;
    }
}

void perf_count_deallocation(uint64_t size) {
    if (!perf_enabled) return;
    if (perf_stats.memory_allocated >= size) {
        perf_stats.memory_allocated -= size;
    }
}

void perf_count_buffer_alloc(void) {
    if (!perf_enabled) return;
    perf_stats.buffer_allocations++;
}

void perf_count_line_alloc(void) {
    if (!perf_enabled) return;
    perf_stats.line_allocations++;
}

void perf_count_key_lookup(void) {
    if (!perf_enabled) return;
    perf_stats.key_lookups++;
}

void perf_count_display_update(void) {
    if (!perf_enabled) return;
    perf_stats.display_updates++;
}

void perf_count_file_read(void) {
    if (!perf_enabled) return;
    perf_stats.file_reads++;
}

void perf_count_file_write(void) {
    if (!perf_enabled) return;
    perf_stats.file_writes++;
}

void perf_start_timing(const char* operation) {
    if (!perf_enabled) return;
    
    perf_timer_t* timer = (perf_timer_t*)safe_alloc(sizeof(perf_timer_t), "perf timer", __FILE__, __LINE__);
    if (!timer) return;
    
    timer->operation = operation;
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
    timer->elapsed_ns = 0;
    timer->next = perf_stats.timers;
    perf_stats.timers = timer;
}

void perf_end_timing(const char* operation) {
    if (!perf_enabled) return;
    
    perf_timer_t* timer = perf_stats.timers;
    while (timer) {
        if (timer->operation == operation && timer->elapsed_ns == 0) {
            clock_gettime(CLOCK_MONOTONIC, &timer->end);
            timer->elapsed_ns = (timer->end.tv_sec - timer->start.tv_sec) * 1000000000ULL +
                              (timer->end.tv_nsec - timer->start.tv_nsec);
            break;
        }
        timer = timer->next;
    }
}

void perf_report(void) {
    if (!perf_enabled) return;
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t total_ns = (now.tv_sec - perf_stats.start_time.tv_sec) * 1000000000ULL +
                       (now.tv_nsec - perf_stats.start_time.tv_nsec);
    
    mlwrite("=== Performance Report ===");
    mlwrite("Total runtime: %llu ms", total_ns / 1000000);
    mlwrite("Memory allocated: %llu bytes", perf_stats.memory_allocated);
    mlwrite("Memory peak: %llu bytes", perf_stats.memory_peak);
    mlwrite("Buffer allocations: %llu", perf_stats.buffer_allocations);
    mlwrite("Line allocations: %llu", perf_stats.line_allocations);
    mlwrite("Key lookups: %llu", perf_stats.key_lookups);
    mlwrite("Display updates: %llu", perf_stats.display_updates);
    mlwrite("File reads: %llu", perf_stats.file_reads);
    mlwrite("File writes: %llu", perf_stats.file_writes);
    
    mlwrite("=== Timing Details ===");
    perf_timer_t* timer = perf_stats.timers;
    while (timer) {
        if (timer->elapsed_ns > 0) {
            mlwrite("%s: %llu ms", timer->operation, timer->elapsed_ns / 1000000);
        }
        timer = timer->next;
    }
}