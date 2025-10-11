#include <includes.h>
#include <bsp.h>
#include <bsp_os.h>
#include <uart.h>

#define TEST_TASK_A_PRIO    5u
#define TEST_TASK_B_PRIO    6u
#define TEST_STACK_SIZE     4096u
#define TEST_ITERATIONS     5u
#define TICK_TARGET         1000u
#define TICK_TOLERANCE      150u

static OS_STK test_task_a_stack[TEST_STACK_SIZE];
static OS_STK test_task_b_stack[TEST_STACK_SIZE];

static volatile INT32U task_a_runs = 0;
static volatile INT32U task_b_runs = 0;
static INT32U tick_intervals[TEST_ITERATIONS];
static volatile INT8U test_completed = 0u;
extern INT32U OSCtxSwCtr;

static inline INT32U abs_diff(INT32U a, INT32U b)
{
    return (a > b) ? (a - b) : (b - a);
}

static void task_b(void *p_arg)
{
    (void)p_arg;

    while (test_completed == 0u) {
        task_b_runs++;
        OSTimeDlyHMSM(0u, 0u, 0u, 500u);
    }

    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

static void task_a(void *p_arg)
{
    (void)p_arg;

    BSP_OS_TmrTickInit(1000u);

    INT32U cs_before = OSCtxSwCtr;
    INT32U test_start = OSTimeGet();
    INT32U min_interval = 0xFFFFFFFFu;
    INT32U max_interval = 0u;
    INT32U interval_sum = 0u;

    for (INT32U i = 0u; i < TEST_ITERATIONS; ++i) {
        INT32U start = OSTimeGet();
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
        INT32U stop = OSTimeGet();

        tick_intervals[i] = stop - start;
        interval_sum += tick_intervals[i];
        if (tick_intervals[i] < min_interval) {
            min_interval = tick_intervals[i];
        }
        if (tick_intervals[i] > max_interval) {
            max_interval = tick_intervals[i];
        }
        task_a_runs++;
    }

    while (task_b_runs < TEST_ITERATIONS) {
        OSTimeDlyHMSM(0u, 0u, 0u, 100u);
    }

    INT32U within_tolerance = 0u;
    for (INT32U i = 0u; i < TEST_ITERATIONS; ++i) {
        if (abs_diff(tick_intervals[i], TICK_TARGET) <= TICK_TOLERANCE) {
            within_tolerance++;
        }
    }

    INT32U test_end = OSTimeGet();
    INT32U duration = test_end - test_start;
    INT32U ctx_switches = OSCtxSwCtr - cs_before;
    INT32U avg_interval = (interval_sum + (TEST_ITERATIONS / 2u)) / TEST_ITERATIONS;

    printf("[STATS] ctx_sw=%u duration_ms=%u avg_tick=%u\n",
           ctx_switches,
           duration,
           avg_interval);
    printf("[RESULT] ticks: min=%u max=%u count=%u/%u\n",
           min_interval,
           max_interval,
           within_tolerance,
           TEST_ITERATIONS);

    if ((task_a_runs >= TEST_ITERATIONS) &&
        (task_b_runs >= TEST_ITERATIONS) &&
        (within_tolerance == TEST_ITERATIONS)) {
        printf("[PASS] Context switch and timer stability verified\n");
    } else {
        printf("[FAIL] Context/timer test failed\n");
    }

    test_completed = 1u;

    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

int main(void)
{
    INT8U err;

    printf("[TEST] Context switch & timer validation start\n");

    CPU_Init();
    Mem_Init();
    BSP_Init();

    OSInit();

    err = OSTaskCreate(task_a,
                       0,
                       &test_task_a_stack[TEST_STACK_SIZE - 1u],
                       TEST_TASK_A_PRIO);
    if (err != OS_ERR_NONE) {
        printf("[ERROR] Failed to create Task A (err=%u)\n", err);
        return 1;
    }

    err = OSTaskCreate(task_b,
                       0,
                       &test_task_b_stack[TEST_STACK_SIZE - 1u],
                       TEST_TASK_B_PRIO);
    if (err != OS_ERR_NONE) {
        printf("[ERROR] Failed to create Task B (err=%u)\n", err);
        return 1;
    }

    __asm__ volatile("msr daifclr, #0x2");

    printf("[TEST] Scheduler start\n");
    OSStart();

    printf("[ERROR] Returned from OSStart()\n");
    return 1;
}
