#include <stdio.h>

#include "list.h"

void __print_json(list_t *entry_list, ebm_mode mode,
			char **prefices, char **order_name);

#ifndef JSON
#define __unused __attribute__((unused))
void print_json(list_t __unused *entry_list, ebm_mode __unused mode,
				char __unused **prefices, char __unused **order_name)
{
        printf("JSON support is not built-in\n");
}
#else
static inline void print_json(list_t *entry_list, ebm_mode mode,
				char **prefices, char **order_name)
{
        __print_json(entry_list, mode, prefices, order_name);
}
#endif
