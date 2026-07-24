#include <pebble.h>
#include "ui.h"
#include "settings.h"
#include "scheduler.h"

static void on_settings(bool seed_changed) {
  scheduler_apply_cadence();
  if (seed_changed) scheduler_reseed();
  face_poke();
}

int main(void) {
  settings_init(on_settings);
  bool had_save = game_init();
  scheduler_boot(had_save);
  face_init();
  if (scheduler_take_summary()) face_show_summary();
  app_event_loop();
  scheduler_save();
  game_save();
  face_deinit();
}
