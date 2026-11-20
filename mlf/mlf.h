/* MLF — the Matroshka Log File, the C reference of its core.
 *
 * A file as MLF.md fixes it and as ../tools/mlf.py writes it: the two write the same bytes. The
 * writer holds the file in memory and appends as a writer on a card does — an object's framed
 * bytes, then its entry; what a segment's body holds is the profile's. C99.
 */
#ifndef MLF_H
#define MLF_H

#include <stddef.h>
#include <stdint.h>

#define MLF_MB           (1u << 20)
#define MLF_FORMAT       1u       /* for ever: MLF has no versions (MLF.md, §11) */
#define MLF_MAX_SECTIONS 64u
#define MLF_CODEC        255u     /* the core's section: a codec's table */
#define MLF_SECTOR       512u     /* the header fills whole sectors; its mirror the last ones */

enum { MLF_SEG = 1, MLF_CFG = 2 };     /* an entry's type */
enum { MLF_RAW = 0, MLF_NRC = 1 };     /* a segment's codec */
enum {
    MLF_OK = 0,
    MLF_DAMAGED = 1,     /* not whole: a CRC, a length, a sync, the framing */
    MLF_EINVAL = 2,      /* what the format does not hold */
    MLF_ENOMEM = 3,
    MLF_FULL = 4,        /* the object and its entry would cross */
    MLF_UNKNOWN = 5      /* a codec this reader does not know */
};

uint32_t mlf_crc32(const uint8_t *b, size_t n);     /* IEEE 802.3, zlib's */
uint16_t mlf_crc16(const uint8_t *b, size_t n);     /* CCITT-FALSE: 0x1021, from 0xFFFF */

/* COBS: `out` holds n + n / 254 + 1 bytes; decode's `out` holds n. */
size_t mlf_cobs_encode(const uint8_t *in, size_t n, uint8_t *out);
int mlf_cobs_decode(const uint8_t *in, size_t n, uint8_t *out, size_t *outlen);

typedef struct {
    uint8_t id;
    uint32_t length;
    const uint8_t *body;
} mlf_section;

/* The sections in force: the header's, each config's ids replacing all of theirs. The bodies
 * are copied. */
typedef struct {
    size_t n;
    mlf_section s[MLF_MAX_SECTIONS];
} mlf_sections;

int mlf_sections_parse(const uint8_t *b, size_t len, mlf_sections *out);   /* bodies point in b */
int mlf_sections_apply(mlf_sections *in_force, const mlf_sections *config);
void mlf_sections_free(mlf_sections *s);                                   /* owned copies only */

typedef struct {
    uint16_t sequence;
    uint32_t first_second;
    uint32_t source;     /* whose series, read as the profile says */
    uint8_t size;        /* 1 MB × 2^size */
    uint32_t length;     /* parse: the whole header */
    const uint8_t *sections;
    size_t sections_len;
    char letter;         /* the profile's, the mark's fourth byte; 0 builds 'U' */
} mlf_header;

size_t mlf_header_build(const mlf_header *h, uint8_t *out);  /* out holds 24 + sections_len */
int mlf_header_parse(const uint8_t *b, size_t len, mlf_header *h);

/* Unframed objects, malloc'd. */
int mlf_segment_build(uint32_t first, uint32_t last, uint8_t codec, const uint8_t *body,
                      size_t blen, uint8_t **out, size_t *len);
/* An even run (MLF.md §4): the time byte 1 and the first record's place in its first second. */
int mlf_run_build(uint32_t first, uint32_t last, uint8_t codec, uint16_t place, const uint8_t *body,
                  size_t blen, uint8_t **out, size_t *len);
int mlf_config_build(uint32_t from_second, const uint8_t *sections, size_t slen, uint8_t **out,
                     size_t *len);

typedef struct {
    uint32_t second, offset, length;
    uint8_t type;
} mlf_entry;

void mlf_entry_build(const mlf_entry *e, uint8_t out[16]);
int mlf_entry_parse(const uint8_t b[16], mlf_entry *e);    /* MLF_OK or MLF_DAMAGED (free too) */

typedef struct {
    int type;
    uint32_t first, last, from;   /* a segment's seconds · a config's */
    uint8_t codec;
    uint8_t run;                  /* a segment's time byte: 1 an even run */
    uint16_t place;               /* an even run's first record in its first second */
    const uint8_t *body;          /* a segment's body · a config's sections */
    size_t body_len;
} mlf_object;

int mlf_object_parse(const uint8_t *b, size_t len, mlf_object *o);

/* ------------------------------------------------------------------ a file in memory */

typedef struct {
    uint8_t *image;
    size_t size, end, entries, pad;    /* pad: the header's whole sectors */
    uint32_t last;
    mlf_sections in_force;
} mlf_writer;

int mlf_writer_open(mlf_writer *w, const mlf_header *h);
int mlf_writer_append(mlf_writer *w, const uint8_t *obj, size_t len, uint8_t type, uint32_t second);
int mlf_writer_config(mlf_writer *w, uint32_t from_second, const uint8_t *sections, size_t slen);
void mlf_writer_close(mlf_writer *w);

typedef struct {
    const uint8_t *image;
    size_t size, entries, pad;
    mlf_header h;
} mlf_reader;

/* The header's whole sectors. */
size_t mlf_padded(size_t length);

int mlf_reader_open(mlf_reader *r, const uint8_t *image, size_t len);
int mlf_reader_entry(const mlf_reader *r, size_t i, mlf_entry *e);
size_t mlf_reader_find(const mlf_reader *r, uint32_t second);
/* The object entry i points at; *buf the decoded bytes it lies in, malloc'd. */
int mlf_reader_object(const mlf_reader *r, size_t i, uint8_t **buf, mlf_object *o);

#endif
