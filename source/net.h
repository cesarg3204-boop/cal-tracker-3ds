#ifndef CT_NET_H
#define CT_NET_H

#include <stddef.h>

int net_init(void);
void net_exit(void);

/* GET a URL. On success returns 0 and a malloc'd, NUL-terminated body in *out
 * (caller frees). On failure returns -1 and writes a short reason to err. */
int net_get(const char *url, char **out, char *err, size_t err_size);

#endif
