/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#ident "$Id$"

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <toku_assert.h>
#if defined(HAVE_MALLOC_H)
# include <malloc.h>
#elif defined(HAVE_SYS_MALLOC_H)
# include <sys/malloc.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#if defined(HAVE_SYSCALL_H)
# include <syscall.h>
#endif
#if defined(HAVE_SYS_SYSCALL_H)
# include <sys/syscall.h>
#endif
#if defined(HAVE_SYS_SYSCTL_H)
# include <sys/sysctl.h>
#endif
#if defined(HAVE_PTHREAD_NP_H)
# include <pthread_np.h>
#endif
#include <inttypes.h>
#include <sys/time.h>
#if defined(HAVE_SYS_RESOURCE_H)
# include <sys/resource.h>
#endif
#include <sys/statvfs.h>
#include "toku_portability.h"
#include "toku_os.h"
#include "toku_time.h"
#include "memory.h"
#include <portability/toku_atomic.h>
#include <util/partitioned_counter.h>

int
toku_portability_init(void) {
    int r = toku_memory_startup();
    return r;
}

void
toku_portability_destroy(void) {
    toku_memory_shutdown();
}

int
toku_os_getpid(void) {
    return getpid();
}

int
toku_os_gettid(void) {
#if defined(__NR_gettid)
    return syscall(__NR_gettid);
#elif defined(SYS_gettid)
    return syscall(SYS_gettid);
#elif defined(HAVE_PTHREAD_GETTHREADID_NP)
    return pthread_getthreadid_np();
#else
# error "no implementation of gettid available"
#endif
}

int
toku_os_get_number_processors(void) {
    return sysconf(_SC_NPROCESSORS_CONF);
}

int
toku_os_get_number_active_processors(void) {
    int n = sysconf(_SC_NPROCESSORS_ONLN);
#define DO_TOKU_NCPUS 1
#if DO_TOKU_NCPUS
    {
        char *toku_ncpus = getenv("TOKU_NCPUS");
        if (toku_ncpus) {
            int ncpus = atoi(toku_ncpus);
            if (ncpus < n)
                n = ncpus;
        }
    }
#endif
    return n;
}

int
toku_os_get_pagesize(void) {
    return sysconf(_SC_PAGESIZE);
}

uint64_t
toku_os_get_phys_memory_size(void) {
#if defined(_SC_PHYS_PAGES)
    uint64_t npages = sysconf(_SC_PHYS_PAGES);
    uint64_t pagesize = sysconf(_SC_PAGESIZE);
    return npages*pagesize;
#elif defined(HAVE_SYS_SYSCTL_H)
    uint64_t memsize;
    size_t len = sizeof memsize;
    sysctlbyname("hw.memsize", &memsize, &len, NULL, 0);
    return memsize;
#else
# error "cannot find _SC_PHYS_PAGES or sysctlbyname()"
#endif
}

int
toku_os_get_file_size(int fildes, int64_t *fsize) {
    toku_struct_stat sbuf;
    int r = fstat(fildes, &sbuf);
    if (r==0) {
        *fsize = sbuf.st_size;
    }
    return r;
}

int
toku_os_get_unique_file_id(int fildes, struct fileid *id) {
    toku_struct_stat statbuf;
    memset(id, 0, sizeof(*id));
    int r=fstat(fildes, &statbuf);
    if (r==0) {
        id->st_dev = statbuf.st_dev;
        id->st_ino = statbuf.st_ino;
    }
    return r;
}

int
toku_os_lock_file(const char *name) {
    int r;
    int fd = open(name, O_RDWR|O_CREAT, S_IRUSR | S_IWUSR);
    if (fd>=0) {
        r = flock(fd, LOCK_EX | LOCK_NB);
        if (r!=0) {
            r = errno; //Save errno from flock.
            close(fd);
            fd = -1; //Disable fd.
            errno = r;
        }
    }
    return fd;
}

int
toku_os_unlock_file(int fildes) {
    int r = flock(fildes, LOCK_UN);
    if (r==0) r = close(fildes);
    return r;
}

int
toku_os_mkdir(const char *pathname, mode_t mode) {
    int r = mkdir(pathname, mode);
    return r;
}

int
toku_os_get_process_times(struct timeval *usertime, struct timeval *kerneltime) {
    int r;
    struct rusage rusage;
    r = getrusage(RUSAGE_SELF, &rusage);
    if (r == -1)
        return get_error_errno();
    if (usertime) 
        *usertime = rusage.ru_utime;
    if (kerneltime)
        *kerneltime = rusage.ru_stime;
    return 0;
}

int
toku_os_initialize_settings(int UU(verbosity)) {
    int r = 0;
    static int initialized = 0;
    assert(initialized==0);
    initialized=1;
    return r;
}

bool toku_os_is_absolute_name(const char* path) {
    return path[0] == '/';
}

int
toku_os_get_max_process_data_size(uint64_t *maxdata) {
    int r;
    struct rlimit rlimit;

    r = getrlimit(RLIMIT_DATA, &rlimit);
    if (r == 0) {
        uint64_t d;
        d = rlimit.rlim_max;
	// with the "right" macros defined, the rlimit is a 64 bit number on a
	// 32 bit system.  getrlimit returns 2**64-1 which is clearly wrong.

        // for 32 bit processes, we assume that 1/2 of the address space is
        // used for mapping the kernel.  this may be pessimistic.
        if (sizeof (void *) == 4 && d > (1ULL << 31))
            d = 1ULL << 31;
	*maxdata = d;
    } else
        r = get_error_errno();
    return r;
}

int
toku_stat(const char *name, toku_struct_stat *buf) {
    int r = stat(name, buf);
    return r;
}

int
toku_fstat(int fd, toku_struct_stat *buf) {
    int r = fstat(fd, buf);
    return r;
}

static int
toku_get_processor_frequency_sys(uint64_t *hzret) {
    int r;
    FILE *fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (!fp) 
        r = get_error_errno();
    else {
        unsigned int khz = 0;
        if (fscanf(fp, "%u", &khz) == 1) {
            *hzret = khz * 1000ULL;
            r = 0;
        } else
            r = ENOENT;
        fclose(fp);
    }
    return r;
}

static int
toku_get_processor_frequency_cpuinfo(uint64_t *hzret) {
    int r;
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        r = get_error_errno();
    } else {
        uint64_t maxhz = 0;
        char *buf = NULL;
        size_t n = 0;
        while (getline(&buf, &n, fp) >= 0) {
            unsigned int cpu;
            sscanf(buf, "processor : %u", &cpu);
            unsigned int ma, mb;
            if (sscanf(buf, "cpu MHz : %u.%u", &ma, &mb) == 2) {
                uint64_t hz = ma * 1000000ULL + mb * 1000ULL;
                if (hz > maxhz)
                    maxhz = hz;
            }
        }
        if (buf)
            free(buf);
        fclose(fp);
        *hzret = maxhz;
        r = maxhz == 0 ? ENOENT : 0;;
    }
    return r;
}

static int
toku_get_processor_frequency_sysctl(const char * const cmd, uint64_t *hzret) {
    int r = 0;
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        r = EINVAL;  // popen doesn't return anything useful in errno,
                     // gotta pick something
        goto exit;
    }
    r = fscanf(fp, "%" SCNu64, hzret);
    if (r != 1) {
        r = get_maybe_error_errno();
    } else {
        r = 0;
    }
    pclose(fp);

exit:
    return r;
}

int
toku_os_get_processor_frequency(uint64_t *hzret) {
    int r;
    r = toku_get_processor_frequency_sys(hzret);
    if (r != 0)
        r = toku_get_processor_frequency_cpuinfo(hzret);
    if (r != 0)
        r = toku_get_processor_frequency_sysctl("sysctl -n hw.cpufrequency", hzret);
    if (r != 0)
        r = toku_get_processor_frequency_sysctl("sysctl -n machdep.tsc_freq", hzret);
    return r;
}

int
toku_get_filesystem_sizes(const char *path, uint64_t *avail_size, uint64_t *free_size, uint64_t *total_size) {
    struct statvfs s;
    int r = statvfs(path, &s);
    if (r == -1) {
        r = get_error_errno();
    } else {
        // get the block size in bytes
        uint64_t bsize = s.f_frsize ? s.f_frsize : s.f_bsize;
        // convert blocks to bytes
        if (avail_size)
            *avail_size = (uint64_t) s.f_bavail * bsize;
        if (free_size) 
            *free_size = (uint64_t) s.f_bfree * bsize;
        if (total_size) 
            *total_size = (uint64_t) s.f_blocks * bsize;
    }
    return r;
}


int
toku_dup2(int fd, int fd2) {
    int r;
    r = dup2(fd, fd2);
    return r;
}


// Time
static       double seconds_per_clock = -1;

double tokutime_to_seconds(tokutime_t t) {
    // Convert tokutime to seconds.
    if (seconds_per_clock<0) {
	uint64_t hz;
	int r = toku_os_get_processor_frequency(&hz);
	assert(r==0);
	// There's a race condition here, but it doesn't really matter.  If two threads call tokutime_to_seconds
	// for the first time at the same time, then both will fetch the value and set the same value.
	seconds_per_clock = 1.0/hz;
    }
    return t*seconds_per_clock;
}
