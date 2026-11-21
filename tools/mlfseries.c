/* NIC-SDK — one channel's series out of MLF files, through the program's glue; see mlfseries.h. */
#include "mlfseries.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct acc {
	struct mlf_sample *v;
	size_t n, cap;
	uint32_t order;
};

static void emit(void *ctx, int64_t index, int32_t raw, uint8_t quality)
{
	struct acc *a = ctx;

	if (a->n == a->cap) {
		a->cap = a->cap ? 2 * a->cap : 4096;
		a->v = realloc(a->v, a->cap * sizeof(*a->v));
	}
	a->v[a->n++] = (struct mlf_sample){ index, raw, quality, a->order++ };
}

static int by_index(const void *x, const void *y)
{
	const struct mlf_sample *a = x, *b = y;

	return a->index != b->index ? (a->index < b->index ? -1 : 1) : (a->order < b->order ? -1 : 1);
}

static uint8_t *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *b = NULL;
	long n;

	if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) ||
	    !(b = malloc((size_t)n ? (size_t)n : 1)) || fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		b = NULL;
	}
	if (f) {
		fclose(f);
	}
	*len = b ? (size_t)n : 0;
	return b;
}

int mlf_series(char *const *paths, int n, const char *name, struct mlf_channel *ch, struct mlf_station *st,
	       struct mlf_sample **out, size_t *count)
{
	struct acc a = { 0 };
	int have = 0;

	memset(st, 0, sizeof(*st));
	for (int p = 0; p < n; p++) {
		size_t len;
		uint8_t *img = slurp(paths[p], &len);
		mlf_reader r;
		mlf_sections hs, in_force;

		memset(&in_force, 0, sizeof(in_force));
		if (!img || mlf_reader_open(&r, img, len) != MLF_OK || r.h.letter != mlf_glue.letter ||
		    mlf_sections_parse(r.h.sections, r.h.sections_len, &hs) != MLF_OK ||
		    mlf_sections_apply(&in_force, &hs) != MLF_OK) {
			fprintf(stderr, "%s: not an MLF file of the profile %c\n", paths[p], mlf_glue.letter);
			free(img);
			free(a.v);
			return -1;
		}
		for (size_t i = 0; i < r.entries; i++) {
			uint8_t *buf;
			mlf_object o;
			if (mlf_reader_object(&r, i, &buf, &o) != MLF_OK) {
				continue;               /* a damaged object costs itself */
			}
			if (o.type == MLF_CFG) {
				mlf_sections cs;
				if (mlf_sections_parse(o.body, o.body_len, &cs) == MLF_OK) {
					mlf_sections_apply(&in_force, &cs);
				}
			} else if (o.type == MLF_SEG) {
				if (!have && (memset(ch, 0, sizeof(*ch)), mlf_glue.channel(&in_force, name, ch) == 0)) {
					have = 1;
					if (mlf_glue.station(&in_force, st) != 0) {
						memset(st, 0, sizeof(*st));
					}
				}
				size_t before = a.n;
				if (have && mlf_glue.samples(&in_force, &o, name, emit, &a) != 0) {
					a.n = before;   /* a segment not read costs itself */
				}
			}
			free(buf);
		}
		mlf_sections_free(&in_force);
		free(img);
	}
	if (!have) {
		fprintf(stderr, "no channel %s\n", name);
		free(a.v);
		return -1;
	}
	qsort(a.v, a.n, sizeof(*a.v), by_index);
	size_t k = 0;
	for (size_t i = 0; i < a.n; i++) {      /* of one index the later written */
		if (k && a.v[k - 1].index == a.v[i].index) {
			a.v[k - 1] = a.v[i];
		} else {
			a.v[k++] = a.v[i];
		}
	}
	*out = a.v;
	*count = k;
	return 0;
}
