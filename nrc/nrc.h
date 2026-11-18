/* NRC — the NIC Rice Codec, the C reference of its core.
 *
 * One series of n values of w bits to a bit stream and back, losslessly, exactly as NRC.md fixes
 * it and as ../tools/nrc.py writes it: the two write the same bytes. A profile cuts its records
 * into series and writes them, with gamma-coded counts, in its own order (NRC.md, §7). C99, no
 * state, no allocation but malloc and free.
 */
#ifndef NRC_H
#define NRC_H

#include <stddef.h>
#include <stdint.h>

enum {
    NRC_OK      = 0,
    NRC_DAMAGED = 1,   /* decode: the stream says what no coder writes, or ends wrong */
    NRC_EINVAL  = 2,   /* the input is not what NRC codes */
    NRC_ENOMEM  = 3
};

typedef struct {       /* a growing bit stream, most significant bit first */
    uint8_t *buf;
    size_t len, cap;
    uint64_t acc;
    unsigned nbits;
    int nomem;
} nrc_writer;

typedef struct {       /* a stream being read; `bad` once a read went past its end */
    const uint8_t *d;
    uint64_t pos, end;
    int bad;
} nrc_reader;

void     nrc_write(nrc_writer *b, uint64_t v, unsigned width);
void     nrc_gamma(nrc_writer *b, uint64_t m);              /* m >= 1 */
void     nrc_flush(nrc_writer *b);                          /* pad to a whole byte */
int      nrc_write_series(nrc_writer *b, const uint32_t *xs, size_t n, unsigned w, int is_signed,
                          const uint32_t *ref);             /* ref: the reference series or NULL */

void     nrc_reader_init(nrc_reader *r, const uint8_t *d, size_t len);
uint64_t nrc_read(nrc_reader *r, unsigned width);
uint64_t nrc_read_gamma(nrc_reader *r);
int      nrc_read_series(nrc_reader *r, size_t n, unsigned w, int is_signed, const uint32_t *ref,
                         uint32_t *xs);
int      nrc_finish(const nrc_reader *r);                   /* NRC_DAMAGED if it ends wrong */

#endif
