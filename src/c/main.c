#include <pebble.h>
#include "ui.h"
#include "settings.h"
#include "scheduler.h"
#include "stars.h"
#include "walk.h"

static void on_settings(bool resync) {
  if (resync) scheduler_resync();
  face_poke();
}

int main(void) {
  srand(time(NULL));
  settings_init(on_settings);
  if (!stars_load()) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "catalog failed to load");
    return 1;
  }
  scheduler_boot();
  face_init();
  if (scheduler_take_summary()) face_show_summary();
  app_event_loop();
  walk_save();
  face_deinit();
}
