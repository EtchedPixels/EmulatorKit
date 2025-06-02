#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

/* opaque handle */
struct gdb_server;

/* front-end interface: create, destroy, run, and notify about memory access */

struct gdb_backend;
struct gdb_server *gdb_server_create(struct gdb_backend *backend, char *bindstr, bool stopped);
void gdb_server_free(struct gdb_server *gdb);
void gdb_server_step(struct gdb_server *gdb, volatile int *done);
void gdb_server_notify(struct gdb_server *gdb, unsigned long addr, unsigned int len, bool write);

/* use GNU C attributes when possible to mark format strings
   this provides nice compile time feedback about using the right formats */
#ifdef __GNUC__
#define GDB_ATTR_FORMAT(archetype, fmt, arg) __attribute__ ((format (archetype, fmt, arg)))
#else
#define GDB_ATTR_FORMAT(archetype, fmt, arg)
#endif

/* writef and friends write big-endian numbers. for little-endian, use swaps */
#define gdb_swap_u16(v) (((v & 0xff) << 8) | ((v & 0xff00) >> 8))

/* helpers for writing packets */

void gdb_writef(struct gdb_server *gdb, const char *fmt, ...) GDB_ATTR_FORMAT(printf, 2, 3);
void gdb_writefv(struct gdb_server *gdb, const char *fmt, va_list args);

/* helpers for parsing packets*/

struct gdb_packet {
	/* note: buf[len] must be safe to access. last valid byte is buf[len-1] */
	char *buf;
	size_t len;
	/* the character at buf[0], as buf[0] may be 0 temporarily */
	char c;
};

#define gdb_packet_end(p) ((p)->len == 0)
#define gdb_packet_scanf(p, end, fmt, ...) _gdb_packet_scanf(p, end, fmt "%n", ##__VA_ARGS__, end)
bool _gdb_packet_scanf(struct gdb_packet *p, int *end, const char *fmt, ...) GDB_ATTR_FORMAT(scanf, 3, 4);
bool _gdb_packet_scanfv(struct gdb_packet *p, int *end, const char *fmt, va_list args);

/* backend interface */

struct gdb_backend {
	void *ctx;
	void (*free)(void *ctx);
};
