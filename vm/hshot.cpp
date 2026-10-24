// Saves the whole Haiku screen as a binary PPM: hshot <file.ppm>
#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <stdio.h>

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: hshot file.ppm\n"); return 1; }
  BApplication app("application/x-vnd.bluevisor-hshot");
  BScreen screen;
  BBitmap* bitmap = nullptr;
  if (screen.GetBitmap(&bitmap, false) != B_OK || !bitmap) { fprintf(stderr, "GetBitmap failed\n"); return 1; }
  BRect r = bitmap->Bounds();
  int w = r.IntegerWidth() + 1, h = r.IntegerHeight() + 1;
  FILE* f = fopen(argv[1], "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  const uint8* bits = (const uint8*)bitmap->Bits();
  int bpr = bitmap->BytesPerRow();
  for (int y = 0; y < h; y++) {
    const uint8* row = bits + y * bpr;
    for (int x = 0; x < w; x++) {  // B_RGB32 is BGRA in memory
      uint8 px[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4]};
      fwrite(px, 1, 3, f);
    }
  }
  fclose(f);
  return 0;
}
