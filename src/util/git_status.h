#ifndef GIT_STATUS_H
#define GIT_STATUS_H

/*
 * git_status.h - Async Git status API for status line
 */

#ifdef __cplusplus
extern "C" {
#endif

void git_status_init(void);
void git_status_request_async(const char* cwd);
int git_status_get_cached(char* out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* GIT_STATUS_H */
