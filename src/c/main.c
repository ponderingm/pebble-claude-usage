#include <pebble.h>

#define THRESHOLD_WARN_PCT 70
#define THRESHOLD_DANGER_PCT 90
#define RING_DIAMETER 80
#define RING_THICKNESS 10
#define RING_GAP 20
#define RING_TOP_OFFSET 30

enum {
  PersistKeyFiveHourPct = 100,
  PersistKeyFiveHourReset = 101,
  PersistKeySevenDayPct = 102,
  PersistKeySevenDayReset = 103,
};

static Window *s_window;
static TextLayer *s_time_layer;
static Layer *s_canvas_layer;

static int s_five_hour_pct = -1;
static int s_seven_day_pct = -1;
static time_t s_five_hour_reset = 0;
static time_t s_seven_day_reset = 0;

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);
}

static void format_countdown(time_t reset_epoch, char *buf, size_t buf_len) {
  if (reset_epoch <= 0) {
    snprintf(buf, buf_len, "--");
    return;
  }
  time_t now = time(NULL);
  int diff = (int)difftime(reset_epoch, now);
  if (diff <= 0) {
    snprintf(buf, buf_len, "reset");
    return;
  }
  int days = diff / 86400;
  int hours = (diff % 86400) / 3600;
  int mins = (diff % 3600) / 60;
  if (days > 0) {
    snprintf(buf, buf_len, "%dd %dh", days, hours);
  } else if (hours > 0) {
    snprintf(buf, buf_len, "%dh %dm", hours, mins);
  } else {
    snprintf(buf, buf_len, "%dm", mins);
  }
}

static GColor color_for_pct(int pct) {
  if (pct >= THRESHOLD_DANGER_PCT) {
    return GColorRed;
  } else if (pct >= THRESHOLD_WARN_PCT) {
    return GColorOrange;
  }
  return GColorGreen;
}

static void draw_metric(GContext *ctx, GRect ring_rect, const char *label,
                         int pct, time_t reset_epoch) {
  GRect label_rect = GRect(ring_rect.origin.x - 10, ring_rect.origin.y - 22,
                            ring_rect.size.w + 20, 20);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                      label_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle, RING_THICKNESS,
                        DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(360));

  int clamped_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  if (pct >= 0) {
    graphics_context_set_fill_color(ctx, color_for_pct(pct));
    graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle, RING_THICKNESS,
                          DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(360 * clamped_pct / 100));
  }

  char pct_buf[8];
  if (pct < 0) {
    snprintf(pct_buf, sizeof(pct_buf), "--");
  } else {
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", clamped_pct);
  }
  GRect pct_rect = GRect(ring_rect.origin.x, ring_rect.origin.y + (ring_rect.size.h / 2) - 12,
                          ring_rect.size.w, 24);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, pct_buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                      pct_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  char reset_buf[16];
  format_countdown(reset_epoch, reset_buf, sizeof(reset_buf));
  GRect reset_rect = GRect(ring_rect.origin.x - 10, ring_rect.origin.y + ring_rect.size.h + 4,
                            ring_rect.size.w + 20, 20);
  graphics_draw_text(ctx, reset_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                      reset_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int total_w = RING_DIAMETER * 2 + RING_GAP;
  int start_x = (bounds.size.w - total_w) / 2;

  GRect five_hour_rect = GRect(start_x, RING_TOP_OFFSET, RING_DIAMETER, RING_DIAMETER);
  GRect seven_day_rect = GRect(start_x + RING_DIAMETER + RING_GAP, RING_TOP_OFFSET,
                                RING_DIAMETER, RING_DIAMETER);

  draw_metric(ctx, five_hour_rect, "5H", s_five_hour_pct, s_five_hour_reset);
  draw_metric(ctx, seven_day_rect, "7D", s_seven_day_pct, s_seven_day_reset);
}

static void request_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
  app_message_outbox_send();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_update();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  layer_mark_dirty(s_canvas_layer);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *five_hour_pct_tuple = dict_find(iterator, MESSAGE_KEY_FIVE_HOUR_PCT);
  Tuple *five_hour_reset_tuple = dict_find(iterator, MESSAGE_KEY_FIVE_HOUR_RESET_EPOCH);
  Tuple *seven_day_pct_tuple = dict_find(iterator, MESSAGE_KEY_SEVEN_DAY_PCT);
  Tuple *seven_day_reset_tuple = dict_find(iterator, MESSAGE_KEY_SEVEN_DAY_RESET_EPOCH);

  if (five_hour_pct_tuple) {
    s_five_hour_pct = (int)five_hour_pct_tuple->value->int32;
    persist_write_int(PersistKeyFiveHourPct, s_five_hour_pct);
  }
  if (five_hour_reset_tuple) {
    s_five_hour_reset = (time_t)five_hour_reset_tuple->value->int32;
    persist_write_int(PersistKeyFiveHourReset, (int32_t)s_five_hour_reset);
  }
  if (seven_day_pct_tuple) {
    s_seven_day_pct = (int)seven_day_pct_tuple->value->int32;
    persist_write_int(PersistKeySevenDayPct, s_seven_day_pct);
  }
  if (seven_day_reset_tuple) {
    s_seven_day_reset = (time_t)seven_day_reset_tuple->value->int32;
    persist_write_int(PersistKeySevenDayReset, (int32_t)s_seven_day_reset);
  }

  layer_mark_dirty(s_canvas_layer);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage inbox dropped, reason %d", reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage outbox failed, reason %d", reason);
}

static void load_persisted_values(void) {
  if (persist_exists(PersistKeyFiveHourPct)) {
    s_five_hour_pct = persist_read_int(PersistKeyFiveHourPct);
  }
  if (persist_exists(PersistKeyFiveHourReset)) {
    s_five_hour_reset = (time_t)persist_read_int(PersistKeyFiveHourReset);
  }
  if (persist_exists(PersistKeySevenDayPct)) {
    s_seven_day_pct = persist_read_int(PersistKeySevenDayPct);
  }
  if (persist_exists(PersistKeySevenDayReset)) {
    s_seven_day_reset = (time_t)persist_read_int(PersistKeySevenDayReset);
  }
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_time_layer = text_layer_create(GRect(0, 5, bounds.size.w, 50));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  s_canvas_layer = layer_create(GRect(0, 60, bounds.size.w, bounds.size.h - 60));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  update_time();
}

static void window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  layer_destroy(s_canvas_layer);
}

static void init(void) {
  load_persisted_values();

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
