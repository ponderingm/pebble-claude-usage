#include <pebble.h>

#define THRESHOLD_WARN_PCT 70
#define THRESHOLD_DANGER_PCT 90

#define SCREEN_W 200
#define SCREEN_H 228
#define FRAME_THICKNESS 9
#define FRAME_SIDE_THICKNESS 3
#define BUS_LONG_WAIT_THRESHOLD_MIN 60
#define BATTERY_LOW_PCT 20

#define WEATHER_TEMP_UNSET -1000

#define BUS_LABEL_HOME_TO_WORK "Kiba"
#define BUS_LABEL_WORK_TO_HOME "Honjo"

// Region layout, top to bottom: usage bar (bare color strip, no text) ->
// Header (weather | bus) -> Body (date + time) -> Footer (5H | 7D detail)
// -> Status (battery / Bluetooth, icon-only) -> usage bar.
#define HEADER_Y (FRAME_THICKNESS + 8)
#define DIVIDER_1_Y (FRAME_THICKNESS + 44)
#define DATE_LAYER_Y (FRAME_THICKNESS + 50)
#define TIME_LAYER_Y (FRAME_THICKNESS + 70)
#define DIVIDER_2_Y (FRAME_THICKNESS + 134)
#define FOOTER_Y (FRAME_THICKNESS + 140)
#define STATUS_Y (FRAME_THICKNESS + 178)

// Dark Slate theme: near-black background, white text, status-color accents kept as-is.
#define THEME_BG_COLOR GColorBlack
#define THEME_TEXT_COLOR GColorWhite
#define THEME_BORDER_COLOR GColorDarkGray
#define THEME_DIVIDER_COLOR GColorLightGray
#define THEME_ALERT_COLOR GColorRed

enum {
  PersistKeyFiveHourPct = 100,
  PersistKeyFiveHourReset = 101,
  PersistKeySevenDayPct = 102,
  PersistKeySevenDayReset = 103,
  PersistKeyWeatherTempC = 104,
  PersistKeyWeatherShapeId = 105,
  PersistKeyBusDirection = 106,
  PersistKeyBusNextMin = 107,
};

enum {
  WeatherShapeSun = 0,
  WeatherShapeCloudSun = 1,
  WeatherShapeCloud = 2,
  WeatherShapeFog = 3,
  WeatherShapeRain = 4,
  WeatherShapeSnow = 5,
  WeatherShapeStorm = 6,
  WeatherShapeUnknown = 7,
};

enum {
  BusDirectionHomeToWork = 0,
  BusDirectionWorkToHome = 1,
};

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static Layer *s_canvas_layer;

static int s_five_hour_pct = -1;
static int s_seven_day_pct = -1;
static time_t s_five_hour_reset = 0;
static time_t s_seven_day_reset = 0;

static int s_battery_pct = 100;
static bool s_bluetooth_connected = true;

static int s_weather_temp_c = WEATHER_TEMP_UNSET;
static int s_weather_shape_id = WeatherShapeUnknown;

static int s_bus_direction = -1;
static int s_bus_next_min = -1;

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), "%H:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  static char s_date_buffer[16];
  strftime(s_date_buffer, sizeof(s_date_buffer), "%m-%d (%a)", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
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

static void draw_usage_bar(GContext *ctx, int pct, bool top) {
  int y = top ? 0 : (SCREEN_H - FRAME_THICKNESS);
  GRect track_rect = GRect(0, y, SCREEN_W, FRAME_THICKNESS);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, track_rect, 0, GCornerNone);

  if (pct < 0) {
    return;
  }
  int clamped_pct = pct > 100 ? 100 : pct;
  int fill_w = (SCREEN_W * clamped_pct) / 100;
  GRect fill_rect = GRect(0, y, fill_w, FRAME_THICKNESS);
  graphics_context_set_fill_color(ctx, color_for_pct(pct));
  graphics_fill_rect(ctx, fill_rect, 0, GCornerNone);
}

static void draw_side_borders(GContext *ctx) {
  graphics_context_set_fill_color(ctx, THEME_BORDER_COLOR);
  graphics_fill_rect(ctx,
                      GRect(0, FRAME_THICKNESS, FRAME_SIDE_THICKNESS, SCREEN_H - 2 * FRAME_THICKNESS),
                      0, GCornerNone);
  graphics_fill_rect(ctx,
                      GRect(SCREEN_W - FRAME_SIDE_THICKNESS, FRAME_THICKNESS,
                            FRAME_SIDE_THICKNESS, SCREEN_H - 2 * FRAME_THICKNESS),
                      0, GCornerNone);
}

static void draw_divider(GContext *ctx, int y) {
  graphics_context_set_stroke_color(ctx, THEME_DIVIDER_COLOR);
  graphics_draw_line(ctx, GPoint(FRAME_SIDE_THICKNESS + 6, y),
                      GPoint(SCREEN_W - FRAME_SIDE_THICKNESS - 6, y));
}

static void draw_battery_icon(GContext *ctx, GPoint top_left, int pct) {
  int body_w = 20;
  int body_h = 10;
  GRect body_rect = GRect(top_left.x, top_left.y, body_w, body_h);
  graphics_context_set_stroke_color(ctx, THEME_TEXT_COLOR);
  graphics_draw_rect(ctx, body_rect);
  graphics_context_set_fill_color(ctx, THEME_TEXT_COLOR);
  graphics_fill_rect(ctx, GRect(top_left.x + body_w, top_left.y + 3, 2, 4), 0, GCornerNone);

  int clamped_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  int fill_w = ((body_w - 4) * clamped_pct) / 100;
  graphics_context_set_fill_color(ctx, pct <= BATTERY_LOW_PCT ? THEME_ALERT_COLOR : THEME_TEXT_COLOR);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y + 2, fill_w, body_h - 4), 0, GCornerNone);
}

static void draw_bt_disconnected_icon(GContext *ctx, GPoint center) {
  graphics_context_set_stroke_color(ctx, THEME_ALERT_COLOR);
  graphics_draw_circle(ctx, center, 7);
  graphics_draw_line(ctx, GPoint(center.x - 4, center.y - 4), GPoint(center.x + 4, center.y + 4));
  graphics_draw_line(ctx, GPoint(center.x - 4, center.y + 4), GPoint(center.x + 4, center.y - 4));
}

static void draw_bus_icon(GContext *ctx, GPoint center) {
  GRect body_rect = GRect(center.x - 9, center.y - 6, 18, 10);
  graphics_context_set_fill_color(ctx, THEME_TEXT_COLOR);
  graphics_fill_rect(ctx, body_rect, 2, GCornersTop);
  graphics_context_set_fill_color(ctx, THEME_BG_COLOR);
  graphics_fill_circle(ctx, GPoint(center.x - 4, center.y + 4), 2);
  graphics_fill_circle(ctx, GPoint(center.x + 4, center.y + 4), 2);
}

static void draw_status_row(GContext *ctx, int y) {
  // Icon-only status row: battery is centered alone when Bluetooth is
  // connected (nothing to pair it with), otherwise the two icons sit
  // side by side.
  int battery_x = s_bluetooth_connected ? SCREEN_W / 2 - 10 : SCREEN_W / 2 - 32;
  draw_battery_icon(ctx, GPoint(battery_x, y), s_battery_pct);

  if (!s_bluetooth_connected) {
    draw_bt_disconnected_icon(ctx, GPoint(SCREEN_W / 2 + 24, y + 5));
  }
}

static void draw_footer_band(GContext *ctx, int y) {
  char five_pct_buf[16];
  char seven_pct_buf[16];
  if (s_five_hour_pct < 0) {
    snprintf(five_pct_buf, sizeof(five_pct_buf), "5H --");
  } else {
    snprintf(five_pct_buf, sizeof(five_pct_buf), "5H %d%%", s_five_hour_pct);
  }
  if (s_seven_day_pct < 0) {
    snprintf(seven_pct_buf, sizeof(seven_pct_buf), "7D --");
  } else {
    snprintf(seven_pct_buf, sizeof(seven_pct_buf), "7D %d%%", s_seven_day_pct);
  }

  char five_reset_buf[16];
  char seven_reset_buf[16];
  format_countdown(s_five_hour_reset, five_reset_buf, sizeof(five_reset_buf));
  format_countdown(s_seven_day_reset, seven_reset_buf, sizeof(seven_reset_buf));

  int half_w = SCREEN_W / 2;
  GRect five_pct_rect = GRect(FRAME_SIDE_THICKNESS + 6, y, half_w - FRAME_SIDE_THICKNESS - 10, 16);
  GRect seven_pct_rect = GRect(half_w + 4, y, half_w - FRAME_SIDE_THICKNESS - 10, 16);
  graphics_context_set_text_color(ctx, THEME_TEXT_COLOR);
  graphics_draw_text(ctx, five_pct_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                      five_pct_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, seven_pct_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                      seven_pct_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  GRect five_reset_rect = GRect(FRAME_SIDE_THICKNESS + 6, y + 15, half_w - FRAME_SIDE_THICKNESS - 10, 14);
  GRect seven_reset_rect = GRect(half_w + 4, y + 15, half_w - FRAME_SIDE_THICKNESS - 10, 14);
  graphics_draw_text(ctx, five_reset_buf, fonts_get_system_font(FONT_KEY_GOTHIC_09),
                      five_reset_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, seven_reset_buf, fonts_get_system_font(FONT_KEY_GOTHIC_09),
                      seven_reset_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void draw_weather_shape(GContext *ctx, GPoint center, int shape_id) {
  switch (shape_id) {
    case WeatherShapeSun:
      graphics_context_set_fill_color(ctx, GColorOrange);
      graphics_fill_circle(ctx, center, 10);
      break;
    case WeatherShapeCloudSun:
      graphics_context_set_fill_color(ctx, GColorOrange);
      graphics_fill_circle(ctx, GPoint(center.x - 4, center.y - 2), 7);
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, GPoint(center.x + 4, center.y + 3), 8);
      break;
    case WeatherShapeCloud:
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, GPoint(center.x - 5, center.y + 2), 7);
      graphics_fill_circle(ctx, GPoint(center.x + 5, center.y + 2), 7);
      graphics_fill_circle(ctx, GPoint(center.x, center.y - 3), 8);
      break;
    case WeatherShapeFog:
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      for (int i = -1; i <= 1; i++) {
        graphics_draw_line(ctx, GPoint(center.x - 10, center.y + i * 5),
                            GPoint(center.x + 10, center.y + i * 5));
      }
      break;
    case WeatherShapeRain:
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, center, 8);
      graphics_context_set_stroke_color(ctx, GColorBlue);
      for (int i = -1; i <= 1; i++) {
        graphics_draw_line(ctx, GPoint(center.x + i * 6, center.y + 6),
                            GPoint(center.x + i * 6 - 2, center.y + 12));
      }
      break;
    case WeatherShapeSnow:
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, center, 8);
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_draw_circle(ctx, center, 8);
      break;
    case WeatherShapeStorm:
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, center, 8);
      graphics_context_set_fill_color(ctx, GColorYellow);
      graphics_fill_circle(ctx, GPoint(center.x, center.y + 8), 3);
      break;
    default:
      break;
  }
}

static void format_bus_wait(int minutes, char *buf, size_t buf_len) {
  if (minutes < BUS_LONG_WAIT_THRESHOLD_MIN) {
    snprintf(buf, buf_len, "%dmin", minutes);
    return;
  }
  int hours = minutes / 60;
  int mins = minutes % 60;
  snprintf(buf, buf_len, "%dh%02dm", hours, mins);
}

static void draw_header_band(GContext *ctx, int y) {
  bool has_weather = (s_weather_temp_c != WEATHER_TEMP_UNSET);
  bool has_bus = (s_bus_next_min >= 0);

  // Center whichever block is showing when the other one is absent,
  // instead of leaving half the band empty.
  int weather_x = has_bus ? FRAME_SIDE_THICKNESS + 16 : SCREEN_W / 2 - 28;
  int bus_x = has_weather ? SCREEN_W / 2 + 24 : SCREEN_W / 2 - 10;

  if (has_weather) {
    draw_weather_shape(ctx, GPoint(weather_x, y + 8), s_weather_shape_id);

    char temp_buf[8];
    snprintf(temp_buf, sizeof(temp_buf), "%d°C", s_weather_temp_c);
    GRect temp_rect = GRect(weather_x + 16, y, 56, 24);
    graphics_context_set_text_color(ctx, THEME_TEXT_COLOR);
    graphics_draw_text(ctx, temp_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                        temp_rect, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }

  if (has_bus) {
    const char *label = (s_bus_direction == BusDirectionHomeToWork)
                            ? BUS_LABEL_HOME_TO_WORK
                            : BUS_LABEL_WORK_TO_HOME;
    char wait_buf[16];
    format_bus_wait(s_bus_next_min, wait_buf, sizeof(wait_buf));
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %s", label, wait_buf);

    draw_bus_icon(ctx, GPoint(bus_x, y + 8));
    GRect rect = GRect(bus_x + 12, y, 60, 20);
    graphics_context_set_text_color(ctx, THEME_TEXT_COLOR);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                        rect, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  draw_usage_bar(ctx, s_five_hour_pct, true);
  draw_usage_bar(ctx, s_seven_day_pct, false);
  draw_side_borders(ctx);

  draw_header_band(ctx, HEADER_Y);
  draw_divider(ctx, DIVIDER_1_Y);
  draw_divider(ctx, DIVIDER_2_Y);
  draw_footer_band(ctx, FOOTER_Y);
  draw_status_row(ctx, STATUS_Y);
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

static void battery_callback(BatteryChargeState charge_state) {
  s_battery_pct = charge_state.charge_percent;
  layer_mark_dirty(s_canvas_layer);
}

static void bluetooth_callback(bool connected) {
  s_bluetooth_connected = connected;
  layer_mark_dirty(s_canvas_layer);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *five_hour_pct_tuple = dict_find(iterator, MESSAGE_KEY_FIVE_HOUR_PCT);
  Tuple *five_hour_reset_tuple = dict_find(iterator, MESSAGE_KEY_FIVE_HOUR_RESET_EPOCH);
  Tuple *seven_day_pct_tuple = dict_find(iterator, MESSAGE_KEY_SEVEN_DAY_PCT);
  Tuple *seven_day_reset_tuple = dict_find(iterator, MESSAGE_KEY_SEVEN_DAY_RESET_EPOCH);
  Tuple *weather_temp_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_TEMP_C);
  Tuple *weather_shape_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_CODE);
  Tuple *bus_direction_tuple = dict_find(iterator, MESSAGE_KEY_BUS_DIRECTION);
  Tuple *bus_next_min_tuple = dict_find(iterator, MESSAGE_KEY_BUS_NEXT_MIN);

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
  if (weather_temp_tuple) {
    s_weather_temp_c = (int)weather_temp_tuple->value->int32;
    persist_write_int(PersistKeyWeatherTempC, s_weather_temp_c);
  }
  if (weather_shape_tuple) {
    s_weather_shape_id = (int)weather_shape_tuple->value->int32;
    persist_write_int(PersistKeyWeatherShapeId, s_weather_shape_id);
  }
  if (bus_direction_tuple) {
    s_bus_direction = (int)bus_direction_tuple->value->int32;
    persist_write_int(PersistKeyBusDirection, s_bus_direction);
  }
  if (bus_next_min_tuple) {
    s_bus_next_min = (int)bus_next_min_tuple->value->int32;
    persist_write_int(PersistKeyBusNextMin, s_bus_next_min);
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
  if (persist_exists(PersistKeyWeatherTempC)) {
    s_weather_temp_c = persist_read_int(PersistKeyWeatherTempC);
  }
  if (persist_exists(PersistKeyWeatherShapeId)) {
    s_weather_shape_id = persist_read_int(PersistKeyWeatherShapeId);
  }
  if (persist_exists(PersistKeyBusDirection)) {
    s_bus_direction = persist_read_int(PersistKeyBusDirection);
  }
  if (persist_exists(PersistKeyBusNextMin)) {
    s_bus_next_min = persist_read_int(PersistKeyBusNextMin);
  }
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);

  s_date_layer = text_layer_create(GRect(0, DATE_LAYER_Y, SCREEN_W, 20));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, THEME_TEXT_COLOR);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  s_time_layer = text_layer_create(GRect(0, TIME_LAYER_Y, SCREEN_W, 60));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, THEME_TEXT_COLOR);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  s_canvas_layer = layer_create(GRect(0, 0, SCREEN_W, SCREEN_H));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  s_battery_pct = battery_state_service_peek().charge_percent;
  battery_state_service_subscribe(battery_callback);

  s_bluetooth_connected = connection_service_peek_pebble_app_connection();
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bluetooth_callback,
  });

  update_time();
}

static void window_unload(Window *window) {
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  layer_destroy(s_canvas_layer);
}

static void init(void) {
  load_persisted_values();

  s_window = window_create();
  window_set_background_color(s_window, THEME_BG_COLOR);
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
