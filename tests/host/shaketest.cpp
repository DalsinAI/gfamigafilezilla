#include "crypt/util.hpp"
#include <stdio.h>
#include <string.h>
int main() {
	unsigned char msg[300]; for (int i = 0; i < 300; ++i) msg[i] = (unsigned char)(i * 13 + 5);
	size_t lens[] = {0, 1, 135, 136, 137, 167, 168, 169, 300};
	for (size_t L : lens) for (int v = 0; v < 2; ++v) {
		ShakeXOF* h = v ? shake256_xof_from_input(make_ptrlen(msg, L)) : shake128_xof_from_input(make_ptrlen(msg, L));
		unsigned char out[400]; shake_xof_read(h, out, 100); shake_xof_read(h, out + 100, 300); shake_xof_free(h);
		printf("%d %zu ", v ? 256 : 128, L); for (int i = 0; i < 400; ++i) printf("%02x", out[i]); printf("\n");
	}
}
