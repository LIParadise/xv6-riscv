#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static atomic_bool    guard           = false;
static atomic_bool    payload         = false;
static const uint32_t RX_DELAY_AMOUNT = 456u;
static const uint32_t EXP_TIMES       = 3000u;

void reset(void)
{
    atomic_store_explicit(&payload, false, memory_order_relaxed);
    atomic_store_explicit(&guard, false, memory_order_relaxed);
}

void random_delay(const uint32_t delay)
{
    for (uint32_t u = 0; u < delay; ++u)
    {
        for (int32_t times = 5 + rand() % 32; times >= 0; --times)
        {
            asm volatile("" ::: "memory");
        }
    }
}

enum Experiment
{
    Invalid = 0,
    ConditionNotMet,
    InOrder,
    OutOfOrder,
};

void *rx_relaxed(void *unused)
{
    random_delay(RX_DELAY_AMOUNT + atomic_load_explicit(&payload, memory_order_relaxed));
    if (atomic_load_explicit(&guard, memory_order_relaxed))
    {
        return atomic_load_explicit(&payload, memory_order_relaxed) ? (void *)(uintptr_t)InOrder
                                                                    : (void *)(uintptr_t)OutOfOrder;
    }
    else
    {
        return (void *)(uintptr_t)ConditionNotMet;
    }
}

void *rx_acquire(void *unused)
{
    random_delay(RX_DELAY_AMOUNT + atomic_load_explicit(&payload, memory_order_relaxed));
    if (atomic_load_explicit(&guard, memory_order_acquire))
    {
        return atomic_load_explicit(&payload, memory_order_relaxed) ? (void *)(uintptr_t)InOrder
                                                                    : (void *)(uintptr_t)OutOfOrder;
    }
    else
    {
        return (void *)(uintptr_t)ConditionNotMet;
    }
}

void *tx_seqcst(void *unused)
{
    atomic_store_explicit(&payload, true, memory_order_relaxed);
    atomic_store_explicit(&guard, true, memory_order_seq_cst);
    return NULL;
}

void *tx_gcc_builtin_sync_synchronize(void *unused)
{
    atomic_store_explicit(&payload, true, memory_order_relaxed);
    __sync_synchronize();
    atomic_store_explicit(&guard, true, memory_order_relaxed);
    return NULL;
}

typedef void *(*const pthread_fp)(void *);
void my_exp(const char *const exp_name, pthread_fp tx_fp, pthread_fp rx_fp)
{
    size_t    ooo_cnt      = 0;
    size_t    not_met_cnt  = 0;
    size_t    in_order_cnt = 0;
    pthread_t tx_handle;
    pthread_t rx_handle;
    for (size_t s = 0; s < (size_t)EXP_TIMES; ++s)
    {
        enum Experiment exp_stat = Invalid;
        reset();
        pthread_create(&rx_handle, NULL, rx_fp, NULL);
        pthread_create(&tx_handle, NULL, tx_fp, NULL);
        pthread_join(rx_handle, (void **)(void *)&exp_stat);
        switch (exp_stat)
        {
        case OutOfOrder:
            ooo_cnt++;
            break;
        case InOrder:
            in_order_cnt++;
            break;
        case ConditionNotMet:
            not_met_cnt++;
            break;
        default:
            exit(42069);
            break;
        }
        pthread_join(tx_handle, NULL);
    }
    printf("%50.50s, not met/in/ooo are resp. %lu/%lu/%lu\n", exp_name, not_met_cnt, in_order_cnt, ooo_cnt);
}

int main()
{
    srand(time(NULL));
    my_exp("tx_gcc_builtin_sync_synchronize, rx_relaxed", tx_gcc_builtin_sync_synchronize, rx_relaxed);
    my_exp("tx_gcc_builtin_sync_synchronize, rx_acquire", tx_gcc_builtin_sync_synchronize, rx_acquire);
    my_exp("tx_seqcst, rx_relaxed", tx_seqcst, rx_relaxed);
    my_exp("tx_seqcst, rx_acquire", tx_seqcst, rx_acquire);
}
