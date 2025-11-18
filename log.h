#pragma once
#include <stdio.h>

#ifdef DEBUG
#define log_debug(...)                                                         \
	do {                                                                   \
		printf("debug: ");                                             \
		printf(__VA_ARGS__);                                           \
		fflush(stdout);                                                \
	} while (0)
#else
#define log_debug(...)                                                         \
	do {                                                                   \
	} while (0)
#endif
