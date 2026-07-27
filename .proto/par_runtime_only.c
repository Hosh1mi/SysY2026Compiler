// 5.2 runtime 源（gcc -O2 -S 生成嵌入用汇编；见 parallel-runtime-design）
#define _GNU_SOURCE
#include <sched.h>

void __sysy_par_dispatch(int id, int lo, int hi); // 编译器生成

static char __sysy_wstack[1 << 20] __attribute__((aligned(16)));
static volatile int __sysy_job_seq = 0;
static volatile int __sysy_done_seq = 0;
static volatile int __sysy_job_id, __sysy_job_lo, __sysy_job_hi;
static int __sysy_worker_started = 0;
static int __sysy_worker_ok = 0; // clone 失败时整段降级串行
static cpu_set_t __sysy_orig_mask;
static int __sysy_orig_mask_valid = 0;

static void __sysy_bind_cpu(int which) {
    if (!__sysy_orig_mask_valid) return;
    int found = 0;
    for (int c = 0; c < CPU_SETSIZE; c++) {
        if (!CPU_ISSET(c, &__sysy_orig_mask)) continue;
        if (found == which) {
            cpu_set_t one;
            CPU_ZERO(&one);
            CPU_SET(c, &one);
            sched_setaffinity(0, sizeof(one), &one);
            return;
        }
        found++;
    }
}

static int __sysy_worker(void *arg) {
    (void)arg;
    __sysy_bind_cpu(1);
    int seen = 0;
    for (;;) {
        while (__atomic_load_n(&__sysy_job_seq, __ATOMIC_ACQUIRE) == seen)
            ;
        seen++;
        __sysy_par_dispatch(__sysy_job_id, __sysy_job_lo, __sysy_job_hi);
        // There is exactly one worker and the producer waits for every job
        // before publishing the next one.  Publishing the completed sequence
        // needs release ordering, but not an atomic read-modify-write.
        __atomic_store_n(&__sysy_done_seq, seen, __ATOMIC_RELEASE);
    }
    return 0;
}

void __sysy_parallel_for(int id, int lo, int hi) {
    if (hi - lo < 2) {
        __sysy_par_dispatch(id, lo, hi);
        return;
    }
    if (!__sysy_worker_started) {
        __sysy_worker_started = 1;
        if (sched_getaffinity(0, sizeof(__sysy_orig_mask), &__sysy_orig_mask) == 0)
            __sysy_orig_mask_valid = 1;
        __sysy_bind_cpu(0);
        __sysy_worker_ok =
            clone(__sysy_worker, __sysy_wstack + sizeof(__sysy_wstack),
                  CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                      CLONE_THREAD | CLONE_SYSVSEM,
                  0) > 0;
    }
    if (!__sysy_worker_ok) { // 环境禁线程：降级串行，避免发布任务后挂死
        __sysy_par_dispatch(id, lo, hi);
        return;
    }
    int mid = lo + (hi - lo) / 2;
    __sysy_job_id = id;
    __sysy_job_lo = mid;
    __sysy_job_hi = hi;
    int target = __sysy_job_seq + 1;
    // Single producer: a release store publishes both the context fields and
    // the new sequence number to the worker's acquire load.
    __atomic_store_n(&__sysy_job_seq, target, __ATOMIC_RELEASE);
    __sysy_par_dispatch(id, lo, mid);
    while (__atomic_load_n(&__sysy_done_seq, __ATOMIC_ACQUIRE) != target)
        ;
}
