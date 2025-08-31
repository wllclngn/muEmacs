#ifndef UTIL_H_
#define UTIL_H_

/* C23 typeof-based type-safe array size macro */
#ifdef __STDC_VERSION__ 
#if __STDC_VERSION__ >= 202311L
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(typeof(a[0])))
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif
void mystrscpy(char *dst, const char *src, int size);

#endif  /* UTIL_H_ */
