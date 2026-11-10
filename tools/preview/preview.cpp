// Draws the screens on this computer with the board's own drawing code, as PPM files, for checking by eye.
//   g++ -std=gnu++17 -O2 -Ilib/screen -Ilib/profile tools/preview/preview.cpp lib/screen/*.cpp lib/profile/profile.cpp -o preview/out/preview
//   preview/out/preview <dir> stills | <dir> frames <scene> <rrggbb> <count>
// tools/preview.py runs it and turns the files into PNG sheets and GIFs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "screen.h"

using namespace cheerlights;

static uint16_t pixels[240 * 135];

static void save(const Canvas& c, const char* dir, const char* name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
  for (int y = 0; y < c.height(); y++) {
    for (int x = 0; x < c.width(); x++) {
      const Rgb p = c.get(x, y);
      fputc(p.r, f);
      fputc(p.g, f);
      fputc(p.b, f);
    }
  }
  fclose(f);
}

static View showing(Rgb color) {
  View v{};
  v.status = Status::Showing;
  v.color = v.from = color;
  v.fade = 1;
  v.setupUrl = "cheerlights.com/tdisplay";
  v.ssid = "Scharler Home";
  return v;
}

int main(int argc, char** argv) {
  if (argc < 3) return 1;
  const char* dir = argv[1];
  Canvas c(pixels, 240, 135);
  SceneState scene{};
  if (strcmp(argv[2], "stills") == 0) {
    for (int s = 0; s < SCENE_COUNT; s++) {
      for (size_t i = 0; i < PALETTE_SIZE; i++) {
        sceneStart(scene, static_cast<SceneId>(s), 240, 135, 12345);
        for (int k = 0; k < 90; k++) sceneStep(scene, 1 / 30.0f);
        drawView(c, showing(PALETTE[i].rgb), scene);
        char name[64];
        snprintf(name, sizeof(name), "%s-%02d-%s", sceneName(static_cast<SceneId>(s)), static_cast<int>(i),
                 PALETTE[i].name);
        save(c, dir, name);
      }
    }
    sceneStart(scene, SceneId::Solid, 240, 135, 1);
    View v = showing({0x12, 0xab, 0xef});
    drawView(c, v, scene);
    save(c, dir, "state-hex");
    v = showing({0x80, 0x00, 0x80});
    v.offline = true;
    drawView(c, v, scene);
    save(c, dir, "state-offline");
    v.offline = false;
    v.from = {0x00, 0x80, 0x00};
    v.fade = 0.3f;
    drawView(c, v, scene);
    save(c, dir, "state-fade-30");
    v.fade = 0.75f;
    drawView(c, v, scene);
    save(c, dir, "state-fade-75");
    const Status statuses[] = {Status::NoWifi,   Status::Scanning,    Status::Joining,
                               Status::JoinFailed, Status::NoneInRange, Status::Connecting};
    const char* names[] = {"state-nowifi",     "state-scanning",    "state-joining",
                           "state-joinfailed", "state-noneinrange", "state-connecting"};
    for (int i = 0; i < 6; i++) {
      View w = showing({0, 0, 0});
      w.status = statuses[i];
      w.saved = 3;
      drawView(c, w, scene);
      save(c, dir, names[i]);
    }
    View info = showing({0x80, 0x00, 0x80});
    info.info = true;
    info.infoLines[0] = "Color\tpurple";
    info.infoLines[1] = "Wi-Fi\tScharler Home";
    info.infoLines[2] = "Address\t192.168.1.42";
    info.infoLines[3] = "Firmware\t0.1.0";
    drawView(c, info, scene);
    save(c, dir, "state-info");
    return 0;
  }
  if (strcmp(argv[2], "frames") == 0 && argc == 6) {
    SceneId id;
    Rgb color;
    if (!sceneFromName(argv[3], id) || !parseHex(argv[4], strlen(argv[4]), color)) return 1;
    sceneStart(scene, id, 240, 135, 777);
    const int count = atoi(argv[5]);
    for (int k = 0; k < count; k++) {
      sceneStep(scene, 1 / 25.0f);
      drawView(c, showing(color), scene);
      char name[32];
      snprintf(name, sizeof(name), "frame-%04d", k);
      save(c, dir, name);
    }
    return 0;
  }
  return 1;
}
