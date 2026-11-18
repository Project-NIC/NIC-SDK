/* NIC-SDK — MUF's writer, plain C: the head, the table, the constants, every stream's blocks as
 * they fill, an index after every 200 numbered blocks, the end — as MUF.md sets them out. */
#ifndef MUF_H
#define MUF_H
#include <stddef.h>
#include <stdint.h>

#define MUF_BLOCK 512u
#define MUF_STEPS_FROM 28u      /* a block's steps from byte 28 … */
#define MUF_STEPS_TO 508u       /* … to 507 at most */
#define MUF_MAX_STREAMS 18u
#define MUF_INDEX_SLOTS 17u
#define MUF_INDEX_EVERY 200u

enum muf_kind { MUF_HEAD = 0x01, MUF_HEAD_MORE = 0x02, MUF_END = 0x03, MUF_TABLE = 0x04,
		MUF_INFO = 0x05, MUF_CONST = 0x06, MUF_INDEX = 0x07, MUF_DATA0 = 0x10 };

struct muf_stream {
	uint8_t kind;           /* 0x05, 0x06, 0x10–0x1F */
	uint32_t rate_mhz;      /* 0 irregular */
	uint8_t steps_per_block, step_bytes, flags;
};

struct muf_group {
	uint8_t stream, first, count;
	char type;              /* b B h H i I q Q e f d */
	uint8_t flags;
	int8_t exp10;
	int16_t mantissa;
	int32_t add;
	char name[9], unit[9];
	uint8_t ref;
	uint16_t range;
};

struct muf_head {
	char letter;            /* the profile's: the mark is "MUF" + it */
	uint16_t session;
	uint8_t unit;
	uint64_t chip_id;
	uint8_t fw[3];
	uint8_t time_source;
	uint64_t start_us;      /* µs since 1970, UTC; 0 unknown */
	int8_t tz_quarters;
	char name[17];
};

typedef int (*muf_write_fn)(void *ctx, const uint8_t block[MUF_BLOCK]);

struct muf_writer {
	muf_write_fn write;
	void *ctx;
	struct muf_head head;
	struct muf_stream streams[MUF_MAX_STREAMS];
	unsigned n_streams;
	uint32_t number;                       /* the next numbered block */
	uint32_t range_first;                  /* the range the next index lists */
	uint8_t range_kind[MUF_INDEX_EVERY], range_steps[MUF_INDEX_EVERY];
	unsigned range_n;
	uint32_t range_next[MUF_INDEX_SLOTS];  /* each stream's next step at the range's start */
	/* per stream: the block being filled */
	uint8_t buf[MUF_MAX_STREAMS][MUF_BLOCK];
	uint8_t filled[MUF_MAX_STREAMS];
	uint32_t next_step[MUF_MAX_STREAMS];   /* the step the next written step will be */
	uint32_t dropped;
	int error;
};

int muf_begin(struct muf_writer *w, muf_write_fn write, void *ctx, const struct muf_head *h,
	      const struct muf_stream *streams, unsigned n_streams, const struct muf_group *groups,
	      unsigned n_groups, const char *table_csv, const uint8_t *constants_step);
/* One step of a stream (its index in the head's table), its bytes as the groups lay them. */
int muf_step(struct muf_writer *w, unsigned stream, const uint8_t *step);
/* Skip steps (a pause: "a jump in the numbers is a pause, with no data in it"). */
void muf_skip(struct muf_writer *w, unsigned stream, uint32_t steps);
/* Write a stream's part-filled block now (a slow stream at least every few seconds). */
int muf_flush(struct muf_writer *w, unsigned stream);
int muf_end(struct muf_writer *w, uint8_t why);

#endif
