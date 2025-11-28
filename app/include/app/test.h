#ifndef TEST_H
#define TEST_H

#if CONFIG_ENABLE_TESTING
#define EXPOSE_FOR_TESTING
#else
#define EXPOSE_FOR_TESTING static
#endif

#endif