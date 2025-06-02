#include <stdbool.h>
#include <stdlib.h>

/* opaque handle */
struct gdb_server;

/* front-end interface: create, destroy, run, and notify about memory access */

struct gdb_backend;
struct gdb_server *gdb_server_create(struct gdb_backend *backend, char *bindstr, bool stopped);
void gdb_server_free(struct gdb_server *gdb);
void gdb_server_step(struct gdb_server *gdb, volatile int *done);
void gdb_server_notify(struct gdb_server *gdb, unsigned long addr, unsigned int len, bool write);

/* backend interface */

struct gdb_backend {
	void *ctx;
	void (*free)(void *ctx);
};
