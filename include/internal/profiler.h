// profiler.h - Performance monitoring interface

#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>

// Initialization
void perf_init(void);
void perf_shutdown(void);

// Counter functions
void perf_count_allocation(uint64_t size);
void perf_count_deallocation(uint64_t size);
void perf_count_buffer_alloc(void);
void perf_count_line_alloc(void);
void perf_count_key_lookup(void);
void perf_count_display_update(void);
void perf_count_file_read(void);
void perf_count_file_write(void);

// Timing functions
void perf_start_timing(const char* operation);
void perf_end_timing(const char* operation);

// Reporting
void perf_report(void);

#endif // PROFILER_H