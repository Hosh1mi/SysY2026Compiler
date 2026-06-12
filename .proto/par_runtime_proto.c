// 5.2 双核 runtime 设计原型（仅验证用，不进产品；产品形态是编译器在 .s
// 尾部直接发射等价汇编）。
// 约束模拟：评测 `gcc out.s -L lib -lsysy -static`，只能依赖 libc.a 必有
// 符号（clone / sched_setaffinity / syscall），不依赖 pthread/futex。
// 握手：纯自旋 + acquire/release——isolcpus=2,3 下两核独占，自旋零代价。
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// ---- 编译器将发射的 runtime 部分 ----------------------------------------
static char __sysy_wstack[1 << 20] __attribute__((aligned(16)));
static volatile int __sysy_job_seq = 0;   // 主线程发布计数
static volatile int __sysy_done_seq = 0;  // 工作线程完成计数
static volatile int __sysy_job_id, __sysy_job_lo, __sysy_job_hi;
static int __sysy_worker_started = 0;

// 编译器生成的分发函数（这里手写模拟）
void __sysy_par_dispatch(int id, int lo, int hi);

static cpu_set_t __sysy_orig_mask; // pin 之前的进程掩码（评测机={2,3}）
static int __sysy_orig_mask_valid = 0;

static void __sysy_bind_cpu(int which) {
    // 取原始掩码里的第 which 个核——必须用 pin 之前存下的掩码，
    // 否则 clone 子线程继承主线程的单核掩码后找不到第二个核。
    if (!__sysy_orig_mask_valid) return;
    int found = 0;
    for (int c = 0; c < CPU_SETSIZE; c++) {
        if (!CPU_ISSET(c, &__sysy_orig_mask)) continue;
        if (found == which) {
            cpu_set_t one;
            CPU_ZERO(&one);
            CPU_SET(c, &one);
            sched_setaffinity(0, sizeof(one), &one); // 失败则保持原掩码
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
            ; // spin
        seen++;
        __sysy_par_dispatch(__sysy_job_id, __sysy_job_lo, __sysy_job_hi);
        __atomic_fetch_add(&__sysy_done_seq, 1, __ATOMIC_RELEASE);
    }
    return 0;
}

void __sysy_parallel_for(int id, int lo, int hi) {
    if (hi - lo < 2) { // 退化：不值得切
        __sysy_par_dispatch(id, lo, hi);
        return;
    }
    if (!__sysy_worker_started) {
        __sysy_worker_started = 1;
        if (sched_getaffinity(0, sizeof(__sysy_orig_mask), &__sysy_orig_mask) == 0)
            __sysy_orig_mask_valid = 1;
        __sysy_bind_cpu(0);
        clone(__sysy_worker, __sysy_wstack + sizeof(__sysy_wstack),
              CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                  CLONE_THREAD | CLONE_SYSVSEM,
              0);
    }
    int mid = lo + (hi - lo) / 2;
    __sysy_job_id = id;
    __sysy_job_lo = mid;
    __sysy_job_hi = hi;
    int target = __sysy_job_seq + 1;
    __atomic_fetch_add(&__sysy_job_seq, 1, __ATOMIC_RELEASE);
    __sysy_par_dispatch(id, lo, mid);
    while (__atomic_load_n(&__sysy_done_seq, __ATOMIC_ACQUIRE) != target)
        ; // spin
}

// ---- 模拟"编译器外提产物"：matmul 外层 i 循环 ---------------------------
#define N 1024
static int A[N][N], B[N][N], C[N][N];

static void mm_body(int lo, int hi) { // 外提的循环体（i in [lo,hi)）
    for (int i = lo; i < hi; i++)
        for (int k = 0; k < N; k++) {
            int a = A[i][k];
            for (int j = 0; j < N; j++)
                C[i][j] += a * B[k][j];
        }
}

static volatile int sink;
static void empty_body(int lo, int hi) { sink += hi - lo; }

void __sysy_par_dispatch(int id, int lo, int hi) {
    if (id == 0) { mm_body(lo, hi); return; }
    if (id == 1) { empty_body(lo, hi); return; }
}

static long long now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

int main(int argc, char **argv) {
    int parallel = argc > 1 && argv[1][0] == 'p';
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            A[i][j] = i * 3 + j;
            B[i][j] = i - 2 * j;
        }
    if (argv[1] && argv[1][0] == 'o') { // 握手净开销：100 万次空体任务
        __sysy_parallel_for(1, 0, 2); // 预热（建线程）
        const int R = 1000000;
        long long o0 = now_us();
        for (int r = 0; r < R; r++)
            __sysy_parallel_for(1, 0, 4);
        long long o1 = now_us();
        long long d0 = now_us();
        for (int r = 0; r < R; r++)
            __sysy_par_dispatch(1, 0, 4);
        long long d1 = now_us();
        printf("par_invoke_ns=%lld serial_invoke_ns=%lld overhead_ns=%lld\n",
               (o1 - o0) * 1000 / R, (d1 - d0) * 1000 / R,
               ((o1 - o0) - (d1 - d0)) * 1000 / R);
        return 0;
    }
    long long t0 = now_us();
    if (parallel)
        __sysy_parallel_for(0, 0, N);
    else
        mm_body(0, N);
    long long t1 = now_us();
    long long sum = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            sum += C[i][j] % 1000;
    printf("mode=%s time_us=%lld checksum=%lld\n", parallel ? "par" : "ser",
           t1 - t0, sum);
    return 0;
}
