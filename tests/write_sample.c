/* NIC-SDK — a sample MUF written by the C writer, for tests/test_muf.py to read back:
 * a profile of a flight recorder's shape, 60 s at 1 kHz, a pause in the slow stream, events. */
#include <stdio.h>
#include <string.h>
#include "../muf/muf.h"

static int to_file(void *ctx, const uint8_t b[MUF_BLOCK])
{
	return fwrite(b, MUF_BLOCK, 1, ctx) == 1 ? 0 : -1;
}

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "sample.muf", "wb");
	static struct muf_writer w;
	const struct muf_head h = { 'X', 7, 3, 0x1122334455667788ull, { 0, 6, 0 }, 1,
				    1763500000000000ull, 4, "sample" };
	const struct muf_stream st[] = {
		{ 0x10, 1000000, 17, 28, 0 }, { 0x11, 10000, 80, 6, 0 }, { 0x13, 0, 96, 5, 1 },
		{ 0x05, 1000, 28, 17, 0 }, { 0x06, 0, 1, 48, 0 },
	};
	const struct muf_group g[] = {
		{ 0x10, 0, 3, 'h', 0, -6, 488, 0, "ACC", "g", 0, 16 },
		{ 0x10, 3, 3, 'h', 0, -6, 10417, 0, "ACC_HG", "g", 1, 320 },
		{ 0x10, 6, 3, 'h', 0, -3, 70, 0, "GYR", "dps", 0, 4000 },
		{ 0x10, 9, 3, 'h', 0, -9, 1, 0, "MAG", "T", 0, 800 },
		{ 0x10, 12, 1, 'i', 0, -6, 15625, 0, "PRESS", "Pa", 0, 0 },
		{ 0x11, 0, 1, 'H', 0, -2, 1, 0, "HUM", "%RH", 0, 100 },
		{ 0x11, 1, 1, 'h', 0, -2, 1, 0, "TEMP", "degC", 0, 125 },
		{ 0x11, 2, 1, 'h', 0, -2, 1, 0, "PTEMP", "degC", 0, 85 },
		{ 0x13, 0, 1, 'I', 0, 0, 1, 0, "T_STEP", "", 0, 0 },
		{ 0x13, 1, 1, 'B', 0, 0, 1, 0, "EVENT", "", 0, 0 },
		{ 0x05, 0, 1, 'h', 0, -2, 1, 0, "IMU_T", "degC", 0, 0 },
		{ 0x05, 1, 1, 'H', 0, -3, 1, 0, "CELL", "V", 0, 0 },
		{ 0x05, 2, 1, 'b', 0, 0, 1, 0, "RSSI", "dBm", 0, 0 },
		{ 0x05, 3, 1, 'I', 0, 0, 1, 0, "TIME_UTC", "s", 0, 0 },
		{ 0x05, 4, 1, 'I', 0, 0, 1, 0, "TIME_STP", "", 0, 0 },
		{ 0x05, 5, 1, 'i', 0, -9, 1, 0, "TIME_NS", "s", 0, 0 },
		{ 0x06, 0, 3, 'i', 0, -9, 1, 0, "MAG_OFF", "T", 0, 0 },
		{ 0x06, 3, 9, 'i', 0, -6, 1, 0, "MAG_MAT", "", 0, 0 },
	};
	uint8_t c[48] = { 0 }, s[28], sl[6], ev[5], in[17];

	for (int k = 0; k < 12; k++) {
		p32(c + 4 * k, (uint32_t)(k * 1000 - 3000));
	}
	if (muf_begin(&w, to_file, f, &h, st, 5, g, 18, "kind;name;motor\nrocket;\"Test; one\";C6-3\n", c)) {
		return 1;
	}
	for (uint32_t n = 0; n < 60000; n++) {
		for (int k = 0; k < 12; k++) {
			p16(s + 2 * k, (uint16_t)(int16_t)((n + k) % 2000 - 1000));
		}
		p32(s + 24, 101325u * 64u - n);
		muf_step(&w, 0, s);
		if (n % 100 == 0 && !(n >= 20000 && n < 30000)) {      /* 10 Hz, a pause of 10 s */
			p16(sl, (uint16_t)(5000 + n / 100));
			p16(sl + 2, (uint16_t)(2000 - n / 100));
			p16(sl + 4, 2500);
			muf_step(&w, 1, sl);
		}
		if (n == 30000) {
			muf_skip(&w, 1, 0);
		}
		if (n == 20000) {
			muf_skip(&w, 1, 100);                           /* the slow stream's 100 steps paused */
		}
		if (n % 1000 == 0) {
			p16(in, 2400);
			p16(in + 2, 3300);
			in[4] = (uint8_t)-80;
			p32(in + 5, 1763500000u + n / 1000);
			p32(in + 9, n);
			p32(in + 13, 0);
			muf_step(&w, 3, in);
		}
		if (n == 3000 || n == 7500 || n == 37930) {
			p32(ev, n);
			ev[4] = n == 3000 ? 2 : n == 7500 ? 3 : 4;
			muf_step(&w, 2, ev);
		}
	}
	muf_end(&w, 16);
	fclose(f);
	return w.error;
}
