/* NIC-SDK — a MUF file opened for the tools: its head, or, when the head fails, its mirror in the
 * file's last blocks (MUF.md §1); the reader then stands past the head's blocks. */
#ifndef MUFFILE_H
#define MUFFILE_H
#include <stdio.h>
#include <stdlib.h>
#include "../muf/muf_read.h"

static int muf_file_read(void *ctx, uint8_t b[MUF_BLOCK])
{
	size_t n = fread(b, 1, MUF_BLOCK, ctx);

	return n == MUF_BLOCK ? 0 : n == 0 ? 1 : -1;
}

static int muf_open_file(struct muf_reader *r, FILE *f)
{
	if (muf_open(r, muf_file_read, f) == 0) {
		return 0;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		return -1;
	}
	long size = ftell(f);
	unsigned n = size / MUF_BLOCK > 256 ? 256 : (unsigned)(size / MUF_BLOCK), k = 0;
	uint8_t *tail = malloc((size_t)MUF_BLOCK * (n ? n : 1));
	int e = -1;
	if (tail && n && fseek(f, size - (long)MUF_BLOCK * n, SEEK_SET) == 0 &&
	    fread(tail, MUF_BLOCK, n, f) == n && (k = muf_mirror(tail, n)) != 0 &&
	    fseek(f, (long)MUF_BLOCK * k, SEEK_SET) == 0) {
		e = muf_open_blocks(r, tail + (size_t)MUF_BLOCK * (n - k), k, muf_file_read, f);
	}
	free(tail);
	return e;
}

#endif
