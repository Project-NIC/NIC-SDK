/* NIC-SDK — MUF's reader, plain C: the head and its continuations, every block with its CRC
 * checked, the steps and their values raw or physical. Reads through a callback, so it runs on a
 * file, on a buffer, on a unit or in a browser (WebAssembly). Its twin in Python: tools/muf.py. */
#ifndef MUF_READ_H
#define MUF_READ_H
#include <stdint.h>
#include "muf.h"

#define MUF_MAX_GROUPS 64u

typedef int (*muf_read_fn)(void *ctx, uint8_t block[MUF_BLOCK]); /* 0 a block, 1 the end, <0 an error */

struct muf_reader {
	muf_read_fn read;
	void *ctx;
	struct muf_head head;
	struct muf_stream streams[MUF_MAX_STREAMS];
	unsigned n_streams;
	struct muf_group groups[MUF_MAX_GROUPS];
	unsigned n_groups;
	uint32_t blocks_bad;            /* blocks whose CRC failed, skipped */
};

/* A block as read: its kind, its steps, its number and first step, and its bytes. */
struct muf_block {
	uint8_t kind, steps;
	uint32_t number, first_step;
	uint8_t raw[MUF_BLOCK];
};

int muf_open(struct muf_reader *r, muf_read_fn read, void *ctx);
/* The next good block after the head: 0 one given, 1 the file's end, <0 an error. */
int muf_next(struct muf_reader *r, struct muf_block *b);
/* A stream's place in the head's table, by its kind; −1 none. */
int muf_stream_index(const struct muf_reader *r, uint8_t kind);
/* A value of a step, its group's k-th, raw (as a double) and physical. */
double muf_raw(const struct muf_reader *r, const struct muf_group *g, const uint8_t *step, unsigned k);
double muf_physical(const struct muf_reader *r, const struct muf_group *g, const uint8_t *step, unsigned k);
/* A group's byte offset in its stream's step (the groups in the order of their first). */
unsigned muf_group_offset(const struct muf_reader *r, const struct muf_group *g);

#endif
