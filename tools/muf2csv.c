/* NIC-SDK — MUF to CSV in C: muf2csv FILE DIR — a CSV a stream with groups, as tools/muf.py --csv. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "muffile.h"

int main(int argc, char **argv)
{
	static struct muf_reader r;
	static struct muf_block b;
	FILE *out[MUF_MAX_STREAMS] = { 0 };
	const struct muf_group *order[MUF_MAX_STREAMS][MUF_MAX_GROUPS];
	unsigned n_order[MUF_MAX_STREAMS] = { 0 };

	if (argc < 3) {
		fprintf(stderr, "muf2csv FILE DIR\n");
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f || muf_open_file(&r, f) != 0) {
		fprintf(stderr, "not a MUF file\n");
		return 1;
	}
	for (unsigned s = 0; s < r.n_streams; s++) {
		/* its groups in the order of their first */
		for (unsigned g = 0; g < r.n_groups; g++) {
			if (r.groups[g].stream == r.streams[s].kind) {
				unsigned k = n_order[s]++;
				while (k && order[s][k - 1]->first > r.groups[g].first) {
					order[s][k] = order[s][k - 1];
					k--;
				}
				order[s][k] = &r.groups[g];
			}
		}
		if (!n_order[s]) {
			continue;
		}
		char path[512];
		snprintf(path, sizeof(path), "%s/stream_%02X.csv", argv[2], r.streams[s].kind);
		out[s] = fopen(path, "w");
		if (!out[s]) {
			return 1;
		}
		fprintf(out[s], "step");
		for (unsigned i = 0; i < n_order[s]; i++) {
			for (unsigned k = 0; k < order[s][i]->count; k++) {
				if (order[s][i]->count > 1) {
					fprintf(out[s], ",%s_%u", order[s][i]->name, k);
				} else {
					fprintf(out[s], ",%s", order[s][i]->name);
				}
			}
		}
		fprintf(out[s], "\r\n"); /* as Python's csv module */
	}
	while (muf_next(&r, &b) == 0) {
		int s = muf_stream_index(&r, b.kind);
		if (s < 0 || !out[s]) {
			continue;
		}
		for (unsigned st = 0; st < b.steps; st++) {
			const uint8_t *step = b.raw + MUF_STEPS_FROM + st * r.streams[s].step_bytes;
			fprintf(out[s], "%u", b.first_step + st);
			for (unsigned i = 0; i < n_order[s]; i++) {
				for (unsigned k = 0; k < order[s][i]->count; k++) {
					fprintf(out[s], ",%.17g", muf_raw(&r, order[s][i], step, k));
				}
			}
			fprintf(out[s], "\r\n");
		}
	}
	for (unsigned s = 0; s < r.n_streams; s++) {
		if (out[s]) {
			fclose(out[s]);
		}
	}
	fprintf(stderr, "%u damaged blocks skipped\n", r.blocks_bad);
	return 0;
}
