#ifndef TEST_H
#define TEST_H

#if CONFIG_BUILD_TEST_APP
// disables static
#define EXPOSE_FOR_TESTING
#else
// enables static
#define EXPOSE_FOR_TESTING static
#endif

#endif