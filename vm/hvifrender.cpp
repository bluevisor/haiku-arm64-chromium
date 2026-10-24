// Render an HVIF file at a given size to a PPM: hvifrender icon.hvif 64 out.ppm
#include <Application.h>
#include <Bitmap.h>
#include <IconUtils.h>
#include <File.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
	if (argc < 4) return 1;
	BApplication app("application/x-vnd.hvifrender");  // BBitmap needs app_server
	BFile f(argv[1], B_READ_ONLY);
	off_t size; f.GetSize(&size);
	uint8* data = (uint8*)malloc(size);
	f.Read(data, size);
	int n = atoi(argv[2]);
	BBitmap bm(BRect(0, 0, n - 1, n - 1), B_RGBA32);
	memset(bm.Bits(), 0, bm.BitsLength());
	status_t s = BIconUtils::GetVectorIcon(data, size, &bm);
	if (s != B_OK) { fprintf(stderr, "GetVectorIcon: %s\n", strerror(s)); return 1; }
	FILE* o = fopen(argv[3], "wb");
	fprintf(o, "P6\n%d %d\n255\n", n, n);
	uint8* p = (uint8*)bm.Bits();
	for (int y = 0; y < n; y++) {
		uint8* row = p + y * bm.BytesPerRow();
		for (int x = 0; x < n; x++) {
			uint8 b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];
			// composite on the Deskbar grey so the edge AA is visible
			uint8 bg = 0x4a;
			uint8 rgb[3] = { (uint8)((r * a + bg * (255 - a)) / 255), (uint8)((g * a + bg * (255 - a)) / 255), (uint8)((b * a + bg * (255 - a)) / 255) };
			fwrite(rgb, 1, 3, o);
		}
	}
	fclose(o);
	return 0;
}
