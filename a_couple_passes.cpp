
typedef unsigned long size_t;
;
typedef int wchar_t;
;
typedef float _Float32;
;
typedef double _Float64;
;
typedef double _Float32x;
;
typedef long double _Float64x;
;
typedef struct {
    int quot;
    int rem;
} div_t;
;
typedef struct {
    long quot;
    long rem;
} ldiv_t;
;
__extension__ typedef struct {
    long long quot;
    long long rem;
} lldiv_t;
;
extern size_t __ctype_get_mb_cur_max(void) __attribute__((__nothrow__));
extern double atof(const char *__nptr)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern int atoi(const char *__nptr)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern long int atol(const char *__nptr)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
__extension__ extern long long int atoll(const char *__nptr)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern double strtod(const char *__restrict __nptr,
                     char **__restrict __endptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern float strtof(const char *__restrict __nptr,
                    char **__restrict __endptr) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern long double strtold(const char *__restrict __nptr,
                           char **__restrict __endptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern _Float32 strtof32(const char *__restrict __nptr,
                         char **__restrict __endptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern _Float64 strtof64(const char *__restrict __nptr,
                         char **__restrict __endptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern _Float32x strtof32x(const char *__restrict __nptr,
                           char **__restrict __endptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern _Float64x strtof64x(const char *__restrict __nptr,
                           char **__restrict __endptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern long int strtol(const char *__restrict __nptr,
                       char **__restrict __endptr, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern unsigned long int strtoul(const char *__restrict __nptr,
                                 char **__restrict __endptr, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
__extension__ extern long long int strtoq(const char *__restrict __nptr,
                                          char **__restrict __endptr, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
__extension__ extern unsigned long long int strtouq(const char *__restrict __nptr,
                                                    char **__restrict __endptr, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
__extension__ extern long long int strtoll(const char *__restrict __nptr,
                                           char **__restrict __endptr, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
__extension__ extern unsigned long long int strtoull(const char *__restrict __nptr,
                                                     char **__restrict __endptr, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int strfromd(char *__dest, size_t __size, const char *__format,
                    double __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int strfromf(char *__dest, size_t __size, const char *__format,
                    float __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int strfroml(char *__dest, size_t __size, const char *__format,
                    long double __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int strfromf32(char *__dest, size_t __size, const char *__format,
                      _Float32 __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int strfromf64(char *__dest, size_t __size, const char *__format,
                      _Float64 __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int strfromf32x(char *__dest, size_t __size, const char *__format,
                       _Float32x __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int strfromf64x(char *__dest, size_t __size, const char *__format,
                       _Float64x __f)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
struct __locale_struct {
  struct __locale_data *__locales[13];
  const unsigned short int *__ctype_b;
  const int *__ctype_tolower;
  const int *__ctype_toupper;
  const char *__names[13];
};
typedef struct __locale_struct *__locale_t;
;

typedef __locale_t locale_t;
;

extern long int strtol_l(const char *__restrict __nptr,
                         char **__restrict __endptr, int __base,
                         locale_t __loc) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 4)));
extern unsigned long int strtoul_l(const char *__restrict __nptr,
                                   char **__restrict __endptr,
                                   int __base, locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 4)));
__extension__ extern long long int strtoll_l(const char *__restrict __nptr,
                                             char **__restrict __endptr, int __base,
                                             locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 4)));
__extension__ extern unsigned long long int strtoull_l(const char *__restrict __nptr,
                                                       char **__restrict __endptr,
                                                       int __base, locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 4)));
extern double strtod_l(const char *__restrict __nptr,
                       char **__restrict __endptr, locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern float strtof_l(const char *__restrict __nptr,
                      char **__restrict __endptr, locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern long double strtold_l(const char *__restrict __nptr,
                             char **__restrict __endptr,
                             locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern _Float32 strtof32_l(const char *__restrict __nptr,
                           char **__restrict __endptr,
                           locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern _Float64 strtof64_l(const char *__restrict __nptr,
                           char **__restrict __endptr,
                           locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern _Float32x strtof32x_l(const char *__restrict __nptr,
                             char **__restrict __endptr,
                             locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern _Float64x strtof64x_l(const char *__restrict __nptr,
                             char **__restrict __endptr,
                             locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 3)));
extern char *l64a(long int __n) __attribute__((__nothrow__));
extern long int a64l(const char *__s)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
typedef unsigned char __u_char;
;
typedef unsigned short __u_short;
;
typedef unsigned int __u_int;
;
typedef unsigned long __u_long;
;
typedef signed char __int8_t;
;
typedef unsigned char __uint8_t;
;
typedef short __int16_t;
;
typedef unsigned short __uint16_t;
;
typedef int __int32_t;
;
typedef unsigned int __uint32_t;
;
typedef long __int64_t;
;
typedef unsigned long __uint64_t;
;
typedef __int8_t __int_least8_t;
;
typedef __uint8_t __uint_least8_t;
;
typedef __int16_t __int_least16_t;
;
typedef __uint16_t __uint_least16_t;
;
typedef __int32_t __int_least32_t;
;
typedef __uint32_t __uint_least32_t;
;
typedef __int64_t __int_least64_t;
;
typedef __uint64_t __uint_least64_t;
;
typedef long __quad_t;
;
typedef unsigned long __u_quad_t;
;
typedef long __intmax_t;
;
typedef unsigned long __uintmax_t;
;
typedef unsigned long __dev_t;
;
typedef unsigned int __uid_t;
;
typedef unsigned int __gid_t;
;
typedef unsigned long __ino_t;
;
typedef unsigned long __ino64_t;
;
typedef unsigned int __mode_t;
;
typedef unsigned long __nlink_t;
;
typedef long __off_t;
;
typedef long __off64_t;
;
typedef int __pid_t;
;
typedef struct {
    int __val[2];
} __fsid_t;
;
typedef long __clock_t;
;
typedef unsigned long __rlim_t;
;
typedef unsigned long __rlim64_t;
;
typedef unsigned int __id_t;
;
typedef long __time_t;
;
typedef unsigned int __useconds_t;
;
typedef long __suseconds_t;
;
typedef long __suseconds64_t;
;
typedef int __daddr_t;
;
typedef int __key_t;
;
typedef int __clockid_t;
;
typedef void *__timer_t;
;
typedef long __blksize_t;
;
typedef long __blkcnt_t;
;
typedef long __blkcnt64_t;
;
typedef unsigned long __fsblkcnt_t;
;
typedef unsigned long __fsblkcnt64_t;
;
typedef unsigned long __fsfilcnt_t;
;
typedef unsigned long __fsfilcnt64_t;
;
typedef long __fsword_t;
;
typedef long __ssize_t;
;
typedef long __syscall_slong_t;
;
typedef unsigned long __syscall_ulong_t;
;
typedef __off64_t __loff_t;
;
typedef char *__caddr_t;
;
typedef long __intptr_t;
;
typedef unsigned int __socklen_t;
;
typedef int __sig_atomic_t;
;
typedef __u_char u_char;
;
typedef __u_short u_short;
;
typedef __u_int u_int;
;
typedef __u_long u_long;
;
typedef __quad_t quad_t;
;
typedef __u_quad_t u_quad_t;
;
typedef __fsid_t fsid_t;
;
typedef __loff_t loff_t;
;
typedef __ino_t ino_t;
;
typedef __ino64_t ino64_t;
;
typedef __dev_t dev_t;
;
typedef __gid_t gid_t;
;
typedef __mode_t mode_t;
;
typedef __nlink_t nlink_t;
;
typedef __uid_t uid_t;
;
typedef __off_t off_t;
;
typedef __off64_t off64_t;
;
typedef __pid_t pid_t;
;
typedef __id_t id_t;
;
typedef __ssize_t ssize_t;
;
typedef __daddr_t daddr_t;
;
typedef __caddr_t caddr_t;
;
typedef __key_t key_t;
;
typedef __clock_t clock_t;
;

typedef __clockid_t clockid_t;
;
typedef __time_t time_t;
;
typedef __timer_t timer_t;
;
typedef __useconds_t useconds_t;
;
typedef __suseconds_t suseconds_t;
;
typedef unsigned long ulong;
;
typedef unsigned short ushort;
;
typedef unsigned int uint;
;
typedef __int8_t int8_t;
;
typedef __int16_t int16_t;
;
typedef __int32_t int32_t;
;
typedef __int64_t int64_t;
;
typedef __uint8_t u_int8_t;
;
typedef __uint16_t u_int16_t;
;
typedef __uint32_t u_int32_t;
;
typedef __uint64_t u_int64_t;
;
typedef int register_t __attribute__((mode(__word__)));
 __attribute__((__mode__(__word__)));
static inline __uint16_t __bswap_16(__uint16_t ___var__207);
static inline __uint32_t __bswap_32(__uint32_t ___var__209);
__extension__ static inline __uint64_t __bswap_64(__uint64_t ___var__211);
static inline __uint16_t __uint16_identity(__uint16_t ___var__213);
static inline __uint32_t __uint32_identity(__uint32_t ___var__215);
static inline __uint64_t __uint64_identity(__uint64_t ___var__217);
typedef struct {
    unsigned long __val[16];
} __sigset_t;
;
typedef __sigset_t sigset_t;
;
struct timeval {
  __time_t tv_sec;
  __suseconds_t tv_usec;
};

struct timespec {
  __time_t tv_sec;
  __syscall_slong_t tv_nsec;
};
typedef long __fd_mask;
;
typedef struct {
    __fd_mask fds_bits[16];
} fd_set;
;
typedef __fd_mask fd_mask;
;
extern int select(int __nfds, fd_set *__restrict __readfds,
                  fd_set *__restrict __writefds,
                  fd_set *__restrict __exceptfds,
                  struct timeval *__restrict __timeout);
extern int pselect(int __nfds, fd_set *__restrict __readfds,
                   fd_set *__restrict __writefds,
                   fd_set *__restrict __exceptfds,
                   const struct timespec *__restrict __timeout,
                   const __sigset_t *__restrict __sigmask);
typedef __blksize_t blksize_t;
;
typedef __blkcnt_t blkcnt_t;
;
typedef __fsblkcnt_t fsblkcnt_t;
;
typedef __fsfilcnt_t fsfilcnt_t;
;
typedef __blkcnt64_t blkcnt64_t;
;
typedef __fsblkcnt64_t fsblkcnt64_t;
;
typedef __fsfilcnt64_t fsfilcnt64_t;
;

typedef union {
    unsigned long long __value64;
    struct {
        unsigned int __low;
        unsigned int __high;
    } __value32;
} __atomic_wide_counter;
;
typedef struct __pthread_internal_list {
    struct __pthread_internal_list *__prev;
    struct __pthread_internal_list *__next;
} __pthread_list_t;
;
typedef struct __pthread_internal_slist {
    struct __pthread_internal_slist *__next;
} __pthread_slist_t;
;
struct __pthread_mutex_s {
  int __lock;
  unsigned int __count;
  int __owner;
  unsigned int __nusers;
  int __kind;
  short __spins;
  short __elision;
  __pthread_list_t __list;
};
struct __pthread_rwlock_arch_t {
  unsigned int __readers;
  unsigned int __writers;
  unsigned int __wrphase_futex;
  unsigned int __writers_futex;
  unsigned int __pad3;
  unsigned int __pad4;
  int __cur_writer;
  int __shared;
  signed char __rwelision;
  unsigned char __pad1[7];
  unsigned long int __pad2;
  unsigned int __flags;
};
struct __pthread_cond_s {
  __atomic_wide_counter __wseq;
  __atomic_wide_counter __g1_start;
  unsigned int __g_refs[2];
  unsigned int __g_size[2];
  unsigned int __g1_orig_size;
  unsigned int __wrefs;
  unsigned int __g_signals[2];
};
typedef unsigned int __tss_t;
;
typedef unsigned long __thrd_t;
;
typedef struct {
    int __data;
} __once_flag;
;
typedef unsigned long pthread_t;
;
typedef union {
    char __size[4];
    int __align;
} pthread_mutexattr_t;
;
typedef union {
    char __size[4];
    int __align;
} pthread_condattr_t;
;
typedef unsigned int pthread_key_t;
;
typedef int pthread_once_t;
;
union pthread_attr_t {
  char __size[56];
  long int __align;
};
typedef union pthread_attr_t pthread_attr_t;
;
typedef union {
    struct __pthread_mutex_s __data;
    char __size[40];
    long __align;
} pthread_mutex_t;
;
typedef union {
    struct __pthread_cond_s __data;
    char __size[48];
    long long __align;
} pthread_cond_t;
;
typedef union {
    struct __pthread_rwlock_arch_t __data;
    char __size[56];
    long __align;
} pthread_rwlock_t;
;
typedef union {
    char __size[8];
    long __align;
} pthread_rwlockattr_t;
;
typedef volatile int pthread_spinlock_t;
;
typedef union {
    char __size[32];
    long __align;
} pthread_barrier_t;
;
typedef union {
    char __size[4];
    int __align;
} pthread_barrierattr_t;
;
extern long int random(void) __attribute__((__nothrow__));
extern void srandom(unsigned int __seed) __attribute__((__nothrow__));
extern char *initstate(unsigned int __seed, char *__statebuf,
                       size_t __statelen) __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern char *setstate(char *__statebuf) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
struct random_data {
  int32_t *fptr;
  int32_t *rptr;
  int32_t *state;
  int rand_type;
  int rand_deg;
  int rand_sep;
  int32_t *end_ptr;
};
extern int random_r(struct random_data *__restrict __buf,
                    int32_t *__restrict __result) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int srandom_r(unsigned int __seed, struct random_data *__buf)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int initstate_r(unsigned int __seed, char *__restrict __statebuf,
                       size_t __statelen,
                       struct random_data *__restrict __buf)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2, 4)));
extern int setstate_r(char *__restrict __statebuf,
                      struct random_data *__restrict __buf)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int rand(void) __attribute__((__nothrow__));
extern void srand(unsigned int __seed) __attribute__((__nothrow__));
extern int rand_r(unsigned int *__seed) __attribute__((__nothrow__));
extern double drand48(void) __attribute__((__nothrow__));
extern double erand48(unsigned short int __xsubi[3]) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern long int lrand48(void) __attribute__((__nothrow__));
extern long int nrand48(unsigned short int __xsubi[3])
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern long int mrand48(void) __attribute__((__nothrow__));
extern long int jrand48(unsigned short int __xsubi[3])
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern void srand48(long int __seedval) __attribute__((__nothrow__));
extern unsigned short int *seed48(unsigned short int __seed16v[3])
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern void lcong48(unsigned short int __param[7]) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
struct drand48_data {
  unsigned short int __x[3];
  unsigned short int __old_x[3];
  unsigned short int __c;
  unsigned short int __init;
  __extension__ unsigned long long int __a;
};
extern int drand48_r(struct drand48_data *__restrict __buffer,
                     double *__restrict __result) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int erand48_r(unsigned short int __xsubi[3],
                     struct drand48_data *__restrict __buffer,
                     double *__restrict __result) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int lrand48_r(struct drand48_data *__restrict __buffer,
                     long int *__restrict __result)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int nrand48_r(unsigned short int __xsubi[3],
                     struct drand48_data *__restrict __buffer,
                     long int *__restrict __result)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int mrand48_r(struct drand48_data *__restrict __buffer,
                     long int *__restrict __result)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int jrand48_r(unsigned short int __xsubi[3],
                     struct drand48_data *__restrict __buffer,
                     long int *__restrict __result)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int srand48_r(long int __seedval, struct drand48_data *__buffer)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int seed48_r(unsigned short int __seed16v[3],
                    struct drand48_data *__buffer) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int lcong48_r(unsigned short int __param[7],
                     struct drand48_data *__buffer)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern __uint32_t arc4random(void)
    __attribute__((__nothrow__));
extern void arc4random_buf(void *__buf, size_t __size)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern __uint32_t arc4random_uniform(__uint32_t __upper_bound)
    __attribute__((__nothrow__));
extern void *malloc(size_t __size) __attribute__((__nothrow__)) __attribute__((__malloc__));
extern void *calloc(size_t __nmemb, size_t __size)
    __attribute__((__nothrow__)) __attribute__((__malloc__));
extern void *realloc(void *__ptr, size_t __size)
    __attribute__((__nothrow__)) __attribute__((__warn_unused_result__));
extern void free(void *__ptr) __attribute__((__nothrow__));
extern void *reallocarray(void *__ptr, size_t __nmemb, size_t __size)
    __attribute__((__nothrow__)) __attribute__((__warn_unused_result__));
extern void *reallocarray(void *__ptr, size_t __nmemb, size_t __size)
    __attribute__((__nothrow__));
extern void *valloc(size_t __size) __attribute__((__nothrow__)) __attribute__((__malloc__));
extern int posix_memalign(void **__memptr, size_t __alignment, size_t __size)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern void *aligned_alloc(size_t __alignment, size_t __size)
    __attribute__((__nothrow__)) __attribute__((__malloc__)) __attribute__((__alloc_align__(1)));
extern void abort(void) __attribute__((__nothrow__)) __attribute__((__noreturn__));
extern int atexit(void (*__func)(void)) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int at_quick_exit(void (*__func)(void)) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int on_exit(void (*__func)(int __status, void *__arg), void *__arg)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern void exit(int __status) __attribute__((__nothrow__)) __attribute__((__noreturn__));
extern void quick_exit(int __status) __attribute__((__nothrow__)) __attribute__((__noreturn__));
extern void _Exit(int __status) __attribute__((__nothrow__)) __attribute__((__noreturn__));
extern char *getenv(const char *__name) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern char *secure_getenv(const char *__name)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int putenv(char *__string) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int setenv(const char *__name, const char *__value, int __replace)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int unsetenv(const char *__name) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int clearenv(void) __attribute__((__nothrow__));
extern char *mktemp(char *__template) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int mkstemp(char *__template) __attribute__((__nonnull__(1)));
extern int mkstemp64(char *__template) __attribute__((__nonnull__(1)));
extern int mkstemps(char *__template, int __suffixlen) __attribute__((__nonnull__(1)));
extern int mkstemps64(char *__template, int __suffixlen)
    __attribute__((__nonnull__(1)));
extern char *mkdtemp(char *__template) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int mkostemp(char *__template, int __flags) __attribute__((__nonnull__(1)));
extern int mkostemp64(char *__template, int __flags) __attribute__((__nonnull__(1)));
extern int mkostemps(char *__template, int __suffixlen, int __flags)
    __attribute__((__nonnull__(1)));
extern int mkostemps64(char *__template, int __suffixlen, int __flags)
    __attribute__((__nonnull__(1)));
extern int system(const char *__command);
extern char *canonicalize_file_name(const char *__name)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1))) __attribute__((__malloc__));
extern char *realpath(const char *__restrict __name,
                      char *__restrict __resolved) __attribute__((__nothrow__));
typedef int (*__compar_fn_t)(const void *, const void *);
;
typedef __compar_fn_t comparison_fn_t;
;
typedef int (*__compar_d_fn_t)(const void *, const void *, void *);
;
extern void *bsearch(const void *__key, const void *__base,
                     size_t __nmemb, size_t __size, __compar_fn_t __compar)
    __attribute__((__nonnull__(1, 2, 5)));
extern void qsort(void *__base, size_t __nmemb, size_t __size,
                  __compar_fn_t __compar) __attribute__((__nonnull__(1, 4)));
extern void qsort_r(void *__base, size_t __nmemb, size_t __size,
                    __compar_d_fn_t __compar, void *__arg)
    __attribute__((__nonnull__(1, 4)));
extern int abs(int __x) __attribute__((__nothrow__)) __attribute__((__const__));
extern long int labs(long int __x) __attribute__((__nothrow__)) __attribute__((__const__));
__extension__ extern long long int llabs(long long int __x)
    __attribute__((__nothrow__)) __attribute__((__const__));
extern div_t div(int __numer, int __denom)
    __attribute__((__nothrow__)) __attribute__((__const__));
extern ldiv_t ldiv(long int __numer, long int __denom)
    __attribute__((__nothrow__)) __attribute__((__const__));
__extension__ extern lldiv_t lldiv(long long int __numer,
                                   long long int __denom)
    __attribute__((__nothrow__)) __attribute__((__const__));
extern char *ecvt(double __value, int __ndigit, int *__restrict __decpt,
                  int *__restrict __sign) __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4)));
extern char *fcvt(double __value, int __ndigit, int *__restrict __decpt,
                  int *__restrict __sign) __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4)));
extern char *gcvt(double __value, int __ndigit, char *__buf)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern char *qecvt(long double __value, int __ndigit,
                   int *__restrict __decpt, int *__restrict __sign)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4)));
extern char *qfcvt(long double __value, int __ndigit,
                   int *__restrict __decpt, int *__restrict __sign)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4)));
extern char *qgcvt(long double __value, int __ndigit, char *__buf)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3)));
extern int ecvt_r(double __value, int __ndigit, int *__restrict __decpt,
                  int *__restrict __sign, char *__restrict __buf,
                  size_t __len) __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4, 5)));
extern int fcvt_r(double __value, int __ndigit, int *__restrict __decpt,
                  int *__restrict __sign, char *__restrict __buf,
                  size_t __len) __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4, 5)));
extern int qecvt_r(long double __value, int __ndigit,
                   int *__restrict __decpt, int *__restrict __sign,
                   char *__restrict __buf, size_t __len)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4, 5)));
extern int qfcvt_r(long double __value, int __ndigit,
                   int *__restrict __decpt, int *__restrict __sign,
                   char *__restrict __buf, size_t __len)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(3, 4, 5)));
extern int mblen(const char *__s, size_t __n) __attribute__((__nothrow__));
extern int mbtowc(wchar_t *__restrict __pwc,
                  const char *__restrict __s, size_t __n) __attribute__((__nothrow__));
extern int wctomb(char *__s, wchar_t __wchar) __attribute__((__nothrow__));
extern size_t mbstowcs(wchar_t *__restrict __pwcs,
                       const char *__restrict __s, size_t __n) __attribute__((__nothrow__));
extern size_t wcstombs(char *__restrict __s,
                       const wchar_t *__restrict __pwcs, size_t __n)
    __attribute__((__nothrow__));
extern int rpmatch(const char *__response) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int getsubopt(char **__restrict __optionp,
                     char *const *__restrict __tokens,
                     char **__restrict __valuep)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2, 3)));
extern int posix_openpt(int __oflag);
extern int grantpt(int __fd) __attribute__((__nothrow__));
extern int unlockpt(int __fd) __attribute__((__nothrow__));
extern char *ptsname(int __fd) __attribute__((__nothrow__));
extern int ptsname_r(int __fd, char *__buf, size_t __buflen)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int getpt(void);
extern int getloadavg(double __loadavg[], int __nelem)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
typedef long ptrdiff_t;
;
typedef struct {
    long long __clang_max_align_nonce1 __attribute__((aligned(8)));
    long double __clang_max_align_nonce2 __attribute__((aligned(16)));
} max_align_t;
;
extern int ___var__646;
;
;
extern int ___var__647;
;
;
extern int ___var__648;
;
;
extern int ___var__649;
;
;
extern int ___var__650;
;
;
extern int ___var__651;
;
;
extern int ___var__652;
;
;
extern int ___var__653;
;
;
extern int ___var__654;
;
;
extern int ___var__655;
;
;
extern int ___var__656;
;
;
extern int ___var__657;
;
;
extern int ___var__658;
;
;
extern int ___var__659;
;
;
extern int ___var__660;
;
;
extern int ___var__661;
;
;
extern int ___var__662;
;
;
extern int ___var__663;
;
;
extern int ___var__664;
;
;
extern int ___var__665;
;
;

typedef void *voidp;
;
typedef __builtin_va_list va_list;
;
typedef __builtin_va_list __gnuc_va_list;
;
typedef struct {
    int __count;
    union {
        unsigned int __wch;
        char __wchb[4];
    } __value;
} __mbstate_t;
;
typedef struct _G_fpos_t {
    __off_t __pos;
    __mbstate_t __state;
} __fpos_t;
;
typedef struct _G_fpos64_t {
    __off64_t __pos;
    __mbstate_t __state;
} __fpos64_t;
;
struct _IO_FILE;
typedef struct _IO_FILE __FILE;
;
struct _IO_FILE;
typedef struct _IO_FILE FILE;
;
struct _IO_FILE;
struct _IO_marker;
struct _IO_codecvt;
struct _IO_wide_data;
typedef void _IO_lock_t;
;
struct _IO_FILE {
  int _flags;
  char *_IO_read_ptr;
  char *_IO_read_end;
  char *_IO_read_base;
  char *_IO_write_base;
  char *_IO_write_ptr;
  char *_IO_write_end;
  char *_IO_buf_base;
  char *_IO_buf_end;
  char *_IO_save_base;
  char *_IO_backup_base;
  char *_IO_save_end;
  struct _IO_marker *_markers;
  struct _IO_FILE *_chain;
  int _fileno;
  int _flags2;
  __off_t _old_offset;
  unsigned short _cur_column;
  signed char _vtable_offset;
  char _shortbuf[1];
  _IO_lock_t *_lock;
  __off64_t _offset;
  struct _IO_codecvt *_codecvt;
  struct _IO_wide_data *_wide_data;
  struct _IO_FILE *_freeres_list;
  void *_freeres_buf;
  size_t __pad5;
  int _mode;
  char _unused2[20];
};
typedef __ssize_t (cookie_read_function_t)(void *, char *, size_t);
;
typedef __ssize_t (cookie_write_function_t)(void *, const char *, size_t);
;
typedef int (cookie_seek_function_t)(void *, __off64_t *, int);
;
typedef int (cookie_close_function_t)(void *);
;
typedef struct _IO_cookie_io_functions_t {
    cookie_read_function_t *read;
    cookie_write_function_t *write;
    cookie_seek_function_t *seek;
    cookie_close_function_t *close;
} cookie_io_functions_t;
;
typedef __gnuc_va_list va_list;
;
typedef __fpos_t fpos_t;
;
typedef __fpos64_t fpos64_t;
;
extern FILE *___var__686;
;
;
extern FILE *___var__687;
;
;
extern FILE *___var__688;
;
;
extern int remove(const char *__filename) __attribute__((__nothrow__));
extern int rename(const char *__old, const char *__new) __attribute__((__nothrow__));
extern int renameat(int __oldfd, const char *__old, int __newfd,
                    const char *__new) __attribute__((__nothrow__));
extern int renameat2(int __oldfd, const char *__old, int __newfd,
                     const char *__new, unsigned int __flags) __attribute__((__nothrow__));
extern int fclose(FILE *__stream);
extern FILE *tmpfile(void)
    __attribute__((__malloc__));
extern FILE *tmpfile64(void)
    __attribute__((__malloc__));
extern char *tmpnam(char[20]) __attribute__((__nothrow__));
extern char *tmpnam_r(char __s[20]) __attribute__((__nothrow__));
extern char *tempnam(const char *__dir, const char *__pfx)
    __attribute__((__nothrow__)) __attribute__((__malloc__));
extern int fflush(FILE *__stream);
extern int fflush_unlocked(FILE *__stream);
extern int fcloseall(void);
extern FILE *fopen(const char *__restrict __filename,
                   const char *__restrict __modes)
    __attribute__((__malloc__));
extern FILE *freopen(const char *__restrict __filename,
                     const char *__restrict __modes,
                     FILE *__restrict __stream);
extern FILE *fopen64(const char *__restrict __filename,
                     const char *__restrict __modes)
    __attribute__((__malloc__));
extern FILE *freopen64(const char *__restrict __filename,
                       const char *__restrict __modes,
                       FILE *__restrict __stream);
extern FILE *fdopen(int __fd, const char *__modes) __attribute__((__nothrow__))
__attribute__((__malloc__));
extern FILE *fopencookie(void *__restrict __magic_cookie,
                         const char *__restrict __modes,
                         cookie_io_functions_t __io_funcs) __attribute__((__nothrow__))
__attribute__((__malloc__));
extern FILE *fmemopen(void *__s, size_t __len, const char *__modes)
    __attribute__((__nothrow__)) __attribute__((__malloc__));
extern FILE *open_memstream(char **__bufloc, size_t *__sizeloc) __attribute__((__nothrow__))
__attribute__((__malloc__));
extern void setbuf(FILE *__restrict __stream, char *__restrict __buf) __attribute__((__nothrow__));
extern int setvbuf(FILE *__restrict __stream, char *__restrict __buf,
                   int __modes, size_t __n) __attribute__((__nothrow__));
extern void setbuffer(FILE *__restrict __stream, char *__restrict __buf,
                      size_t __size) __attribute__((__nothrow__));
extern void setlinebuf(FILE *__stream) __attribute__((__nothrow__));
extern int fprintf(FILE *__restrict __stream,
                   const char *__restrict __format, ...);
extern int printf(const char *__restrict __format, ...);
extern int sprintf(char *__restrict __s,
                   const char *__restrict __format, ...) __attribute__((__nothrow__));
extern int vfprintf(FILE *__restrict __s, const char *__restrict __format,
                    __gnuc_va_list __arg);
extern int vprintf(const char *__restrict __format, __gnuc_va_list __arg);
extern int vsprintf(char *__restrict __s, const char *__restrict __format,
                    __gnuc_va_list __arg) __attribute__((__nothrow__));
extern int snprintf(char *__restrict __s, size_t __maxlen,
                    const char *__restrict __format, ...)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 3, 4)));
extern int vsnprintf(char *__restrict __s, size_t __maxlen,
                     const char *__restrict __format, __gnuc_va_list __arg)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 3, 0)));
extern int vasprintf(char **__restrict __ptr, const char *__restrict __f,
                     __gnuc_va_list __arg)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 2, 0)));
extern int __asprintf(char **__restrict __ptr,
                      const char *__restrict __fmt, ...)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 2, 3)));
extern int asprintf(char **__restrict __ptr,
                    const char *__restrict __fmt, ...)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 2, 3)));
extern int vdprintf(int __fd, const char *__restrict __fmt,
                    __gnuc_va_list __arg)
    __attribute__((__format__(__printf__, 2, 0)));
extern int dprintf(int __fd, const char *__restrict __fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)));
extern int fscanf(FILE *__restrict __stream,
                  const char *__restrict __format, ...);
extern int scanf(const char *__restrict __format, ...);
extern int sscanf(const char *__restrict __s,
                  const char *__restrict __format, ...) __attribute__((__nothrow__));
extern int fscanf(FILE *__restrict __stream, const char *__restrict __format, ...) __asm__(""
                                                                                           "__isoc99_fscanf");
extern int scanf(const char *__restrict __format, ...) __asm__(""
                                                               "__isoc99_scanf");
extern int sscanf(const char *__restrict __s, const char *__restrict __format, ...) __asm__(""
                                                                                            "__isoc99_sscanf") __attribute__((__nothrow__));
extern int vfscanf(FILE *__restrict __s, const char *__restrict __format,
                   __gnuc_va_list __arg)
    __attribute__((__format__(__scanf__, 2, 0)));
extern int vscanf(const char *__restrict __format, __gnuc_va_list __arg)
    __attribute__((__format__(__scanf__, 1, 0)));
extern int vsscanf(const char *__restrict __s,
                   const char *__restrict __format, __gnuc_va_list __arg)
    __attribute__((__nothrow__)) __attribute__((__format__(__scanf__, 2, 0)));
extern int vfscanf(FILE *__restrict __s, const char *__restrict __format, __gnuc_va_list __arg) __asm__(""
                                                                                                        "__isoc99_vfscanf")
    __attribute__((__format__(__scanf__, 2, 0)));
extern int vscanf(const char *__restrict __format, __gnuc_va_list __arg) __asm__(""
                                                                                 "__isoc99_vscanf")
    __attribute__((__format__(__scanf__, 1, 0)));
extern int vsscanf(const char *__restrict __s, const char *__restrict __format, __gnuc_va_list __arg) __asm__(""
                                                                                                              "__isoc99_vsscanf") __attribute__((__nothrow__))
__attribute__((__format__(__scanf__, 2, 0)));
extern int fgetc(FILE *__stream);
extern int getc(FILE *__stream);
extern int getchar(void);
extern int getc_unlocked(FILE *__stream);
extern int getchar_unlocked(void);
extern int fgetc_unlocked(FILE *__stream);
extern int fputc(int __c, FILE *__stream);
extern int putc(int __c, FILE *__stream);
extern int putchar(int __c);
extern int fputc_unlocked(int __c, FILE *__stream);
extern int putc_unlocked(int __c, FILE *__stream);
extern int putchar_unlocked(int __c);
extern int getw(FILE *__stream);
extern int putw(int __w, FILE *__stream);
extern char *fgets(char *__restrict __s, int __n, FILE *__restrict __stream);
extern char *fgets_unlocked(char *__restrict __s, int __n,
                            FILE *__restrict __stream);
extern __ssize_t __getdelim(char **__restrict __lineptr,
                            size_t *__restrict __n, int __delimiter,
                            FILE *__restrict __stream);
extern __ssize_t getdelim(char **__restrict __lineptr,
                          size_t *__restrict __n, int __delimiter,
                          FILE *__restrict __stream);
extern __ssize_t getline(char **__restrict __lineptr,
                         size_t *__restrict __n,
                         FILE *__restrict __stream);
extern int fputs(const char *__restrict __s, FILE *__restrict __stream);
extern int puts(const char *__s);
extern int ungetc(int __c, FILE *__stream);
extern size_t fread(void *__restrict __ptr, size_t __size,
                    size_t __n, FILE *__restrict __stream);
extern size_t fwrite(const void *__restrict __ptr, size_t __size,
                     size_t __n, FILE *__restrict __s);
extern int fputs_unlocked(const char *__restrict __s,
                          FILE *__restrict __stream);
extern size_t fread_unlocked(void *__restrict __ptr, size_t __size,
                             size_t __n, FILE *__restrict __stream);
extern size_t fwrite_unlocked(const void *__restrict __ptr, size_t __size,
                              size_t __n, FILE *__restrict __stream);
extern int fseek(FILE *__stream, long int __off, int __whence);
extern long int ftell(FILE *__stream);
extern void rewind(FILE *__stream);
extern int fseeko(FILE *__stream, __off_t __off, int __whence);
extern __off_t ftello(FILE *__stream);
extern int fgetpos(FILE *__restrict __stream, fpos_t *__restrict __pos);
extern int fsetpos(FILE *__stream, const fpos_t *__pos);
extern int fseeko64(FILE *__stream, __off64_t __off, int __whence);
extern __off64_t ftello64(FILE *__stream);
extern int fgetpos64(FILE *__restrict __stream, fpos64_t *__restrict __pos);
extern int fsetpos64(FILE *__stream, const fpos64_t *__pos);
extern void clearerr(FILE *__stream) __attribute__((__nothrow__));
extern int feof(FILE *__stream) __attribute__((__nothrow__));
extern int ferror(FILE *__stream) __attribute__((__nothrow__));
extern void clearerr_unlocked(FILE *__stream) __attribute__((__nothrow__));
extern int feof_unlocked(FILE *__stream) __attribute__((__nothrow__));
extern int ferror_unlocked(FILE *__stream) __attribute__((__nothrow__));
extern void perror(const char *__s);
extern int fileno(FILE *__stream) __attribute__((__nothrow__));
extern int fileno_unlocked(FILE *__stream) __attribute__((__nothrow__));
extern int pclose(FILE *__stream);
extern FILE *popen(const char *__command, const char *__modes)
    __attribute__((__malloc__));
extern char *ctermid(char *__s) __attribute__((__nothrow__));
extern char *cuserid(char *__s);
struct obstack;
extern int obstack_printf(struct obstack *__restrict __obstack,
                          const char *__restrict __format, ...)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 2, 3)));
extern int obstack_vprintf(struct obstack *__restrict __obstack,
                           const char *__restrict __format,
                           __gnuc_va_list __args)
    __attribute__((__nothrow__)) __attribute__((__format__(__printf__, 2, 0)));
extern void flockfile(FILE *__stream) __attribute__((__nothrow__));
extern int ftrylockfile(FILE *__stream) __attribute__((__nothrow__));
extern void funlockfile(FILE *__stream) __attribute__((__nothrow__));
extern int __uflow(FILE *);
extern int __overflow(FILE *, int);
extern int rpl_fclose(FILE *stream) __attribute__((__nonnull__(1)));
extern int ___var__1111;
;
;
extern int ___var__1112;
;
;
extern int ___var__1113;
;
;
extern int ___var__1114;
;
;
extern int ___var__1115;
;
;
extern int ___var__1116;
;
;
extern int rpl_fflush(FILE *gl_stream);
extern int ___var__1119;
;
;
extern int ___var__1120;
;
;
extern int ___var__1121;
;
;
extern int ___var__1122;
;
;
extern int ___var__1123;
;
;
extern int ___var__1124;
;
;
extern int ___var__1125;
;
;
extern int ___var__1126;
;
;
extern int rpl_fprintf(FILE *__restrict__ fp, const char *__restrict__ format, ...) __attribute__((__format__(__printf__, 2, 3))) __attribute__((__nonnull__(1, 2)));
extern int ___var__1131;
;
;
extern int ___var__1132;
;
;
extern int fpurge(FILE *gl_stream) __attribute__((__nonnull__(1)));
extern int ___var__1135;
;
;
extern int ___var__1136;
;
;
extern int ___var__1137;
;
;
extern int ___var__1138;
;
;
extern int ___var__1139;
;
;
extern int ___var__1140;
;
;
extern int ___var__1141;
;
;
extern int ___var__1142;
;
;
extern int ___var__1143;
;
;
extern int ___var__1144;
;
;
extern int rpl_fseek(FILE *fp, long offset, int whence) __attribute__((__nonnull__(1)));
extern int ___var__1151;
;
;
extern int ___var__1152;
;
;
extern int rpl_fseeko(FILE *fp, off_t offset, int whence) __attribute__((__nonnull__(1)));
extern int ___var__1159;
;
;
extern int ___var__1160;
;
;
extern int ___var__1161;
;
;
extern int ___var__1162;
;
;
extern int ___var__1163;
;
;
extern int ___var__1164;
;
;
extern int ___var__1165;
;
;
extern int ___var__1166;
;
;
extern int ___var__1167;
;
;
extern int ___var__1168;
;
;
extern int ___var__1169;
;
;
extern int ___var__1170;
;
;
extern int ___var__1171;
;
;
extern int ___var__1172;
;
;
extern int ___var__1173;
;
;
extern int ___var__1174;
;
;
extern int __printf__(const char *__restrict__ format, ...) __asm__(""
                                                                    "rpl_printf") __attribute__((__format__(__printf__, 1, 2))) __attribute__((__nonnull__(1)));
extern int ___var__1177;
;
;
extern int ___var__1178;
;
;
extern int ___var__1179;
;
;
extern int ___var__1180;
;
;
extern int ___var__1181;
;
;
extern int ___var__1182;
;
;
extern int ___var__1183;
;
;
extern int ___var__1184;
;
;
extern int ___var__1185;
;
;
extern int ___var__1186;
;
;
extern int ___var__1187;
;
;
extern int ___var__1188;
;
;
extern int ___var__1189;
;
;
extern int ___var__1190;
;
;
extern int rpl_vfprintf(FILE *__restrict__ fp, const char *__restrict__ format, va_list args) __attribute__((__format__(__printf__, 2, 0))) __attribute__((__nonnull__(1, 2)));
extern int ___var__1197;
;
;
extern int ___var__1198;
;
;
extern int ___var__1199;
;
;
extern int ___var__1200;
;
;
struct timex {
  unsigned int modes;
  __syscall_slong_t offset;
  __syscall_slong_t freq;
  __syscall_slong_t maxerror;
  __syscall_slong_t esterror;
  int status;
  __syscall_slong_t constant;
  __syscall_slong_t precision;
  __syscall_slong_t tolerance;
  struct timeval time;
  __syscall_slong_t tick;
  __syscall_slong_t ppsfreq;
  __syscall_slong_t jitter;
  int shift;
  __syscall_slong_t stabil;
  __syscall_slong_t jitcnt;
  __syscall_slong_t calcnt;
  __syscall_slong_t errcnt;
  __syscall_slong_t stbcnt;
  int tai;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
  int : 32;
};
extern int clock_adjtime(__clockid_t __clock_id, struct timex *__utx) __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
  long int tm_gmtoff;
  const char *tm_zone;
};
struct itimerspec {
  struct timespec it_interval;
  struct timespec it_value;
};
struct sigevent;
extern clock_t clock(void) __attribute__((__nothrow__));
extern time_t time(time_t *__timer) __attribute__((__nothrow__));
extern double difftime(time_t __time1, time_t __time0)
    __attribute__((__nothrow__)) __attribute__((__const__));
extern time_t mktime(struct tm *__tp) __attribute__((__nothrow__));
extern size_t strftime(char *__restrict __s, size_t __maxsize,
                       const char *__restrict __format,
                       const struct tm *__restrict __tp) __attribute__((__nothrow__));
extern char *strptime(const char *__restrict __s,
                      const char *__restrict __fmt, struct tm *__tp)
    __attribute__((__nothrow__));
extern size_t strftime_l(char *__restrict __s, size_t __maxsize,
                         const char *__restrict __format,
                         const struct tm *__restrict __tp,
                         locale_t __loc) __attribute__((__nothrow__));
extern char *strptime_l(const char *__restrict __s,
                        const char *__restrict __fmt, struct tm *__tp,
                        locale_t __loc) __attribute__((__nothrow__));
extern struct tm *gmtime(const time_t *__timer) __attribute__((__nothrow__));
extern struct tm *localtime(const time_t *__timer) __attribute__((__nothrow__));
extern struct tm *gmtime_r(const time_t *__restrict __timer,
                           struct tm *__restrict __tp) __attribute__((__nothrow__));
extern struct tm *localtime_r(const time_t *__restrict __timer,
                              struct tm *__restrict __tp) __attribute__((__nothrow__));
extern char *asctime(const struct tm *__tp) __attribute__((__nothrow__));
extern char *ctime(const time_t *__timer) __attribute__((__nothrow__));
extern char *asctime_r(const struct tm *__restrict __tp,
                       char *__restrict __buf) __attribute__((__nothrow__));
extern char *ctime_r(const time_t *__restrict __timer,
                     char *__restrict __buf) __attribute__((__nothrow__));
extern char *___var__1269[2];
;
;
extern int ___var__1270;
;
;
extern long ___var__1271;
;
;
extern char *___var__1272[2];
;
;
extern void tzset(void) __attribute__((__nothrow__));
extern int ___var__1273;
;
;
extern long ___var__1274;
;
;
extern time_t timegm(struct tm *__tp) __attribute__((__nothrow__));
extern time_t timelocal(struct tm *__tp) __attribute__((__nothrow__));
extern int dysize(int __year) __attribute__((__nothrow__)) __attribute__((__const__));
extern int nanosleep(const struct timespec *__requested_time,
                     struct timespec *__remaining);
extern int clock_getres(clockid_t __clock_id, struct timespec *__res) __attribute__((__nothrow__));
extern int clock_gettime(clockid_t __clock_id, struct timespec *__tp)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int clock_settime(clockid_t __clock_id, const struct timespec *__tp)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int clock_nanosleep(clockid_t __clock_id, int __flags,
                           const struct timespec *__req,
                           struct timespec *__rem);
extern int clock_getcpuclockid(pid_t __pid, clockid_t *__clock_id) __attribute__((__nothrow__));
extern int timer_create(clockid_t __clock_id,
                        struct sigevent *__restrict __evp,
                        timer_t *__restrict __timerid) __attribute__((__nothrow__));
extern int timer_delete(timer_t __timerid) __attribute__((__nothrow__));
extern int timer_settime(timer_t __timerid, int __flags,
                         const struct itimerspec *__restrict __value,
                         struct itimerspec *__restrict __ovalue) __attribute__((__nothrow__));
extern int timer_gettime(timer_t __timerid, struct itimerspec *__value)
    __attribute__((__nothrow__));
extern int timer_getoverrun(timer_t __timerid) __attribute__((__nothrow__));
extern int timespec_get(struct timespec *__ts, int __base)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int timespec_getres(struct timespec *__ts, int __base)
    __attribute__((__nothrow__));
extern int ___var__1339;
;
;
extern struct tm *getdate(const char *__string);
extern int getdate_r(const char *__restrict __string,
                     struct tm *__restrict __resbufp);
struct __time_t_must_be_integral {
  unsigned int __floating_time_t_unsupported : 1;
};
extern int ___var__1346;
;
;
extern int ___var__1347;
;
;
extern void *memcpy(void *__restrict __dest, const void *__restrict __src,
                    size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern void *memmove(void *__dest, const void *__src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern void *memccpy(void *__restrict __dest, const void *__restrict __src,
                     int __c, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern void *memset(void *__s, int __c, size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int memcmp(const void *__s1, const void *__s2, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int __memcmpeq(const void *__s1, const void *__s2, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern void *memchr(const void *__s, int __c, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern void *rawmemchr(const void *__s, int __c)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern void *memrchr(const void *__s, int __c, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern char *strcpy(char *__restrict __dest, const char *__restrict __src)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strncpy(char *__restrict __dest,
                     const char *__restrict __src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strcat(char *__restrict __dest, const char *__restrict __src)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strncat(char *__restrict __dest, const char *__restrict __src,
                     size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int strcmp(const char *__s1, const char *__s2)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int strncmp(const char *__s1, const char *__s2, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int strcoll(const char *__s1, const char *__s2)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern size_t strxfrm(char *__restrict __dest,
                      const char *__restrict __src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern int strcoll_l(const char *__s1, const char *__s2, locale_t __l)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2, 3)));
extern size_t strxfrm_l(char *__dest, const char *__src, size_t __n,
                        locale_t __l) __attribute__((__nothrow__)) __attribute__((__nonnull__(2, 4)));
extern char *strdup(const char *__s)
    __attribute__((__nothrow__)) __attribute__((__malloc__)) __attribute__((__nonnull__(1)));
extern char *strndup(const char *__string, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__malloc__)) __attribute__((__nonnull__(1)));
extern char *strchr(const char *__s, int __c)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern char *strrchr(const char *__s, int __c)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern char *strchrnul(const char *__s, int __c)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern size_t strcspn(const char *__s, const char *__reject)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern size_t strspn(const char *__s, const char *__accept)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern char *strpbrk(const char *__s, const char *__accept)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern char *strstr(const char *__haystack, const char *__needle)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern char *strtok(char *__restrict __s, const char *__restrict __delim)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern char *__strtok_r(char *__restrict __s,
                        const char *__restrict __delim,
                        char **__restrict __save_ptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2, 3)));
extern char *strtok_r(char *__restrict __s, const char *__restrict __delim,
                      char **__restrict __save_ptr)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2, 3)));
extern char *strcasestr(const char *__haystack, const char *__needle)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern void *memmem(const void *__haystack, size_t __haystacklen,
                    const void *__needle, size_t __needlelen)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 3)));
extern void *__mempcpy(void *__restrict __dest,
                       const void *__restrict __src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern void *mempcpy(void *__restrict __dest,
                     const void *__restrict __src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern size_t strlen(const char *__s)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern size_t strnlen(const char *__string, size_t __maxlen)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern char *strerror(int __errnum) __attribute__((__nothrow__));
extern char *strerror_r(int __errnum, char *__buf, size_t __buflen)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));
extern const char *strerrordesc_np(int __err) __attribute__((__nothrow__));
extern const char *strerrorname_np(int __err) __attribute__((__nothrow__));
extern char *strerror_l(int __errnum, locale_t __l) __attribute__((__nothrow__));
extern int bcmp(const void *__s1, const void *__s2, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern void bcopy(const void *__src, void *__dest, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern void bzero(void *__s, size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern char *index(const char *__s, int __c)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern char *rindex(const char *__s, int __c)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern int ffs(int __i) __attribute__((__nothrow__)) __attribute__((__const__));
extern int ffsl(long int __l) __attribute__((__nothrow__)) __attribute__((__const__));
__extension__ extern int ffsll(long long int __ll)
    __attribute__((__nothrow__)) __attribute__((__const__));
extern int strcasecmp(const char *__s1, const char *__s2)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int strncasecmp(const char *__s1, const char *__s2, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int strcasecmp_l(const char *__s1, const char *__s2, locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2, 3)));
extern int strncasecmp_l(const char *__s1, const char *__s2,
                         size_t __n, locale_t __loc)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2, 4)));
extern void explicit_bzero(void *__s, size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern char *strsep(char **__restrict __stringp,
                    const char *__restrict __delim)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strsignal(int __sig) __attribute__((__nothrow__));
extern const char *sigabbrev_np(int __sig) __attribute__((__nothrow__));
extern const char *sigdescr_np(int __sig) __attribute__((__nothrow__));
extern char *__stpcpy(char *__restrict __dest, const char *__restrict __src)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *stpcpy(char *__restrict __dest, const char *__restrict __src)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *__stpncpy(char *__restrict __dest,
                       const char *__restrict __src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *stpncpy(char *__restrict __dest,
                     const char *__restrict __src, size_t __n)
    __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern int strverscmp(const char *__s1, const char *__s2)
    __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern char *strfry(char *__string) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern void *memfrob(void *__s, size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern char *basename(const char *__filename) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern void free(void *);
extern int ___var__1654;
;
;
extern int ___var__1655;
;
;
extern int ___var__1656;
;
;
extern int ___var__1657;
;
;
extern int ___var__1658;
;
;
extern int ___var__1659;
;
;
extern int ___var__1660;
;
;
extern int ___var__1661;
;
;
extern int ___var__1662;
;
;
extern int ___var__1663;
;
;
extern int ___var__1664;
;
;
extern int ___var__1665;
;
;
extern int ___var__1666;
;
;
extern int ___var__1667;
;
;
extern int rpl_strerror_r(int errnum, char *buf, size_t buflen) __attribute__((__nonnull__(2)));
extern int ___var__1674;
;
;
extern int ___var__1675;
;
;
typedef unsigned char uch;
;
typedef unsigned short ush;
;
typedef unsigned long ulg;
;
extern int ___var__1676;
;
;
extern uch ___var__1677[];
;
;
extern uch ___var__1678[];
;
;
extern ush ___var__1679[];
;
;
extern uch ___var__1680[];
;
;
extern ush ___var__1681[];
;
;
extern unsigned int ___var__1682;
;
;
extern unsigned int ___var__1683;
;
;
extern unsigned int ___var__1684;
;
;
extern int ___var__1685;
;
;
extern off_t ___var__1686;
;
;
extern off_t ___var__1687;
;
;
extern off_t ___var__1688;
;
;
extern int ___var__1689;
;
;
extern int ___var__1690;
;
;
extern char ___var__1691[];
;
;
extern char ___var__1692[];
;
;
extern char *___var__1693;
;
;
extern struct timespec ___var__1694;
;
;
extern off_t ___var__1695;
;
;
typedef int file_t;
;
extern int ___var__1696;
;
;
extern int ___var__1697;
;
;
extern int ___var__1698;
;
;
extern int ___var__1699;
;
;
extern int ___var__1700;
;
;
extern int ___var__1701;
;
;
extern int zip(int in, int out);
extern int file_read(char *buf, unsigned size);
extern ulg ___var__1710;
;
;
extern int unzip(int in, int out);
extern int check_zipfile(int in);
extern int unpack(int in, int out);
extern int unlzh(int in, int out);
extern _Noreturn void abort_gzip(void);
extern off_t deflate(int pack_level);
extern void ct_init(ush *attr, int *method);
extern int ct_tally(int dist, int lc);
extern off_t flush_block(char *buf, ulg stored_len, int pad, int eof);
extern void bi_init(file_t zipfile);
extern void send_bits(int value, int length);
extern unsigned bi_reverse(unsigned value, int length) __attribute__((__const__));
extern void bi_windup(void);
extern void copy_block(char *buf, unsigned len, int header);
extern int (*___var__1759)(char *, unsigned int);
;
;
extern int copy(int in, int out);
extern ulg updcrc(const uch *s, unsigned n);
extern ulg getcrc(void) __attribute__((__pure__));
extern void setcrc(ulg c);
extern void clear_bufs(void);
extern int fill_inbuf(int eof_ok);
extern void flush_outbuf(void);
extern void flush_window(void);
extern void write_buf(int fd, voidp buf, unsigned cnt);
extern int read_buffer(int fd, voidp buf, unsigned int cnt);
extern char *strlwr(char *s);
extern char *gzip_base_name(char *fname) __attribute__((__pure__));
extern int xunlink(char *fname);
extern void make_simple_name(char *name);
extern char *add_envopt(int *argcp, char ***argvp, char const *env);
extern _Noreturn void gzip_error(char const *m);
extern _Noreturn void xalloc_die(void);
extern void warning(char const *m);
extern _Noreturn void read_error(void);
extern _Noreturn void write_error(void);
extern void display_ratio(off_t num, off_t den, FILE *file);
extern void fprint_off(FILE *, off_t, int);
extern int inflate(void);
struct huft {
  uch e;
  uch b;
  union {
    ush n;
    struct huft *t;
  } v;
};
static int huft_free(struct huft *);
static unsigned int ___var__1820[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
;
;
static ush ___var__1821[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
;
;
static ush ___var__1822[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99};
;
;
static ush ___var__1823[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
;
;
static ush ___var__1824[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
;
;
static ulg ___var__1825;
;
;
static unsigned int ___var__1826;
;
;
static ush ___var__1827[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767, 65535};
;
;
static int ___var__1828 = 9;
;
;
static int ___var__1829 = 6;
;
;
static unsigned int ___var__1830;
;
;
static int huft_build(unsigned int *___var__1832, unsigned int ___var__1834, unsigned int ___var__1836, ush *___var__1838, ush *___var__1840, struct huft **___var__1842, int *___var__1844);
static int huft_free(struct huft *___var__1865);
static int inflate_codes(struct huft *___var__1869, struct huft *___var__1871, int ___var__1873, int ___var__1875);
static int inflate_stored(void);
static int inflate_fixed(void);
static int inflate_dynamic(void);
static int inflate_block(int *___var__1913);
int inflate(void);

static inline __uint16_t __bswap_16(__uint16_t ___var__1858);

static inline __uint32_t __bswap_32(__uint32_t ___var__1860);

static inline __uint64_t __bswap_64(__uint64_t ___var__1862);

static inline __uint16_t __uint16_identity(__uint16_t ___var__1864);

static inline __uint32_t __uint32_identity(__uint32_t ___var__1866);

static inline __uint64_t __uint64_identity(__uint64_t ___var__1868);

static int huft_build(unsigned int *___var__1870, unsigned int ___var__1872, unsigned int ___var__1874, ush *___var__1876, ush *___var__1878, struct huft **___var__1880, int *___var__1882);

static int huft_free(struct huft *___var__1910);

static int inflate_codes(struct huft *___var__1914, struct huft *___var__1916, int ___var__1918, int ___var__1920);

static int inflate_stored(void);

static int inflate_fixed(void);

static int inflate_dynamic(void);

static int inflate_block(int *___var__2044);

int inflate(void);

static inline __uint16_t __bswap_16(__uint16_t ___var__1858){
    return ((__uint16_t)((((___var__1858) >> 8) & 255) | (((___var__1858) & 255) << 8)));
}



static inline __uint32_t __bswap_32(__uint32_t ___var__1860){
    return ((((___var__1860) & 4278190080U) >> 24) | (((___var__1860) & 16711680U) >> 8) | (((___var__1860) & 65280U) << 8) | (((___var__1860) & 255U) << 24));
}



static inline __uint64_t __bswap_64(__uint64_t ___var__1862){
    return ((((___var__1862) & 18374686479671623680ULL) >> 56) | (((___var__1862) & 71776119061217280ULL) >> 40) | (((___var__1862) & 280375465082880ULL) >> 24) | (((___var__1862) & 1095216660480ULL) >> 8) | (((___var__1862) & 4278190080ULL) << 8) | (((___var__1862) & 16711680ULL) << 24) | (((___var__1862) & 65280ULL) << 40) | (((___var__1862) & 255ULL) << 56));
}



static inline __uint16_t __uint16_identity(__uint16_t ___var__1864){
    return ___var__1864;
}



static inline __uint32_t __uint32_identity(__uint32_t ___var__1866){
    return ___var__1866;
}



static inline __uint64_t __uint64_identity(__uint64_t ___var__1868){
    return ___var__1868;
}



static int huft_build(unsigned int *___var__1870, unsigned int ___var__1872, unsigned int ___var__1874, ush *___var__1876, ush *___var__1878, struct huft **___var__1880, int *___var__1882){
    unsigned int ___var__1883;
    unsigned int ___var__1884[17];
    unsigned int ___var__1885;
    int ___var__1886;
    int ___var__1887;
    register unsigned int ___var__1888;
    register unsigned int ___var__1889;
    register int ___var__1890;
    int ___var__1891;
    register unsigned int *___var__1892;
    register struct huft *___var__1893;
    struct huft ___var__1894;
    struct huft *___var__1895[16];
    unsigned int ___var__1896[288];
    register int ___var__1897;
    unsigned int ___var__1898[17];
    unsigned int *___var__1899;
    int ___var__1900;
    unsigned int ___var__1901;
    void *___var__1902 = memset((voidp)(___var__1884), 0, 68);
    ___var__1902;
    ___var__1892 = ___var__1870;
    ___var__1888 = ___var__1872;
    do {
        ;
        ___var__1884[*___var__1892]++;
        ___var__1892++;
    } while (--___var__1888);
    {
        {
            if (___var__1884[0] == ___var__1872) {
                void *___var__1903 = malloc(48);
                ___var__1893 = (struct huft *)___var__1903;
                {
                    {
                        if (!___var__1893) {
                            return 3;
                        }
                    }
                }
                ___var__1830 += 3;
                ___var__1893[0].v.t = (struct huft *)((void *)0);
                ___var__1893[1].e = 99;
                ___var__1893[1].b = 1;
                ___var__1893[2].e = 99;
                ___var__1893[2].b = 1;
                *___var__1880 = ___var__1893 + 1;
                *___var__1882 = 1;
                return 0;
            }
        }
    }
    ___var__1891 = *___var__1882;
    {
        {
            for (___var__1889 = 1; ___var__1889 <= 16; ___var__1889++) {
                {
                    {
                        if (___var__1884[___var__1889]) {
                            break;
                        }
                    }
                }
            }
        }
    }
    ___var__1890 = ___var__1889;
    {
        {
            if ((unsigned int)___var__1891 < ___var__1889) {
                ___var__1891 = ___var__1889;
            }
        }
    }
    {
        {
            for (___var__1888 = 16; ___var__1888; ___var__1888--) {
                {
                    {
                        if (___var__1884[___var__1888]) {
                            break;
                        }
                    }
                }
            }
        }
    }
    ___var__1886 = ___var__1888;
    {
        {
            if ((unsigned int)___var__1891 > ___var__1888) {
                ___var__1891 = ___var__1888;
            }
        }
    }
    *___var__1882 = ___var__1891;
    {
        {
            for (___var__1900 = 1 << ___var__1889; ___var__1889 < ___var__1888; (___var__1889++ , ___var__1900 <<= 1)) {
                {
                    {
                        if ((___var__1900 -= ___var__1884[___var__1889]) < 0) {
                            return 2;
                        }
                    }
                }
            }
        }
    }
    {
        {
            if ((___var__1900 -= ___var__1884[___var__1888]) < 0) {
                return 2;
            }
        }
    }
    ___var__1884[___var__1888] += ___var__1900;
    ___var__1898[1] = ___var__1889 = 0;
    ___var__1892 = ___var__1884 + 1;
    ___var__1899 = ___var__1898 + 2;
    while (--___var__1888)
        {
            *___var__1899++ = (___var__1889 += *___var__1892++);
        }
    ___var__1892 = ___var__1870;
    ___var__1888 = 0;
    do {
        {
            {
                if ((___var__1889 = *___var__1892++) != 0) {
                    ___var__1896[___var__1898[___var__1889]++] = ___var__1888;
                }
            }
        }
    } while (++___var__1888 < ___var__1872);
    ___var__1872 = ___var__1898[___var__1886];
    ___var__1898[0] = ___var__1888 = 0;
    ___var__1892 = ___var__1896;
    ___var__1887 = -1;
    ___var__1897 = -___var__1891;
    ___var__1895[0] = (struct huft *)((void *)0);
    ___var__1893 = (struct huft *)((void *)0);
    ___var__1901 = 0;
    {
        {
            for (; ___var__1890 <= ___var__1886; ___var__1890++) {
                ___var__1883 = ___var__1884[___var__1890];
                while (___var__1883--)
                    {
                        while (___var__1890 > ___var__1897 + ___var__1891)
                            {
                                ___var__1887++;
                                ___var__1897 += ___var__1891;
                                ___var__1901 = (___var__1901 = ___var__1886 - ___var__1897) > (unsigned int)___var__1891 ? ___var__1891 : ___var__1901;
                                {
                                    {
                                        if ((___var__1885 = 1 << (___var__1889 = ___var__1890 - ___var__1897)) > ___var__1883 + 1) {
                                            ___var__1885 -= ___var__1883 + 1;
                                            ___var__1899 = ___var__1884 + ___var__1890;
                                            {
                                                {
                                                    if (___var__1889 < ___var__1901) {
                                                        while (++___var__1889 < ___var__1901)
                                                            {
                                                                {
                                                                    {
                                                                        if ((___var__1885 <<= 1) <= *++___var__1899) {
                                                                            break;
                                                                        }
                                                                    }
                                                                }
                                                                ___var__1885 -= *___var__1899;
                                                            }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                ___var__1901 = 1 << ___var__1889;
                                {
                                    void *___var__1904 = malloc((___var__1901 + 1) * 16);
                                    {
                                        if ((___var__1893 = (struct huft *)___var__1904) == (struct huft *)((void *)0)) {
                                            {
                                                {
                                                    if (___var__1887) {
                                                        int ___var__1905;
                                                        {
                                                            struct huft *___var__1906 = ___var__1895[0];
                                                            register struct huft *___var__1907;
                                                            register struct huft *___var__1908;
                                                            ___var__1907 = ___var__1906;
                                                            while (___var__1907 != (struct huft *)((void *)0))
                                                                {
                                                                    ___var__1908 = (--___var__1907)->v.t;
                                                                    free(___var__1907);
                                                                    ___var__1907 = ___var__1908;
                                                                }
                                                            ___var__1905 = 0;
                                                        }
                                                        ___var__1905;
                                                    }
                                                }
                                            }
                                            return 3;
                                        }
                                    }
                                }
                                ___var__1830 += ___var__1901 + 1;
                                *___var__1880 = ___var__1893 + 1;
                                *(___var__1880 = &(___var__1893->v.t)) = (struct huft *)((void *)0);
                                ___var__1895[___var__1887] = ++___var__1893;
                                {
                                    {
                                        if (___var__1887) {
                                            ___var__1898[___var__1887] = ___var__1888;
                                            ___var__1894.b = (uch)___var__1891;
                                            ___var__1894.e = (uch)(16 + ___var__1889);
                                            ___var__1894.v.t = ___var__1893;
                                            ___var__1889 = ___var__1888 >> (___var__1897 - ___var__1891);
                                            ___var__1895[___var__1887 - 1][___var__1889] = ___var__1894;
                                        }
                                    }
                                }
                            }
                        ___var__1894.b = (uch)(___var__1890 - ___var__1897);
                        {
                            {
                                if (___var__1892 >= ___var__1896 + ___var__1872) {
                                    ___var__1894.e = 99;
                                } else {
                                    {
                                        if (*___var__1892 < ___var__1874) {
                                            ___var__1894.e = (uch)(*___var__1892 < 256 ? 16 : 15);
                                            ___var__1894.v.n = (ush)(*___var__1892);
                                            ___var__1892++;
                                        } else {
                                            ___var__1894.e = (uch)___var__1878[*___var__1892 - ___var__1874];
                                            ___var__1894.v.n = ___var__1876[*___var__1892++ - ___var__1874];
                                        }
                                    }
                                }
                            }
                        }
                        ___var__1885 = 1 << (___var__1890 - ___var__1897);
                        {
                            {
                                for (___var__1889 = ___var__1888 >> ___var__1897; ___var__1889 < ___var__1901; ___var__1889 += ___var__1885) {
                                    ___var__1893[___var__1889] = ___var__1894;
                                }
                            }
                        }
                        {
                            {
                                for (___var__1889 = 1 << (___var__1890 - 1); ___var__1888 & ___var__1889; ___var__1889 >>= 1) {
                                    ___var__1888 ^= ___var__1889;
                                }
                            }
                        }
                        ___var__1888 ^= ___var__1889;
                        while ((___var__1888 & ((1 << ___var__1897) - 1)) != ___var__1898[___var__1887])
                            {
                                ___var__1887--;
                                ___var__1897 -= ___var__1891;
                            }
                    }
            }
        }
    }
    return ___var__1900 != 0 && ___var__1886 != 1;
}



static int huft_free(struct huft *___var__1910){
    register struct huft *___var__1911;
    register struct huft *___var__1912;
    ___var__1911 = ___var__1910;
    while (___var__1911 != (struct huft *)((void *)0))
        {
            ___var__1912 = (--___var__1911)->v.t;
            free(___var__1911);
            ___var__1911 = ___var__1912;
        }
    return 0;
}



static int inflate_codes(struct huft *___var__1914, struct huft *___var__1916, int ___var__1918, int ___var__1920){
    register unsigned int ___var__1921;
    unsigned int ___var__1922;
    unsigned int ___var__1923;
    unsigned int ___var__1924;
    struct huft *___var__1925;
    unsigned int ___var__1926;
    unsigned int ___var__1927;
    register ulg ___var__1928;
    register unsigned int ___var__1929;
    ___var__1928 = ___var__1825;
    ___var__1929 = ___var__1826;
    ___var__1924 = ___var__1684;
    ___var__1926 = ___var__1827[___var__1918];
    ___var__1927 = ___var__1827[___var__1920];
    {
        {
            for (;;) {
                {
                    while (___var__1929 < ((unsigned int)___var__1918))
                        {
                            int ___var__1930;
                            {
                                if (___var__1683 < ___var__1682) {
                                    ___var__1930 = ___var__1677[___var__1683++];
                                } else {
                                    int ___var__1931 = fill_inbuf(0);
                                    ___var__1930 = (___var__1684 = ___var__1924 , ___var__1931);
                                }
                            }
                            ___var__1928 |= ((ulg)(uch)(___var__1930)) << ___var__1929;
                            ___var__1929 += 8;
                        }
                }
                {
                    {
                        if ((___var__1921 = (___var__1925 = ___var__1914 + ((unsigned int)___var__1928 & ___var__1926))->e) > 16) {
                            do {
                                {
                                    {
                                        if (___var__1921 == 99) {
                                            return 1;
                                        }
                                    }
                                }
                                {
                                    ___var__1928 >>= (___var__1925->b);
                                    ___var__1929 -= (___var__1925->b);
                                }
                                ___var__1921 -= 16;
                                {
                                    while (___var__1929 < (___var__1921))
                                        {
                                            int ___var__1932;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__1932 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__1933 = fill_inbuf(0);
                                                    ___var__1932 = (___var__1684 = ___var__1924 , ___var__1933);
                                                }
                                            }
                                            ___var__1928 |= ((ulg)(uch)(___var__1932)) << ___var__1929;
                                            ___var__1929 += 8;
                                        }
                                }
                            } while ((___var__1921 = (___var__1925 = ___var__1925->v.t + ((unsigned int)___var__1928 & ___var__1827[___var__1921]))->e) > 16);
                        }
                    }
                }
                {
                    ___var__1928 >>= (___var__1925->b);
                    ___var__1929 -= (___var__1925->b);
                }
                {
                    {
                        if (___var__1921 == 16) {
                            ___var__1680[___var__1924++] = (uch)___var__1925->v.n;
                            ;
                            {
                                {
                                    if (___var__1924 == 32768) {
                                        flush_window();
                                        (___var__1684 = (___var__1924) , (void)0);
                                        ___var__1924 = 0;
                                    }
                                }
                            }
                        } else {
                            {
                                {
                                    if (___var__1921 == 15) {
                                        break;
                                    }
                                }
                            }
                            {
                                while (___var__1929 < (___var__1921))
                                    {
                                        int ___var__1934;
                                        {
                                            if (___var__1683 < ___var__1682) {
                                                ___var__1934 = ___var__1677[___var__1683++];
                                            } else {
                                                int ___var__1935 = fill_inbuf(0);
                                                ___var__1934 = (___var__1684 = ___var__1924 , ___var__1935);
                                            }
                                        }
                                        ___var__1928 |= ((ulg)(uch)(___var__1934)) << ___var__1929;
                                        ___var__1929 += 8;
                                    }
                            }
                            ___var__1922 = ___var__1925->v.n + ((unsigned int)___var__1928 & ___var__1827[___var__1921]);
                            {
                                ___var__1928 >>= (___var__1921);
                                ___var__1929 -= (___var__1921);
                            }
                            ;
                            {
                                while (___var__1929 < ((unsigned int)___var__1920))
                                    {
                                        int ___var__1936;
                                        {
                                            if (___var__1683 < ___var__1682) {
                                                ___var__1936 = ___var__1677[___var__1683++];
                                            } else {
                                                int ___var__1937 = fill_inbuf(0);
                                                ___var__1936 = (___var__1684 = ___var__1924 , ___var__1937);
                                            }
                                        }
                                        ___var__1928 |= ((ulg)(uch)(___var__1936)) << ___var__1929;
                                        ___var__1929 += 8;
                                    }
                            }
                            {
                                {
                                    if ((___var__1921 = (___var__1925 = ___var__1916 + ((unsigned int)___var__1928 & ___var__1927))->e) > 16) {
                                        do {
                                            {
                                                {
                                                    if (___var__1921 == 99) {
                                                        return 1;
                                                    }
                                                }
                                            }
                                            {
                                                ___var__1928 >>= (___var__1925->b);
                                                ___var__1929 -= (___var__1925->b);
                                            }
                                            ___var__1921 -= 16;
                                            {
                                                while (___var__1929 < (___var__1921))
                                                    {
                                                        int ___var__1938;
                                                        {
                                                            if (___var__1683 < ___var__1682) {
                                                                ___var__1938 = ___var__1677[___var__1683++];
                                                            } else {
                                                                int ___var__1939 = fill_inbuf(0);
                                                                ___var__1938 = (___var__1684 = ___var__1924 , ___var__1939);
                                                            }
                                                        }
                                                        ___var__1928 |= ((ulg)(uch)(___var__1938)) << ___var__1929;
                                                        ___var__1929 += 8;
                                                    }
                                            }
                                        } while ((___var__1921 = (___var__1925 = ___var__1925->v.t + ((unsigned int)___var__1928 & ___var__1827[___var__1921]))->e) > 16);
                                    }
                                }
                            }
                            {
                                ___var__1928 >>= (___var__1925->b);
                                ___var__1929 -= (___var__1925->b);
                            }
                            {
                                while (___var__1929 < (___var__1921))
                                    {
                                        int ___var__1940;
                                        {
                                            if (___var__1683 < ___var__1682) {
                                                ___var__1940 = ___var__1677[___var__1683++];
                                            } else {
                                                int ___var__1941 = fill_inbuf(0);
                                                ___var__1940 = (___var__1684 = ___var__1924 , ___var__1941);
                                            }
                                        }
                                        ___var__1928 |= ((ulg)(uch)(___var__1940)) << ___var__1929;
                                        ___var__1929 += 8;
                                    }
                            }
                            ___var__1923 = ___var__1924 - ___var__1925->v.n - ((unsigned int)___var__1928 & ___var__1827[___var__1921]);
                            {
                                ___var__1928 >>= (___var__1921);
                                ___var__1929 -= (___var__1921);
                            }
                            ;
                            do {
                                ___var__1922 -= (___var__1921 = (___var__1921 = 32768 - ((___var__1923 &= 32767) > ___var__1924 ? ___var__1923 : ___var__1924)) > ___var__1922 ? ___var__1922 : ___var__1921);
                                {
                                    {
                                        if (___var__1921 <= (___var__1923 < ___var__1924 ? ___var__1924 - ___var__1923 : ___var__1923 - ___var__1924)) {
                                            void *___var__1942 = memcpy(___var__1680 + ___var__1924, ___var__1680 + ___var__1923, ___var__1921);
                                            ___var__1942;
                                            ___var__1924 += ___var__1921;
                                            ___var__1923 += ___var__1921;
                                        } else {
                                            do {
                                                ___var__1680[___var__1924++] = ___var__1680[___var__1923++];
                                                ;
                                            } while (--___var__1921);
                                        }
                                    }
                                }
                                {
                                    {
                                        if (___var__1924 == 32768) {
                                            flush_window();
                                            (___var__1684 = (___var__1924) , (void)0);
                                            ___var__1924 = 0;
                                        }
                                    }
                                }
                            } while (___var__1922);
                        }
                    }
                }
            }
        }
    }
    ___var__1684 = ___var__1924;
    ___var__1825 = ___var__1928;
    ___var__1826 = ___var__1929;
    return 0;
}



static int inflate_stored(void){
    unsigned int ___var__1943;
    unsigned int ___var__1944;
    register ulg ___var__1945;
    register unsigned int ___var__1946;
    ___var__1945 = ___var__1825;
    ___var__1946 = ___var__1826;
    ___var__1944 = ___var__1684;
    ___var__1943 = ___var__1946 & 7;
    {
        ___var__1945 >>= (___var__1943);
        ___var__1946 -= (___var__1943);
    }
    ;
    {
        while (___var__1946 < 16)
            {
                int ___var__1947;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__1947 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__1948 = fill_inbuf(0);
                        ___var__1947 = (___var__1684 = ___var__1944 , ___var__1948);
                    }
                }
                ___var__1945 |= ((ulg)(uch)(___var__1947)) << ___var__1946;
                ___var__1946 += 8;
            }
    }
    ___var__1943 = ((unsigned int)___var__1945 & 65535);
    {
        ___var__1945 >>= 16;
        ___var__1946 -= 16;
    }
    {
        while (___var__1946 < 16)
            {
                int ___var__1949;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__1949 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__1950 = fill_inbuf(0);
                        ___var__1949 = (___var__1684 = ___var__1944 , ___var__1950);
                    }
                }
                ___var__1945 |= ((ulg)(uch)(___var__1949)) << ___var__1946;
                ___var__1946 += 8;
            }
    }
    {
        {
            if (___var__1943 != (unsigned int)((~___var__1945) & 65535)) {
                return 1;
            }
        }
    }
    {
        ___var__1945 >>= 16;
        ___var__1946 -= 16;
    }
    while (___var__1943--)
        {
            {
                while (___var__1946 < 8)
                    {
                        int ___var__1951;
                        {
                            if (___var__1683 < ___var__1682) {
                                ___var__1951 = ___var__1677[___var__1683++];
                            } else {
                                int ___var__1952 = fill_inbuf(0);
                                ___var__1951 = (___var__1684 = ___var__1944 , ___var__1952);
                            }
                        }
                        ___var__1945 |= ((ulg)(uch)(___var__1951)) << ___var__1946;
                        ___var__1946 += 8;
                    }
            }
            ___var__1680[___var__1944++] = (uch)___var__1945;
            {
                {
                    if (___var__1944 == 32768) {
                        flush_window();
                        (___var__1684 = (___var__1944) , (void)0);
                        ___var__1944 = 0;
                    }
                }
            }
            {
                ___var__1945 >>= 8;
                ___var__1946 -= 8;
            }
        }
    ___var__1684 = ___var__1944;
    ___var__1825 = ___var__1945;
    ___var__1826 = ___var__1946;
    return 0;
}



static int inflate_fixed(void){
    int ___var__1953;
    struct huft *___var__1954;
    struct huft *___var__1955;
    int ___var__1956;
    int ___var__1957;
    unsigned int ___var__1958[288];
    {
        {
            for (___var__1953 = 0; ___var__1953 < 144; ___var__1953++) {
                ___var__1958[___var__1953] = 8;
            }
        }
    }
    {
        {
            for (; ___var__1953 < 256; ___var__1953++) {
                ___var__1958[___var__1953] = 9;
            }
        }
    }
    {
        {
            for (; ___var__1953 < 280; ___var__1953++) {
                ___var__1958[___var__1953] = 7;
            }
        }
    }
    {
        {
            for (; ___var__1953 < 288; ___var__1953++) {
                ___var__1958[___var__1953] = 8;
            }
        }
    }
    ___var__1956 = 7;
    {
        int ___var__1959 = huft_build(___var__1958, 288, 257, ___var__1821, ___var__1822, &___var__1954, &___var__1956);
        {
            if ((___var__1953 = ___var__1959) != 0) {
                return ___var__1953;
            }
        }
    }
    {
        {
            for (___var__1953 = 0; ___var__1953 < 30; ___var__1953++) {
                ___var__1958[___var__1953] = 5;
            }
        }
    }
    ___var__1957 = 5;
    {
        int ___var__1960 = huft_build(___var__1958, 30, 0, ___var__1823, ___var__1824, &___var__1955, &___var__1957);
        {
            if ((___var__1953 = ___var__1960) > 1) {
                int ___var__1961;
                {
                    struct huft *___var__1962 = ___var__1954;
                    register struct huft *___var__1963;
                    register struct huft *___var__1964;
                    ___var__1963 = ___var__1962;
                    while (___var__1963 != (struct huft *)((void *)0))
                        {
                            ___var__1964 = (--___var__1963)->v.t;
                            free(___var__1963);
                            ___var__1963 = ___var__1964;
                        }
                    ___var__1961 = 0;
                }
                ___var__1961;
                return ___var__1953;
            }
        }
    }
    {
        int ___var__1965 = inflate_codes(___var__1954, ___var__1955, ___var__1956, ___var__1957);
        {
            if (___var__1965) {
                return 1;
            }
        }
    }
    int ___var__1966;
    {
        struct huft *___var__1967 = ___var__1954;
        register struct huft *___var__1968;
        register struct huft *___var__1969;
        ___var__1968 = ___var__1967;
        while (___var__1968 != (struct huft *)((void *)0))
            {
                ___var__1969 = (--___var__1968)->v.t;
                free(___var__1968);
                ___var__1968 = ___var__1969;
            }
        ___var__1966 = 0;
    }
    ___var__1966;
    int ___var__1970;
    {
        struct huft *___var__1971 = ___var__1955;
        register struct huft *___var__1972;
        register struct huft *___var__1973;
        ___var__1972 = ___var__1971;
        while (___var__1972 != (struct huft *)((void *)0))
            {
                ___var__1973 = (--___var__1972)->v.t;
                free(___var__1972);
                ___var__1972 = ___var__1973;
            }
        ___var__1970 = 0;
    }
    ___var__1970;
    return 0;
}



static int inflate_dynamic(void){
    int ___var__1974;
    unsigned int ___var__1975;
    unsigned int ___var__1976;
    unsigned int ___var__1977;
    unsigned int ___var__1978;
    unsigned int ___var__1979;
    struct huft *___var__1980;
    struct huft *___var__1981;
    int ___var__1982;
    int ___var__1983;
    unsigned int ___var__1984;
    unsigned int ___var__1985;
    unsigned int ___var__1986;
    unsigned int ___var__1987[316];
    register ulg ___var__1988;
    register unsigned int ___var__1989;
    ___var__1988 = ___var__1825;
    ___var__1989 = ___var__1826;
    ___var__1979 = ___var__1684;
    {
        while (___var__1989 < 5)
            {
                int ___var__1990;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__1990 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__1991 = fill_inbuf(0);
                        ___var__1990 = (___var__1684 = ___var__1979 , ___var__1991);
                    }
                }
                ___var__1988 |= ((ulg)(uch)(___var__1990)) << ___var__1989;
                ___var__1989 += 8;
            }
    }
    ___var__1985 = 257 + ((unsigned int)___var__1988 & 31);
    {
        ___var__1988 >>= 5;
        ___var__1989 -= 5;
    }
    {
        while (___var__1989 < 5)
            {
                int ___var__1992;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__1992 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__1993 = fill_inbuf(0);
                        ___var__1992 = (___var__1684 = ___var__1979 , ___var__1993);
                    }
                }
                ___var__1988 |= ((ulg)(uch)(___var__1992)) << ___var__1989;
                ___var__1989 += 8;
            }
    }
    ___var__1986 = 1 + ((unsigned int)___var__1988 & 31);
    {
        ___var__1988 >>= 5;
        ___var__1989 -= 5;
    }
    {
        while (___var__1989 < 4)
            {
                int ___var__1994;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__1994 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__1995 = fill_inbuf(0);
                        ___var__1994 = (___var__1684 = ___var__1979 , ___var__1995);
                    }
                }
                ___var__1988 |= ((ulg)(uch)(___var__1994)) << ___var__1989;
                ___var__1989 += 8;
            }
    }
    ___var__1984 = 4 + ((unsigned int)___var__1988 & 15);
    {
        ___var__1988 >>= 4;
        ___var__1989 -= 4;
    }
    {
        {
            if (___var__1985 > 286 || ___var__1986 > 30) {
                return 1;
            }
        }
    }
    {
        {
            for (___var__1975 = 0; ___var__1975 < ___var__1984; ___var__1975++) {
                {
                    while (___var__1989 < 3)
                        {
                            int ___var__1996;
                            {
                                if (___var__1683 < ___var__1682) {
                                    ___var__1996 = ___var__1677[___var__1683++];
                                } else {
                                    int ___var__1997 = fill_inbuf(0);
                                    ___var__1996 = (___var__1684 = ___var__1979 , ___var__1997);
                                }
                            }
                            ___var__1988 |= ((ulg)(uch)(___var__1996)) << ___var__1989;
                            ___var__1989 += 8;
                        }
                }
                ___var__1987[___var__1820[___var__1975]] = (unsigned int)___var__1988 & 7;
                {
                    ___var__1988 >>= 3;
                    ___var__1989 -= 3;
                }
            }
        }
    }
    {
        {
            for (; ___var__1975 < 19; ___var__1975++) {
                ___var__1987[___var__1820[___var__1975]] = 0;
            }
        }
    }
    ___var__1982 = 7;
    {
        int ___var__1998 = huft_build(___var__1987, 19, 19, ((void *)0), ((void *)0), &___var__1980, &___var__1982);
        {
            if ((___var__1974 = ___var__1998) != 0) {
                {
                    {
                        if (___var__1974 == 1) {
                            int ___var__1999;
                            {
                                struct huft *___var__2000 = ___var__1980;
                                register struct huft *___var__2001;
                                register struct huft *___var__2002;
                                ___var__2001 = ___var__2000;
                                while (___var__2001 != (struct huft *)((void *)0))
                                    {
                                        ___var__2002 = (--___var__2001)->v.t;
                                        free(___var__2001);
                                        ___var__2001 = ___var__2002;
                                    }
                                ___var__1999 = 0;
                            }
                            ___var__1999;
                        }
                    }
                }
                return ___var__1974;
            }
        }
    }
    {
        {
            if (___var__1980 == ((void *)0)) {
                return 2;
            }
        }
    }
    ___var__1978 = ___var__1985 + ___var__1986;
    ___var__1977 = ___var__1827[___var__1982];
    ___var__1974 = ___var__1976 = 0;
    while ((unsigned int)___var__1974 < ___var__1978)
        {
            {
                while (___var__1989 < ((unsigned int)___var__1982))
                    {
                        int ___var__2003;
                        {
                            if (___var__1683 < ___var__1682) {
                                ___var__2003 = ___var__1677[___var__1683++];
                            } else {
                                int ___var__2004 = fill_inbuf(0);
                                ___var__2003 = (___var__1684 = ___var__1979 , ___var__2004);
                            }
                        }
                        ___var__1988 |= ((ulg)(uch)(___var__2003)) << ___var__1989;
                        ___var__1989 += 8;
                    }
            }
            ___var__1975 = (___var__1981 = ___var__1980 + ((unsigned int)___var__1988 & ___var__1977))->b;
            {
                ___var__1988 >>= (___var__1975);
                ___var__1989 -= (___var__1975);
            }
            {
                {
                    if (___var__1981->e == 99) {
                        int ___var__2005;
                        {
                            struct huft *___var__2006 = ___var__1980;
                            register struct huft *___var__2007;
                            register struct huft *___var__2008;
                            ___var__2007 = ___var__2006;
                            while (___var__2007 != (struct huft *)((void *)0))
                                {
                                    ___var__2008 = (--___var__2007)->v.t;
                                    free(___var__2007);
                                    ___var__2007 = ___var__2008;
                                }
                            ___var__2005 = 0;
                        }
                        ___var__2005;
                        return 2;
                    }
                }
            }
            ___var__1975 = ___var__1981->v.n;
            {
                {
                    if (___var__1975 < 16) {
                        ___var__1987[___var__1974++] = ___var__1976 = ___var__1975;
                    } else {
                        {
                            if (___var__1975 == 16) {
                                {
                                    while (___var__1989 < 2)
                                        {
                                            int ___var__2009;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2009 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2010 = fill_inbuf(0);
                                                    ___var__2009 = (___var__1684 = ___var__1979 , ___var__2010);
                                                }
                                            }
                                            ___var__1988 |= ((ulg)(uch)(___var__2009)) << ___var__1989;
                                            ___var__1989 += 8;
                                        }
                                }
                                ___var__1975 = 3 + ((unsigned int)___var__1988 & 3);
                                {
                                    ___var__1988 >>= 2;
                                    ___var__1989 -= 2;
                                }
                                {
                                    {
                                        if ((unsigned int)___var__1974 + ___var__1975 > ___var__1978) {
                                            return 1;
                                        }
                                    }
                                }
                                while (___var__1975--)
                                    {
                                        ___var__1987[___var__1974++] = ___var__1976;
                                    }
                            } else {
                                {
                                    if (___var__1975 == 17) {
                                        {
                                            while (___var__1989 < 3)
                                                {
                                                    int ___var__2011;
                                                    {
                                                        if (___var__1683 < ___var__1682) {
                                                            ___var__2011 = ___var__1677[___var__1683++];
                                                        } else {
                                                            int ___var__2012 = fill_inbuf(0);
                                                            ___var__2011 = (___var__1684 = ___var__1979 , ___var__2012);
                                                        }
                                                    }
                                                    ___var__1988 |= ((ulg)(uch)(___var__2011)) << ___var__1989;
                                                    ___var__1989 += 8;
                                                }
                                        }
                                        ___var__1975 = 3 + ((unsigned int)___var__1988 & 7);
                                        {
                                            ___var__1988 >>= 3;
                                            ___var__1989 -= 3;
                                        }
                                        {
                                            {
                                                if ((unsigned int)___var__1974 + ___var__1975 > ___var__1978) {
                                                    return 1;
                                                }
                                            }
                                        }
                                        while (___var__1975--)
                                            {
                                                ___var__1987[___var__1974++] = 0;
                                            }
                                        ___var__1976 = 0;
                                    } else {
                                        {
                                            while (___var__1989 < 7)
                                                {
                                                    int ___var__2013;
                                                    {
                                                        if (___var__1683 < ___var__1682) {
                                                            ___var__2013 = ___var__1677[___var__1683++];
                                                        } else {
                                                            int ___var__2014 = fill_inbuf(0);
                                                            ___var__2013 = (___var__1684 = ___var__1979 , ___var__2014);
                                                        }
                                                    }
                                                    ___var__1988 |= ((ulg)(uch)(___var__2013)) << ___var__1989;
                                                    ___var__1989 += 8;
                                                }
                                        }
                                        ___var__1975 = 11 + ((unsigned int)___var__1988 & 127);
                                        {
                                            ___var__1988 >>= 7;
                                            ___var__1989 -= 7;
                                        }
                                        {
                                            {
                                                if ((unsigned int)___var__1974 + ___var__1975 > ___var__1978) {
                                                    return 1;
                                                }
                                            }
                                        }
                                        while (___var__1975--)
                                            {
                                                ___var__1987[___var__1974++] = 0;
                                            }
                                        ___var__1976 = 0;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    int ___var__2015;
    {
        struct huft *___var__2016 = ___var__1980;
        register struct huft *___var__2017;
        register struct huft *___var__2018;
        ___var__2017 = ___var__2016;
        while (___var__2017 != (struct huft *)((void *)0))
            {
                ___var__2018 = (--___var__2017)->v.t;
                free(___var__2017);
                ___var__2017 = ___var__2018;
            }
        ___var__2015 = 0;
    }
    ___var__2015;
    ___var__1825 = ___var__1988;
    ___var__1826 = ___var__1989;
    ___var__1982 = ___var__1828;
    {
        int ___var__2019 = huft_build(___var__1987, ___var__1985, 257, ___var__1821, ___var__1822, &___var__1980, &___var__1982);
        {
            if ((___var__1974 = ___var__2019) != 0) {
                {
                    {
                        if (___var__1974 == 1) {
                            ;
                            int ___var__2020;
                            {
                                struct huft *___var__2021 = ___var__1980;
                                register struct huft *___var__2022;
                                register struct huft *___var__2023;
                                ___var__2022 = ___var__2021;
                                while (___var__2022 != (struct huft *)((void *)0))
                                    {
                                        ___var__2023 = (--___var__2022)->v.t;
                                        free(___var__2022);
                                        ___var__2022 = ___var__2023;
                                    }
                                ___var__2020 = 0;
                            }
                            ___var__2020;
                        }
                    }
                }
                return ___var__1974;
            }
        }
    }
    ___var__1983 = ___var__1829;
    {
        int ___var__2024 = huft_build(___var__1987 + ___var__1985, ___var__1986, 0, ___var__1823, ___var__1824, &___var__1981, &___var__1983);
        {
            if ((___var__1974 = ___var__2024) != 0) {
                {
                    {
                        if (___var__1974 == 1) {
                            ;
                            int ___var__2025;
                            {
                                struct huft *___var__2026 = ___var__1981;
                                register struct huft *___var__2027;
                                register struct huft *___var__2028;
                                ___var__2027 = ___var__2026;
                                while (___var__2027 != (struct huft *)((void *)0))
                                    {
                                        ___var__2028 = (--___var__2027)->v.t;
                                        free(___var__2027);
                                        ___var__2027 = ___var__2028;
                                    }
                                ___var__2025 = 0;
                            }
                            ___var__2025;
                        }
                    }
                }
                int ___var__2029;
                {
                    struct huft *___var__2030 = ___var__1980;
                    register struct huft *___var__2031;
                    register struct huft *___var__2032;
                    ___var__2031 = ___var__2030;
                    while (___var__2031 != (struct huft *)((void *)0))
                        {
                            ___var__2032 = (--___var__2031)->v.t;
                            free(___var__2031);
                            ___var__2031 = ___var__2032;
                        }
                    ___var__2029 = 0;
                }
                ___var__2029;
                return ___var__1974;
            }
        }
    }
    {
        int ___var__2033 = inflate_codes(___var__1980, ___var__1981, ___var__1982, ___var__1983);
        int ___var__2034 = ___var__2033 ? 1 : 0;
        int ___var__2035;
        {
            struct huft *___var__2036 = ___var__1980;
            register struct huft *___var__2037;
            register struct huft *___var__2038;
            ___var__2037 = ___var__2036;
            while (___var__2037 != (struct huft *)((void *)0))
                {
                    ___var__2038 = (--___var__2037)->v.t;
                    free(___var__2037);
                    ___var__2037 = ___var__2038;
                }
            ___var__2035 = 0;
        }
        ___var__2035;
        int ___var__2039;
        {
            struct huft *___var__2040 = ___var__1981;
            register struct huft *___var__2041;
            register struct huft *___var__2042;
            ___var__2041 = ___var__2040;
            while (___var__2041 != (struct huft *)((void *)0))
                {
                    ___var__2042 = (--___var__2041)->v.t;
                    free(___var__2041);
                    ___var__2041 = ___var__2042;
                }
            ___var__2039 = 0;
        }
        ___var__2039;
        return ___var__2034;
    }
}



static int inflate_block(int *___var__2044){
    unsigned int ___var__2045;
    unsigned int ___var__2046;
    register ulg ___var__2047;
    register unsigned int ___var__2048;
    ___var__2047 = ___var__1825;
    ___var__2048 = ___var__1826;
    ___var__2046 = ___var__1684;
    {
        while (___var__2048 < 1)
            {
                int ___var__2049;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__2049 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__2050 = fill_inbuf(0);
                        ___var__2049 = (___var__1684 = ___var__2046 , ___var__2050);
                    }
                }
                ___var__2047 |= ((ulg)(uch)(___var__2049)) << ___var__2048;
                ___var__2048 += 8;
            }
    }
    *___var__2044 = (int)___var__2047 & 1;
    {
        ___var__2047 >>= 1;
        ___var__2048 -= 1;
    }
    {
        while (___var__2048 < 2)
            {
                int ___var__2051;
                {
                    if (___var__1683 < ___var__1682) {
                        ___var__2051 = ___var__1677[___var__1683++];
                    } else {
                        int ___var__2052 = fill_inbuf(0);
                        ___var__2051 = (___var__1684 = ___var__2046 , ___var__2052);
                    }
                }
                ___var__2047 |= ((ulg)(uch)(___var__2051)) << ___var__2048;
                ___var__2048 += 8;
            }
    }
    ___var__2045 = (unsigned int)___var__2047 & 3;
    {
        ___var__2047 >>= 2;
        ___var__2048 -= 2;
    }
    ___var__1825 = ___var__2047;
    ___var__1826 = ___var__2048;
    {
        {
            if (___var__2045 == 2) {
                int ___var__2053;
                {
                    int ___var__2054;
                    unsigned int ___var__2055;
                    unsigned int ___var__2056;
                    unsigned int ___var__2057;
                    unsigned int ___var__2058;
                    unsigned int ___var__2059;
                    struct huft *___var__2060;
                    struct huft *___var__2061;
                    int ___var__2062;
                    int ___var__2063;
                    unsigned int ___var__2064;
                    unsigned int ___var__2065;
                    unsigned int ___var__2066;
                    unsigned int ___var__2067[316];
                    register ulg ___var__2068;
                    register unsigned int ___var__2069;
                    ___var__2068 = ___var__1825;
                    ___var__2069 = ___var__1826;
                    ___var__2059 = ___var__1684;
                    {
                        while (___var__2069 < 5)
                            {
                                int ___var__2070;
                                {
                                    if (___var__1683 < ___var__1682) {
                                        ___var__2070 = ___var__1677[___var__1683++];
                                    } else {
                                        int ___var__2071 = fill_inbuf(0);
                                        ___var__2070 = (___var__1684 = ___var__2059 , ___var__2071);
                                    }
                                }
                                ___var__2068 |= ((ulg)(uch)(___var__2070)) << ___var__2069;
                                ___var__2069 += 8;
                            }
                    }
                    ___var__2065 = 257 + ((unsigned int)___var__2068 & 31);
                    {
                        ___var__2068 >>= 5;
                        ___var__2069 -= 5;
                    }
                    {
                        while (___var__2069 < 5)
                            {
                                int ___var__2072;
                                {
                                    if (___var__1683 < ___var__1682) {
                                        ___var__2072 = ___var__1677[___var__1683++];
                                    } else {
                                        int ___var__2073 = fill_inbuf(0);
                                        ___var__2072 = (___var__1684 = ___var__2059 , ___var__2073);
                                    }
                                }
                                ___var__2068 |= ((ulg)(uch)(___var__2072)) << ___var__2069;
                                ___var__2069 += 8;
                            }
                    }
                    ___var__2066 = 1 + ((unsigned int)___var__2068 & 31);
                    {
                        ___var__2068 >>= 5;
                        ___var__2069 -= 5;
                    }
                    {
                        while (___var__2069 < 4)
                            {
                                int ___var__2074;
                                {
                                    if (___var__1683 < ___var__1682) {
                                        ___var__2074 = ___var__1677[___var__1683++];
                                    } else {
                                        int ___var__2075 = fill_inbuf(0);
                                        ___var__2074 = (___var__1684 = ___var__2059 , ___var__2075);
                                    }
                                }
                                ___var__2068 |= ((ulg)(uch)(___var__2074)) << ___var__2069;
                                ___var__2069 += 8;
                            }
                    }
                    ___var__2064 = 4 + ((unsigned int)___var__2068 & 15);
                    {
                        ___var__2068 >>= 4;
                        ___var__2069 -= 4;
                    }
                    {
                        {
                            if (___var__2065 > 286 || ___var__2066 > 30) {
                                ___var__2053 = 1;
                            }
                        }
                    }
                    {
                        {
                            for (___var__2055 = 0; ___var__2055 < ___var__2064; ___var__2055++) {
                                {
                                    while (___var__2069 < 3)
                                        {
                                            int ___var__2076;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2076 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2077 = fill_inbuf(0);
                                                    ___var__2076 = (___var__1684 = ___var__2059 , ___var__2077);
                                                }
                                            }
                                            ___var__2068 |= ((ulg)(uch)(___var__2076)) << ___var__2069;
                                            ___var__2069 += 8;
                                        }
                                }
                                ___var__2067[___var__1820[___var__2055]] = (unsigned int)___var__2068 & 7;
                                {
                                    ___var__2068 >>= 3;
                                    ___var__2069 -= 3;
                                }
                            }
                        }
                    }
                    {
                        {
                            for (; ___var__2055 < 19; ___var__2055++) {
                                ___var__2067[___var__1820[___var__2055]] = 0;
                            }
                        }
                    }
                    ___var__2062 = 7;
                    {
                        int ___var__2078 = huft_build(___var__2067, 19, 19, ((void *)0), ((void *)0), &___var__2060, &___var__2062);
                        {
                            if ((___var__2054 = ___var__2078) != 0) {
                                {
                                    {
                                        if (___var__2054 == 1) {
                                            int ___var__2079;
                                            {
                                                struct huft *___var__2080 = ___var__2060;
                                                register struct huft *___var__2081;
                                                register struct huft *___var__2082;
                                                ___var__2081 = ___var__2080;
                                                while (___var__2081 != (struct huft *)((void *)0))
                                                    {
                                                        ___var__2082 = (--___var__2081)->v.t;
                                                        free(___var__2081);
                                                        ___var__2081 = ___var__2082;
                                                    }
                                                ___var__2079 = 0;
                                            }
                                            ___var__2079;
                                        }
                                    }
                                }
                                ___var__2053 = ___var__2054;
                            }
                        }
                    }
                    {
                        {
                            if (___var__2060 == ((void *)0)) {
                                ___var__2053 = 2;
                            }
                        }
                    }
                    ___var__2058 = ___var__2065 + ___var__2066;
                    ___var__2057 = ___var__1827[___var__2062];
                    ___var__2054 = ___var__2056 = 0;
                    while ((unsigned int)___var__2054 < ___var__2058)
                        {
                            {
                                while (___var__2069 < ((unsigned int)___var__2062))
                                    {
                                        int ___var__2083;
                                        {
                                            if (___var__1683 < ___var__1682) {
                                                ___var__2083 = ___var__1677[___var__1683++];
                                            } else {
                                                int ___var__2084 = fill_inbuf(0);
                                                ___var__2083 = (___var__1684 = ___var__2059 , ___var__2084);
                                            }
                                        }
                                        ___var__2068 |= ((ulg)(uch)(___var__2083)) << ___var__2069;
                                        ___var__2069 += 8;
                                    }
                            }
                            ___var__2055 = (___var__2061 = ___var__2060 + ((unsigned int)___var__2068 & ___var__2057))->b;
                            {
                                ___var__2068 >>= (___var__2055);
                                ___var__2069 -= (___var__2055);
                            }
                            {
                                {
                                    if (___var__2061->e == 99) {
                                        int ___var__2085;
                                        {
                                            struct huft *___var__2086 = ___var__2060;
                                            register struct huft *___var__2087;
                                            register struct huft *___var__2088;
                                            ___var__2087 = ___var__2086;
                                            while (___var__2087 != (struct huft *)((void *)0))
                                                {
                                                    ___var__2088 = (--___var__2087)->v.t;
                                                    free(___var__2087);
                                                    ___var__2087 = ___var__2088;
                                                }
                                            ___var__2085 = 0;
                                        }
                                        ___var__2085;
                                        ___var__2053 = 2;
                                    }
                                }
                            }
                            ___var__2055 = ___var__2061->v.n;
                            {
                                {
                                    if (___var__2055 < 16) {
                                        ___var__2067[___var__2054++] = ___var__2056 = ___var__2055;
                                    } else {
                                        {
                                            if (___var__2055 == 16) {
                                                {
                                                    while (___var__2069 < 2)
                                                        {
                                                            int ___var__2089;
                                                            {
                                                                if (___var__1683 < ___var__1682) {
                                                                    ___var__2089 = ___var__1677[___var__1683++];
                                                                } else {
                                                                    int ___var__2090 = fill_inbuf(0);
                                                                    ___var__2089 = (___var__1684 = ___var__2059 , ___var__2090);
                                                                }
                                                            }
                                                            ___var__2068 |= ((ulg)(uch)(___var__2089)) << ___var__2069;
                                                            ___var__2069 += 8;
                                                        }
                                                }
                                                ___var__2055 = 3 + ((unsigned int)___var__2068 & 3);
                                                {
                                                    ___var__2068 >>= 2;
                                                    ___var__2069 -= 2;
                                                }
                                                {
                                                    {
                                                        if ((unsigned int)___var__2054 + ___var__2055 > ___var__2058) {
                                                            ___var__2053 = 1;
                                                        }
                                                    }
                                                }
                                                while (___var__2055--)
                                                    {
                                                        ___var__2067[___var__2054++] = ___var__2056;
                                                    }
                                            } else {
                                                {
                                                    if (___var__2055 == 17) {
                                                        {
                                                            while (___var__2069 < 3)
                                                                {
                                                                    int ___var__2091;
                                                                    {
                                                                        if (___var__1683 < ___var__1682) {
                                                                            ___var__2091 = ___var__1677[___var__1683++];
                                                                        } else {
                                                                            int ___var__2092 = fill_inbuf(0);
                                                                            ___var__2091 = (___var__1684 = ___var__2059 , ___var__2092);
                                                                        }
                                                                    }
                                                                    ___var__2068 |= ((ulg)(uch)(___var__2091)) << ___var__2069;
                                                                    ___var__2069 += 8;
                                                                }
                                                        }
                                                        ___var__2055 = 3 + ((unsigned int)___var__2068 & 7);
                                                        {
                                                            ___var__2068 >>= 3;
                                                            ___var__2069 -= 3;
                                                        }
                                                        {
                                                            {
                                                                if ((unsigned int)___var__2054 + ___var__2055 > ___var__2058) {
                                                                    ___var__2053 = 1;
                                                                }
                                                            }
                                                        }
                                                        while (___var__2055--)
                                                            {
                                                                ___var__2067[___var__2054++] = 0;
                                                            }
                                                        ___var__2056 = 0;
                                                    } else {
                                                        {
                                                            while (___var__2069 < 7)
                                                                {
                                                                    int ___var__2093;
                                                                    {
                                                                        if (___var__1683 < ___var__1682) {
                                                                            ___var__2093 = ___var__1677[___var__1683++];
                                                                        } else {
                                                                            int ___var__2094 = fill_inbuf(0);
                                                                            ___var__2093 = (___var__1684 = ___var__2059 , ___var__2094);
                                                                        }
                                                                    }
                                                                    ___var__2068 |= ((ulg)(uch)(___var__2093)) << ___var__2069;
                                                                    ___var__2069 += 8;
                                                                }
                                                        }
                                                        ___var__2055 = 11 + ((unsigned int)___var__2068 & 127);
                                                        {
                                                            ___var__2068 >>= 7;
                                                            ___var__2069 -= 7;
                                                        }
                                                        {
                                                            {
                                                                if ((unsigned int)___var__2054 + ___var__2055 > ___var__2058) {
                                                                    ___var__2053 = 1;
                                                                }
                                                            }
                                                        }
                                                        while (___var__2055--)
                                                            {
                                                                ___var__2067[___var__2054++] = 0;
                                                            }
                                                        ___var__2056 = 0;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    int ___var__2095;
                    {
                        struct huft *___var__2096 = ___var__2060;
                        register struct huft *___var__2097;
                        register struct huft *___var__2098;
                        ___var__2097 = ___var__2096;
                        while (___var__2097 != (struct huft *)((void *)0))
                            {
                                ___var__2098 = (--___var__2097)->v.t;
                                free(___var__2097);
                                ___var__2097 = ___var__2098;
                            }
                        ___var__2095 = 0;
                    }
                    ___var__2095;
                    ___var__1825 = ___var__2068;
                    ___var__1826 = ___var__2069;
                    ___var__2062 = ___var__1828;
                    {
                        int ___var__2099 = huft_build(___var__2067, ___var__2065, 257, ___var__1821, ___var__1822, &___var__2060, &___var__2062);
                        {
                            if ((___var__2054 = ___var__2099) != 0) {
                                {
                                    {
                                        if (___var__2054 == 1) {
                                            ;
                                            int ___var__2100;
                                            {
                                                struct huft *___var__2101 = ___var__2060;
                                                register struct huft *___var__2102;
                                                register struct huft *___var__2103;
                                                ___var__2102 = ___var__2101;
                                                while (___var__2102 != (struct huft *)((void *)0))
                                                    {
                                                        ___var__2103 = (--___var__2102)->v.t;
                                                        free(___var__2102);
                                                        ___var__2102 = ___var__2103;
                                                    }
                                                ___var__2100 = 0;
                                            }
                                            ___var__2100;
                                        }
                                    }
                                }
                                ___var__2053 = ___var__2054;
                            }
                        }
                    }
                    ___var__2063 = ___var__1829;
                    {
                        int ___var__2104 = huft_build(___var__2067 + ___var__2065, ___var__2066, 0, ___var__1823, ___var__1824, &___var__2061, &___var__2063);
                        {
                            if ((___var__2054 = ___var__2104) != 0) {
                                {
                                    {
                                        if (___var__2054 == 1) {
                                            ;
                                            int ___var__2105;
                                            {
                                                struct huft *___var__2106 = ___var__2061;
                                                register struct huft *___var__2107;
                                                register struct huft *___var__2108;
                                                ___var__2107 = ___var__2106;
                                                while (___var__2107 != (struct huft *)((void *)0))
                                                    {
                                                        ___var__2108 = (--___var__2107)->v.t;
                                                        free(___var__2107);
                                                        ___var__2107 = ___var__2108;
                                                    }
                                                ___var__2105 = 0;
                                            }
                                            ___var__2105;
                                        }
                                    }
                                }
                                int ___var__2109;
                                {
                                    struct huft *___var__2110 = ___var__2060;
                                    register struct huft *___var__2111;
                                    register struct huft *___var__2112;
                                    ___var__2111 = ___var__2110;
                                    while (___var__2111 != (struct huft *)((void *)0))
                                        {
                                            ___var__2112 = (--___var__2111)->v.t;
                                            free(___var__2111);
                                            ___var__2111 = ___var__2112;
                                        }
                                    ___var__2109 = 0;
                                }
                                ___var__2109;
                                ___var__2053 = ___var__2054;
                            }
                        }
                    }
                    {
                        int ___var__2113 = inflate_codes(___var__2060, ___var__2061, ___var__2062, ___var__2063);
                        int ___var__2114 = ___var__2113 ? 1 : 0;
                        int ___var__2115;
                        {
                            struct huft *___var__2116 = ___var__2060;
                            register struct huft *___var__2117;
                            register struct huft *___var__2118;
                            ___var__2117 = ___var__2116;
                            while (___var__2117 != (struct huft *)((void *)0))
                                {
                                    ___var__2118 = (--___var__2117)->v.t;
                                    free(___var__2117);
                                    ___var__2117 = ___var__2118;
                                }
                            ___var__2115 = 0;
                        }
                        ___var__2115;
                        int ___var__2119;
                        {
                            struct huft *___var__2120 = ___var__2061;
                            register struct huft *___var__2121;
                            register struct huft *___var__2122;
                            ___var__2121 = ___var__2120;
                            while (___var__2121 != (struct huft *)((void *)0))
                                {
                                    ___var__2122 = (--___var__2121)->v.t;
                                    free(___var__2121);
                                    ___var__2121 = ___var__2122;
                                }
                            ___var__2119 = 0;
                        }
                        ___var__2119;
                        ___var__2053 = ___var__2114;
                    }
                }
                return ___var__2053;
            }
        }
    }
    {
        {
            if (___var__2045 == 0) {
                int ___var__2123;
                {
                    unsigned int ___var__2124;
                    unsigned int ___var__2125;
                    register ulg ___var__2126;
                    register unsigned int ___var__2127;
                    ___var__2126 = ___var__1825;
                    ___var__2127 = ___var__1826;
                    ___var__2125 = ___var__1684;
                    ___var__2124 = ___var__2127 & 7;
                    {
                        ___var__2126 >>= (___var__2124);
                        ___var__2127 -= (___var__2124);
                    }
                    ;
                    {
                        while (___var__2127 < 16)
                            {
                                int ___var__2128;
                                {
                                    if (___var__1683 < ___var__1682) {
                                        ___var__2128 = ___var__1677[___var__1683++];
                                    } else {
                                        int ___var__2129 = fill_inbuf(0);
                                        ___var__2128 = (___var__1684 = ___var__2125 , ___var__2129);
                                    }
                                }
                                ___var__2126 |= ((ulg)(uch)(___var__2128)) << ___var__2127;
                                ___var__2127 += 8;
                            }
                    }
                    ___var__2124 = ((unsigned int)___var__2126 & 65535);
                    {
                        ___var__2126 >>= 16;
                        ___var__2127 -= 16;
                    }
                    {
                        while (___var__2127 < 16)
                            {
                                int ___var__2130;
                                {
                                    if (___var__1683 < ___var__1682) {
                                        ___var__2130 = ___var__1677[___var__1683++];
                                    } else {
                                        int ___var__2131 = fill_inbuf(0);
                                        ___var__2130 = (___var__1684 = ___var__2125 , ___var__2131);
                                    }
                                }
                                ___var__2126 |= ((ulg)(uch)(___var__2130)) << ___var__2127;
                                ___var__2127 += 8;
                            }
                    }
                    {
                        {
                            if (___var__2124 != (unsigned int)((~___var__2126) & 65535)) {
                                ___var__2123 = 1;
                            }
                        }
                    }
                    {
                        ___var__2126 >>= 16;
                        ___var__2127 -= 16;
                    }
                    while (___var__2124--)
                        {
                            {
                                while (___var__2127 < 8)
                                    {
                                        int ___var__2132;
                                        {
                                            if (___var__1683 < ___var__1682) {
                                                ___var__2132 = ___var__1677[___var__1683++];
                                            } else {
                                                int ___var__2133 = fill_inbuf(0);
                                                ___var__2132 = (___var__1684 = ___var__2125 , ___var__2133);
                                            }
                                        }
                                        ___var__2126 |= ((ulg)(uch)(___var__2132)) << ___var__2127;
                                        ___var__2127 += 8;
                                    }
                            }
                            ___var__1680[___var__2125++] = (uch)___var__2126;
                            {
                                {
                                    if (___var__2125 == 32768) {
                                        flush_window();
                                        (___var__1684 = (___var__2125) , (void)0);
                                        ___var__2125 = 0;
                                    }
                                }
                            }
                            {
                                ___var__2126 >>= 8;
                                ___var__2127 -= 8;
                            }
                        }
                    ___var__1684 = ___var__2125;
                    ___var__1825 = ___var__2126;
                    ___var__1826 = ___var__2127;
                    ___var__2123 = 0;
                }
                return ___var__2123;
            }
        }
    }
    {
        {
            if (___var__2045 == 1) {
                int ___var__2134;
                {
                    int ___var__2135;
                    struct huft *___var__2136;
                    struct huft *___var__2137;
                    int ___var__2138;
                    int ___var__2139;
                    unsigned int ___var__2140[288];
                    {
                        {
                            for (___var__2135 = 0; ___var__2135 < 144; ___var__2135++) {
                                ___var__2140[___var__2135] = 8;
                            }
                        }
                    }
                    {
                        {
                            for (; ___var__2135 < 256; ___var__2135++) {
                                ___var__2140[___var__2135] = 9;
                            }
                        }
                    }
                    {
                        {
                            for (; ___var__2135 < 280; ___var__2135++) {
                                ___var__2140[___var__2135] = 7;
                            }
                        }
                    }
                    {
                        {
                            for (; ___var__2135 < 288; ___var__2135++) {
                                ___var__2140[___var__2135] = 8;
                            }
                        }
                    }
                    ___var__2138 = 7;
                    {
                        int ___var__2141 = huft_build(___var__2140, 288, 257, ___var__1821, ___var__1822, &___var__2136, &___var__2138);
                        {
                            if ((___var__2135 = ___var__2141) != 0) {
                                ___var__2134 = ___var__2135;
                            }
                        }
                    }
                    {
                        {
                            for (___var__2135 = 0; ___var__2135 < 30; ___var__2135++) {
                                ___var__2140[___var__2135] = 5;
                            }
                        }
                    }
                    ___var__2139 = 5;
                    {
                        int ___var__2142 = huft_build(___var__2140, 30, 0, ___var__1823, ___var__1824, &___var__2137, &___var__2139);
                        {
                            if ((___var__2135 = ___var__2142) > 1) {
                                int ___var__2143;
                                {
                                    struct huft *___var__2144 = ___var__2136;
                                    register struct huft *___var__2145;
                                    register struct huft *___var__2146;
                                    ___var__2145 = ___var__2144;
                                    while (___var__2145 != (struct huft *)((void *)0))
                                        {
                                            ___var__2146 = (--___var__2145)->v.t;
                                            free(___var__2145);
                                            ___var__2145 = ___var__2146;
                                        }
                                    ___var__2143 = 0;
                                }
                                ___var__2143;
                                ___var__2134 = ___var__2135;
                            }
                        }
                    }
                    {
                        int ___var__2147 = inflate_codes(___var__2136, ___var__2137, ___var__2138, ___var__2139);
                        {
                            if (___var__2147) {
                                ___var__2134 = 1;
                            }
                        }
                    }
                    int ___var__2148;
                    {
                        struct huft *___var__2149 = ___var__2136;
                        register struct huft *___var__2150;
                        register struct huft *___var__2151;
                        ___var__2150 = ___var__2149;
                        while (___var__2150 != (struct huft *)((void *)0))
                            {
                                ___var__2151 = (--___var__2150)->v.t;
                                free(___var__2150);
                                ___var__2150 = ___var__2151;
                            }
                        ___var__2148 = 0;
                    }
                    ___var__2148;
                    int ___var__2152;
                    {
                        struct huft *___var__2153 = ___var__2137;
                        register struct huft *___var__2154;
                        register struct huft *___var__2155;
                        ___var__2154 = ___var__2153;
                        while (___var__2154 != (struct huft *)((void *)0))
                            {
                                ___var__2155 = (--___var__2154)->v.t;
                                free(___var__2154);
                                ___var__2154 = ___var__2155;
                            }
                        ___var__2152 = 0;
                    }
                    ___var__2152;
                    ___var__2134 = 0;
                }
                return ___var__2134;
            }
        }
    }
    return 2;
}



int inflate(void){
    int ___var__2156;
    int ___var__2157;
    unsigned int ___var__2158;
    ___var__1684 = 0;
    ___var__1826 = 0;
    ___var__1825 = 0;
    ___var__2158 = 0;
    do {
        ___var__1830 = 0;
        {
            int ___var__2159;
            {
                int *___var__2160 = &___var__2156;
                unsigned int ___var__2161;
                unsigned int ___var__2162;
                register ulg ___var__2163;
                register unsigned int ___var__2164;
                ___var__2163 = ___var__1825;
                ___var__2164 = ___var__1826;
                ___var__2162 = ___var__1684;
                {
                    while (___var__2164 < 1)
                        {
                            int ___var__2165;
                            {
                                if (___var__1683 < ___var__1682) {
                                    ___var__2165 = ___var__1677[___var__1683++];
                                } else {
                                    int ___var__2166 = fill_inbuf(0);
                                    ___var__2165 = (___var__1684 = ___var__2162 , ___var__2166);
                                }
                            }
                            ___var__2163 |= ((ulg)(uch)(___var__2165)) << ___var__2164;
                            ___var__2164 += 8;
                        }
                }
                *___var__2160 = (int)___var__2163 & 1;
                {
                    ___var__2163 >>= 1;
                    ___var__2164 -= 1;
                }
                {
                    while (___var__2164 < 2)
                        {
                            int ___var__2167;
                            {
                                if (___var__1683 < ___var__1682) {
                                    ___var__2167 = ___var__1677[___var__1683++];
                                } else {
                                    int ___var__2168 = fill_inbuf(0);
                                    ___var__2167 = (___var__1684 = ___var__2162 , ___var__2168);
                                }
                            }
                            ___var__2163 |= ((ulg)(uch)(___var__2167)) << ___var__2164;
                            ___var__2164 += 8;
                        }
                }
                ___var__2161 = (unsigned int)___var__2163 & 3;
                {
                    ___var__2163 >>= 2;
                    ___var__2164 -= 2;
                }
                ___var__1825 = ___var__2163;
                ___var__1826 = ___var__2164;
                {
                    {
                        if (___var__2161 == 2) {
                            int ___var__2169;
                            {
                                int ___var__2170;
                                unsigned int ___var__2171;
                                unsigned int ___var__2172;
                                unsigned int ___var__2173;
                                unsigned int ___var__2174;
                                unsigned int ___var__2175;
                                struct huft *___var__2176;
                                struct huft *___var__2177;
                                int ___var__2178;
                                int ___var__2179;
                                unsigned int ___var__2180;
                                unsigned int ___var__2181;
                                unsigned int ___var__2182;
                                unsigned int ___var__2183[316];
                                register ulg ___var__2184;
                                register unsigned int ___var__2185;
                                ___var__2184 = ___var__1825;
                                ___var__2185 = ___var__1826;
                                ___var__2175 = ___var__1684;
                                {
                                    while (___var__2185 < 5)
                                        {
                                            int ___var__2186;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2186 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2187 = fill_inbuf(0);
                                                    ___var__2186 = (___var__1684 = ___var__2175 , ___var__2187);
                                                }
                                            }
                                            ___var__2184 |= ((ulg)(uch)(___var__2186)) << ___var__2185;
                                            ___var__2185 += 8;
                                        }
                                }
                                ___var__2181 = 257 + ((unsigned int)___var__2184 & 31);
                                {
                                    ___var__2184 >>= 5;
                                    ___var__2185 -= 5;
                                }
                                {
                                    while (___var__2185 < 5)
                                        {
                                            int ___var__2188;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2188 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2189 = fill_inbuf(0);
                                                    ___var__2188 = (___var__1684 = ___var__2175 , ___var__2189);
                                                }
                                            }
                                            ___var__2184 |= ((ulg)(uch)(___var__2188)) << ___var__2185;
                                            ___var__2185 += 8;
                                        }
                                }
                                ___var__2182 = 1 + ((unsigned int)___var__2184 & 31);
                                {
                                    ___var__2184 >>= 5;
                                    ___var__2185 -= 5;
                                }
                                {
                                    while (___var__2185 < 4)
                                        {
                                            int ___var__2190;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2190 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2191 = fill_inbuf(0);
                                                    ___var__2190 = (___var__1684 = ___var__2175 , ___var__2191);
                                                }
                                            }
                                            ___var__2184 |= ((ulg)(uch)(___var__2190)) << ___var__2185;
                                            ___var__2185 += 8;
                                        }
                                }
                                ___var__2180 = 4 + ((unsigned int)___var__2184 & 15);
                                {
                                    ___var__2184 >>= 4;
                                    ___var__2185 -= 4;
                                }
                                {
                                    {
                                        if (___var__2181 > 286 || ___var__2182 > 30) {
                                            ___var__2169 = 1;
                                        }
                                    }
                                }
                                {
                                    {
                                        for (___var__2171 = 0; ___var__2171 < ___var__2180; ___var__2171++) {
                                            {
                                                while (___var__2185 < 3)
                                                    {
                                                        int ___var__2192;
                                                        {
                                                            if (___var__1683 < ___var__1682) {
                                                                ___var__2192 = ___var__1677[___var__1683++];
                                                            } else {
                                                                int ___var__2193 = fill_inbuf(0);
                                                                ___var__2192 = (___var__1684 = ___var__2175 , ___var__2193);
                                                            }
                                                        }
                                                        ___var__2184 |= ((ulg)(uch)(___var__2192)) << ___var__2185;
                                                        ___var__2185 += 8;
                                                    }
                                            }
                                            ___var__2183[___var__1820[___var__2171]] = (unsigned int)___var__2184 & 7;
                                            {
                                                ___var__2184 >>= 3;
                                                ___var__2185 -= 3;
                                            }
                                        }
                                    }
                                }
                                {
                                    {
                                        for (; ___var__2171 < 19; ___var__2171++) {
                                            ___var__2183[___var__1820[___var__2171]] = 0;
                                        }
                                    }
                                }
                                ___var__2178 = 7;
                                {
                                    int ___var__2194 = huft_build(___var__2183, 19, 19, ((void *)0), ((void *)0), &___var__2176, &___var__2178);
                                    {
                                        if ((___var__2170 = ___var__2194) != 0) {
                                            {
                                                {
                                                    if (___var__2170 == 1) {
                                                        int ___var__2195;
                                                        {
                                                            struct huft *___var__2196 = ___var__2176;
                                                            register struct huft *___var__2197;
                                                            register struct huft *___var__2198;
                                                            ___var__2197 = ___var__2196;
                                                            while (___var__2197 != (struct huft *)((void *)0))
                                                                {
                                                                    ___var__2198 = (--___var__2197)->v.t;
                                                                    free(___var__2197);
                                                                    ___var__2197 = ___var__2198;
                                                                }
                                                            ___var__2195 = 0;
                                                        }
                                                        ___var__2195;
                                                    }
                                                }
                                            }
                                            ___var__2169 = ___var__2170;
                                        }
                                    }
                                }
                                {
                                    {
                                        if (___var__2176 == ((void *)0)) {
                                            ___var__2169 = 2;
                                        }
                                    }
                                }
                                ___var__2174 = ___var__2181 + ___var__2182;
                                ___var__2173 = ___var__1827[___var__2178];
                                ___var__2170 = ___var__2172 = 0;
                                while ((unsigned int)___var__2170 < ___var__2174)
                                    {
                                        {
                                            while (___var__2185 < ((unsigned int)___var__2178))
                                                {
                                                    int ___var__2199;
                                                    {
                                                        if (___var__1683 < ___var__1682) {
                                                            ___var__2199 = ___var__1677[___var__1683++];
                                                        } else {
                                                            int ___var__2200 = fill_inbuf(0);
                                                            ___var__2199 = (___var__1684 = ___var__2175 , ___var__2200);
                                                        }
                                                    }
                                                    ___var__2184 |= ((ulg)(uch)(___var__2199)) << ___var__2185;
                                                    ___var__2185 += 8;
                                                }
                                        }
                                        ___var__2171 = (___var__2177 = ___var__2176 + ((unsigned int)___var__2184 & ___var__2173))->b;
                                        {
                                            ___var__2184 >>= (___var__2171);
                                            ___var__2185 -= (___var__2171);
                                        }
                                        {
                                            {
                                                if (___var__2177->e == 99) {
                                                    int ___var__2201;
                                                    {
                                                        struct huft *___var__2202 = ___var__2176;
                                                        register struct huft *___var__2203;
                                                        register struct huft *___var__2204;
                                                        ___var__2203 = ___var__2202;
                                                        while (___var__2203 != (struct huft *)((void *)0))
                                                            {
                                                                ___var__2204 = (--___var__2203)->v.t;
                                                                free(___var__2203);
                                                                ___var__2203 = ___var__2204;
                                                            }
                                                        ___var__2201 = 0;
                                                    }
                                                    ___var__2201;
                                                    ___var__2169 = 2;
                                                }
                                            }
                                        }
                                        ___var__2171 = ___var__2177->v.n;
                                        {
                                            {
                                                if (___var__2171 < 16) {
                                                    ___var__2183[___var__2170++] = ___var__2172 = ___var__2171;
                                                } else {
                                                    {
                                                        if (___var__2171 == 16) {
                                                            {
                                                                while (___var__2185 < 2)
                                                                    {
                                                                        int ___var__2205;
                                                                        {
                                                                            if (___var__1683 < ___var__1682) {
                                                                                ___var__2205 = ___var__1677[___var__1683++];
                                                                            } else {
                                                                                int ___var__2206 = fill_inbuf(0);
                                                                                ___var__2205 = (___var__1684 = ___var__2175 , ___var__2206);
                                                                            }
                                                                        }
                                                                        ___var__2184 |= ((ulg)(uch)(___var__2205)) << ___var__2185;
                                                                        ___var__2185 += 8;
                                                                    }
                                                            }
                                                            ___var__2171 = 3 + ((unsigned int)___var__2184 & 3);
                                                            {
                                                                ___var__2184 >>= 2;
                                                                ___var__2185 -= 2;
                                                            }
                                                            {
                                                                {
                                                                    if ((unsigned int)___var__2170 + ___var__2171 > ___var__2174) {
                                                                        ___var__2169 = 1;
                                                                    }
                                                                }
                                                            }
                                                            while (___var__2171--)
                                                                {
                                                                    ___var__2183[___var__2170++] = ___var__2172;
                                                                }
                                                        } else {
                                                            {
                                                                if (___var__2171 == 17) {
                                                                    {
                                                                        while (___var__2185 < 3)
                                                                            {
                                                                                int ___var__2207;
                                                                                {
                                                                                    if (___var__1683 < ___var__1682) {
                                                                                        ___var__2207 = ___var__1677[___var__1683++];
                                                                                    } else {
                                                                                        int ___var__2208 = fill_inbuf(0);
                                                                                        ___var__2207 = (___var__1684 = ___var__2175 , ___var__2208);
                                                                                    }
                                                                                }
                                                                                ___var__2184 |= ((ulg)(uch)(___var__2207)) << ___var__2185;
                                                                                ___var__2185 += 8;
                                                                            }
                                                                    }
                                                                    ___var__2171 = 3 + ((unsigned int)___var__2184 & 7);
                                                                    {
                                                                        ___var__2184 >>= 3;
                                                                        ___var__2185 -= 3;
                                                                    }
                                                                    {
                                                                        {
                                                                            if ((unsigned int)___var__2170 + ___var__2171 > ___var__2174) {
                                                                                ___var__2169 = 1;
                                                                            }
                                                                        }
                                                                    }
                                                                    while (___var__2171--)
                                                                        {
                                                                            ___var__2183[___var__2170++] = 0;
                                                                        }
                                                                    ___var__2172 = 0;
                                                                } else {
                                                                    {
                                                                        while (___var__2185 < 7)
                                                                            {
                                                                                int ___var__2209;
                                                                                {
                                                                                    if (___var__1683 < ___var__1682) {
                                                                                        ___var__2209 = ___var__1677[___var__1683++];
                                                                                    } else {
                                                                                        int ___var__2210 = fill_inbuf(0);
                                                                                        ___var__2209 = (___var__1684 = ___var__2175 , ___var__2210);
                                                                                    }
                                                                                }
                                                                                ___var__2184 |= ((ulg)(uch)(___var__2209)) << ___var__2185;
                                                                                ___var__2185 += 8;
                                                                            }
                                                                    }
                                                                    ___var__2171 = 11 + ((unsigned int)___var__2184 & 127);
                                                                    {
                                                                        ___var__2184 >>= 7;
                                                                        ___var__2185 -= 7;
                                                                    }
                                                                    {
                                                                        {
                                                                            if ((unsigned int)___var__2170 + ___var__2171 > ___var__2174) {
                                                                                ___var__2169 = 1;
                                                                            }
                                                                        }
                                                                    }
                                                                    while (___var__2171--)
                                                                        {
                                                                            ___var__2183[___var__2170++] = 0;
                                                                        }
                                                                    ___var__2172 = 0;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                int ___var__2211;
                                {
                                    struct huft *___var__2212 = ___var__2176;
                                    register struct huft *___var__2213;
                                    register struct huft *___var__2214;
                                    ___var__2213 = ___var__2212;
                                    while (___var__2213 != (struct huft *)((void *)0))
                                        {
                                            ___var__2214 = (--___var__2213)->v.t;
                                            free(___var__2213);
                                            ___var__2213 = ___var__2214;
                                        }
                                    ___var__2211 = 0;
                                }
                                ___var__2211;
                                ___var__1825 = ___var__2184;
                                ___var__1826 = ___var__2185;
                                ___var__2178 = ___var__1828;
                                {
                                    int ___var__2215 = huft_build(___var__2183, ___var__2181, 257, ___var__1821, ___var__1822, &___var__2176, &___var__2178);
                                    {
                                        if ((___var__2170 = ___var__2215) != 0) {
                                            {
                                                {
                                                    if (___var__2170 == 1) {
                                                        ;
                                                        int ___var__2216;
                                                        {
                                                            struct huft *___var__2217 = ___var__2176;
                                                            register struct huft *___var__2218;
                                                            register struct huft *___var__2219;
                                                            ___var__2218 = ___var__2217;
                                                            while (___var__2218 != (struct huft *)((void *)0))
                                                                {
                                                                    ___var__2219 = (--___var__2218)->v.t;
                                                                    free(___var__2218);
                                                                    ___var__2218 = ___var__2219;
                                                                }
                                                            ___var__2216 = 0;
                                                        }
                                                        ___var__2216;
                                                    }
                                                }
                                            }
                                            ___var__2169 = ___var__2170;
                                        }
                                    }
                                }
                                ___var__2179 = ___var__1829;
                                {
                                    int ___var__2220 = huft_build(___var__2183 + ___var__2181, ___var__2182, 0, ___var__1823, ___var__1824, &___var__2177, &___var__2179);
                                    {
                                        if ((___var__2170 = ___var__2220) != 0) {
                                            {
                                                {
                                                    if (___var__2170 == 1) {
                                                        ;
                                                        int ___var__2221;
                                                        {
                                                            struct huft *___var__2222 = ___var__2177;
                                                            register struct huft *___var__2223;
                                                            register struct huft *___var__2224;
                                                            ___var__2223 = ___var__2222;
                                                            while (___var__2223 != (struct huft *)((void *)0))
                                                                {
                                                                    ___var__2224 = (--___var__2223)->v.t;
                                                                    free(___var__2223);
                                                                    ___var__2223 = ___var__2224;
                                                                }
                                                            ___var__2221 = 0;
                                                        }
                                                        ___var__2221;
                                                    }
                                                }
                                            }
                                            int ___var__2225;
                                            {
                                                struct huft *___var__2226 = ___var__2176;
                                                register struct huft *___var__2227;
                                                register struct huft *___var__2228;
                                                ___var__2227 = ___var__2226;
                                                while (___var__2227 != (struct huft *)((void *)0))
                                                    {
                                                        ___var__2228 = (--___var__2227)->v.t;
                                                        free(___var__2227);
                                                        ___var__2227 = ___var__2228;
                                                    }
                                                ___var__2225 = 0;
                                            }
                                            ___var__2225;
                                            ___var__2169 = ___var__2170;
                                        }
                                    }
                                }
                                {
                                    int ___var__2229 = inflate_codes(___var__2176, ___var__2177, ___var__2178, ___var__2179);
                                    int ___var__2230 = ___var__2229 ? 1 : 0;
                                    int ___var__2231;
                                    {
                                        struct huft *___var__2232 = ___var__2176;
                                        register struct huft *___var__2233;
                                        register struct huft *___var__2234;
                                        ___var__2233 = ___var__2232;
                                        while (___var__2233 != (struct huft *)((void *)0))
                                            {
                                                ___var__2234 = (--___var__2233)->v.t;
                                                free(___var__2233);
                                                ___var__2233 = ___var__2234;
                                            }
                                        ___var__2231 = 0;
                                    }
                                    ___var__2231;
                                    int ___var__2235;
                                    {
                                        struct huft *___var__2236 = ___var__2177;
                                        register struct huft *___var__2237;
                                        register struct huft *___var__2238;
                                        ___var__2237 = ___var__2236;
                                        while (___var__2237 != (struct huft *)((void *)0))
                                            {
                                                ___var__2238 = (--___var__2237)->v.t;
                                                free(___var__2237);
                                                ___var__2237 = ___var__2238;
                                            }
                                        ___var__2235 = 0;
                                    }
                                    ___var__2235;
                                    ___var__2169 = ___var__2230;
                                }
                            }
                            ___var__2159 = ___var__2169;
                        }
                    }
                }
                {
                    {
                        if (___var__2161 == 0) {
                            int ___var__2239;
                            {
                                unsigned int ___var__2240;
                                unsigned int ___var__2241;
                                register ulg ___var__2242;
                                register unsigned int ___var__2243;
                                ___var__2242 = ___var__1825;
                                ___var__2243 = ___var__1826;
                                ___var__2241 = ___var__1684;
                                ___var__2240 = ___var__2243 & 7;
                                {
                                    ___var__2242 >>= (___var__2240);
                                    ___var__2243 -= (___var__2240);
                                }
                                ;
                                {
                                    while (___var__2243 < 16)
                                        {
                                            int ___var__2244;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2244 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2245 = fill_inbuf(0);
                                                    ___var__2244 = (___var__1684 = ___var__2241 , ___var__2245);
                                                }
                                            }
                                            ___var__2242 |= ((ulg)(uch)(___var__2244)) << ___var__2243;
                                            ___var__2243 += 8;
                                        }
                                }
                                ___var__2240 = ((unsigned int)___var__2242 & 65535);
                                {
                                    ___var__2242 >>= 16;
                                    ___var__2243 -= 16;
                                }
                                {
                                    while (___var__2243 < 16)
                                        {
                                            int ___var__2246;
                                            {
                                                if (___var__1683 < ___var__1682) {
                                                    ___var__2246 = ___var__1677[___var__1683++];
                                                } else {
                                                    int ___var__2247 = fill_inbuf(0);
                                                    ___var__2246 = (___var__1684 = ___var__2241 , ___var__2247);
                                                }
                                            }
                                            ___var__2242 |= ((ulg)(uch)(___var__2246)) << ___var__2243;
                                            ___var__2243 += 8;
                                        }
                                }
                                {
                                    {
                                        if (___var__2240 != (unsigned int)((~___var__2242) & 65535)) {
                                            ___var__2239 = 1;
                                        }
                                    }
                                }
                                {
                                    ___var__2242 >>= 16;
                                    ___var__2243 -= 16;
                                }
                                while (___var__2240--)
                                    {
                                        {
                                            while (___var__2243 < 8)
                                                {
                                                    int ___var__2248;
                                                    {
                                                        if (___var__1683 < ___var__1682) {
                                                            ___var__2248 = ___var__1677[___var__1683++];
                                                        } else {
                                                            int ___var__2249 = fill_inbuf(0);
                                                            ___var__2248 = (___var__1684 = ___var__2241 , ___var__2249);
                                                        }
                                                    }
                                                    ___var__2242 |= ((ulg)(uch)(___var__2248)) << ___var__2243;
                                                    ___var__2243 += 8;
                                                }
                                        }
                                        ___var__1680[___var__2241++] = (uch)___var__2242;
                                        {
                                            {
                                                if (___var__2241 == 32768) {
                                                    flush_window();
                                                    (___var__1684 = (___var__2241) , (void)0);
                                                    ___var__2241 = 0;
                                                }
                                            }
                                        }
                                        {
                                            ___var__2242 >>= 8;
                                            ___var__2243 -= 8;
                                        }
                                    }
                                ___var__1684 = ___var__2241;
                                ___var__1825 = ___var__2242;
                                ___var__1826 = ___var__2243;
                                ___var__2239 = 0;
                            }
                            ___var__2159 = ___var__2239;
                        }
                    }
                }
                {
                    {
                        if (___var__2161 == 1) {
                            int ___var__2250;
                            {
                                int ___var__2251;
                                struct huft *___var__2252;
                                struct huft *___var__2253;
                                int ___var__2254;
                                int ___var__2255;
                                unsigned int ___var__2256[288];
                                {
                                    {
                                        for (___var__2251 = 0; ___var__2251 < 144; ___var__2251++) {
                                            ___var__2256[___var__2251] = 8;
                                        }
                                    }
                                }
                                {
                                    {
                                        for (; ___var__2251 < 256; ___var__2251++) {
                                            ___var__2256[___var__2251] = 9;
                                        }
                                    }
                                }
                                {
                                    {
                                        for (; ___var__2251 < 280; ___var__2251++) {
                                            ___var__2256[___var__2251] = 7;
                                        }
                                    }
                                }
                                {
                                    {
                                        for (; ___var__2251 < 288; ___var__2251++) {
                                            ___var__2256[___var__2251] = 8;
                                        }
                                    }
                                }
                                ___var__2254 = 7;
                                {
                                    int ___var__2257 = huft_build(___var__2256, 288, 257, ___var__1821, ___var__1822, &___var__2252, &___var__2254);
                                    {
                                        if ((___var__2251 = ___var__2257) != 0) {
                                            ___var__2250 = ___var__2251;
                                        }
                                    }
                                }
                                {
                                    {
                                        for (___var__2251 = 0; ___var__2251 < 30; ___var__2251++) {
                                            ___var__2256[___var__2251] = 5;
                                        }
                                    }
                                }
                                ___var__2255 = 5;
                                {
                                    int ___var__2258 = huft_build(___var__2256, 30, 0, ___var__1823, ___var__1824, &___var__2253, &___var__2255);
                                    {
                                        if ((___var__2251 = ___var__2258) > 1) {
                                            int ___var__2259;
                                            {
                                                struct huft *___var__2260 = ___var__2252;
                                                register struct huft *___var__2261;
                                                register struct huft *___var__2262;
                                                ___var__2261 = ___var__2260;
                                                while (___var__2261 != (struct huft *)((void *)0))
                                                    {
                                                        ___var__2262 = (--___var__2261)->v.t;
                                                        free(___var__2261);
                                                        ___var__2261 = ___var__2262;
                                                    }
                                                ___var__2259 = 0;
                                            }
                                            ___var__2259;
                                            ___var__2250 = ___var__2251;
                                        }
                                    }
                                }
                                {
                                    int ___var__2263 = inflate_codes(___var__2252, ___var__2253, ___var__2254, ___var__2255);
                                    {
                                        if (___var__2263) {
                                            ___var__2250 = 1;
                                        }
                                    }
                                }
                                int ___var__2264;
                                {
                                    struct huft *___var__2265 = ___var__2252;
                                    register struct huft *___var__2266;
                                    register struct huft *___var__2267;
                                    ___var__2266 = ___var__2265;
                                    while (___var__2266 != (struct huft *)((void *)0))
                                        {
                                            ___var__2267 = (--___var__2266)->v.t;
                                            free(___var__2266);
                                            ___var__2266 = ___var__2267;
                                        }
                                    ___var__2264 = 0;
                                }
                                ___var__2264;
                                int ___var__2268;
                                {
                                    struct huft *___var__2269 = ___var__2253;
                                    register struct huft *___var__2270;
                                    register struct huft *___var__2271;
                                    ___var__2270 = ___var__2269;
                                    while (___var__2270 != (struct huft *)((void *)0))
                                        {
                                            ___var__2271 = (--___var__2270)->v.t;
                                            free(___var__2270);
                                            ___var__2270 = ___var__2271;
                                        }
                                    ___var__2268 = 0;
                                }
                                ___var__2268;
                                ___var__2250 = 0;
                            }
                            ___var__2159 = ___var__2250;
                        }
                    }
                }
                ___var__2159 = 2;
            }
            {
                if ((___var__2157 = ___var__2159) != 0) {
                    return ___var__2157;
                }
            }
        }
        {
            {
                if (___var__1830 > ___var__2158) {
                    ___var__2158 = ___var__1830;
                }
            }
        }
    } while (!___var__2156);
    while (___var__1826 >= 8)
        {
            ___var__1826 -= 8;
            ___var__1683--;
        }
    flush_window();
    (___var__1684 = (___var__1684) , (void)0);
    ;
    return 0;
}



