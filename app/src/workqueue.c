#include <zephyr/kernel.h>
#include <zephyr/kernel/thread_stack.h>

#define APP_WORKQUEUE_STACK_SIZE 512
#define APP_WORKQUEUE_PRIORITY 5

K_THREAD_STACK_DEFINE(app_workqueue_stack, APP_WORKQUEUE_STACK_SIZE);

struct k_work_q app_workqueue;

void init_app_workqueue()
{
    k_work_queue_init(&app_workqueue);
    k_work_queue_start(
        &app_workqueue,
        app_workqueue_stack,
        K_THREAD_STACK_SIZEOF(app_workqueue_stack),
        APP_WORKQUEUE_PRIORITY,
        NULL);
}