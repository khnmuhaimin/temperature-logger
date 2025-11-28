#ifndef LOCK_H
#define LOCK_H

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define DOUBLE_LOCK(lock1, lock2, timeout)       \
    do                                           \
    {                                            \
        if ((uintptr_t)lock1 < (uintptr_t)lock2) \
        {                                        \
            k_mutex_lock(lock1, timeout);        \
            k_mutex_lock(lock2, timeout);        \
        }                                        \
        else                                     \
        {                                        \
            k_mutex_lock(lock2, timeout);        \
            k_mutex_lock(lock1, timeout);        \
        }                                        \
    } while (0);

#define DOUBLE_UNLOCK(lock1, lock2)              \
    do                                           \
    {                                            \
        if ((uintptr_t)lock1 < (uintptr_t)lock2) \
        {                                        \
            k_mutex_unlock(lock2);               \
            k_mutex_unlock(lock1);               \
        }                                        \
        else                                     \
        {                                        \
            k_mutex_unlock(lock1);               \
            k_mutex_unlock(lock2);               \
        }                                        \
    } while (0);

#define TRIPLE_LOCK(lock1, lock2, lock3, timeout)                  \
    do                                                             \
    {                                                              \
        struct k_mutex *locks[3] = {lock1, lock2, lock3};          \
        struct k_mutex *temp;                                      \
        /* Simple Bubble Sort based on address */                  \
        for (int i = 0; i < 2; i++)                                \
        {                                                          \
            for (int j = 0; j < 2 - i; j++)                        \
            {                                                      \
                if ((uintptr_t)locks[j] > (uintptr_t)locks[j + 1]) \
                {                                                  \
                    temp = locks[j];                               \
                    locks[j] = locks[j + 1];                       \
                    locks[j + 1] = temp;                           \
                }                                                  \
            }                                                      \
        }                                                          \
        k_mutex_lock(locks[0], timeout);                           \
        k_mutex_lock(locks[1], timeout);                           \
        k_mutex_lock(locks[2], timeout);                           \
    } while (0);

#define TRIPLE_UNLOCK(lock1, lock2, lock3)                         \
    do                                                             \
    {                                                              \
        struct k_mutex *locks[3] = {lock1, lock2, lock3};          \
        /* Simple Bubble Sort based on address */                  \
        for (int i = 0; i < 2; i++)                                \
        {                                                          \
            for (int j = 0; j < 2 - i; j++)                        \
            {                                                      \
                if ((uintptr_t)locks[j] > (uintptr_t)locks[j + 1]) \
                {                                                  \
                    struct k_mutex *temp = locks[j];               \
                    locks[j] = locks[j + 1];                       \
                    locks[j + 1] = temp;                           \
                }                                                  \
            }                                                      \
        }                                                          \
        k_mutex_unlock(locks[2]);                                  \
        k_mutex_unlock(locks[1]);                                  \
        k_mutex_unlock(locks[0]);                                  \
    } while (0);

#endif