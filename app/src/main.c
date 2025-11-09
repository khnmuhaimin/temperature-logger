#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
    LOG_INF("Hello, World!");
    return 0;
}