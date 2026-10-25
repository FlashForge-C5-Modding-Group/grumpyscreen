#include "macro_panel.h"
#include "i18n.h"
#include "config.h"
#include "icons.h"
#include "logger.h"
#include "theme.h"
#include "state.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

using namespace Theme;

static lv_coord_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};

// FIXME - do we actually need all these icons for macros maybe we should only be supporting a subset
static const std::map<std::string, const lv_img_dsc_t *> macro_icons = {
  {"arrow_down", Icons::ARROW_DOWN}, {"arrow_left", Icons::ARROW_LEFT},
  {"arrow_right", Icons::ARROW_RIGHT}, {"arrow_up", Icons::ARROW_UP},
  {"back", Icons::BACK}, {"bed", Icons::BED}, {"cancel", Icons::CANCEL},
  {"clock", Icons::CLOCK_IMG}, {"cooldown", Icons::COOLDOWN_IMG}, {"delete", Icons::DELETE_IMG},
  {"emergency", Icons::EMERGENCY}, {"extrude", Icons::EXTRUDE_IMG}, {"extruder", Icons::EXTRUDER},
  {"factory_reset", Icons::FACTORY_RESET_IMG}, {"fan", Icons::FAN}, {"fan_off", Icons::FAN_OFF_IMG},
  {"fan_on", Icons::FAN_ON}, {"fine_tune", Icons::FINE_TUNE_IMG},
  {"firmware", Icons::FIRMWARE_IMG}, {"flow", Icons::EXTRUDE}, {"flow_up", Icons::FLOW_UP_IMG},
  {"gui", Icons::GUI_IMG}, {"heater", Icons::HEATER}, {"home", Icons::HOME}, {"home_z", Icons::HOME_Z},
  {"hourglass", Icons::HOURGLASS}, {"infinity", Icons::INFINITY_IMG}, {"info", Icons::INFO_IMG},
  {"layers", Icons::LAYERS_IMG}, {"light", Icons::LIGHT_IMG}, {"light_off", Icons::LIGHT_OFF},
  {"load_filament", Icons::LOAD_FILAMENT_IMG}, {"motor_off", Icons::MOTOR_OFF_IMG},
  {"move", Icons::MOVE}, {"pa_plus", Icons::PA_PLUS_IMG}, {"pause", Icons::PAUSE_IMG},
  {"print", Icons::PRINT}, {"refresh", Icons::REFRESH_IMG}, {"restart", Icons::RESTART_IMG},
  {"resume", Icons::RESUME}, {"retract", Icons::RETRACT_IMG}, {"sd", Icons::SD_IMG},
  {"shutdown", Icons::SHUTDOWN_IMG}, {"speed_up", Icons::SPEED_UP_IMG},
  {"spoolman", Icons::SPOOLMAN_IMG}, {"support_zip", Icons::SUPPORT_ZIP_IMG},
  {"switch_stock", Icons::SWITCH_STOCK_IMG}, {"unload_filament", Icons::UNLOAD_FILAMENT_IMG},
  {"update", Icons::UPDATE_IMG}, {"z_closer", Icons::Z_CLOSER}, {"z_farther", Icons::Z_FARTHER},
};

static const lv_img_dsc_t *macro_icon(const std::string &name) {
  auto it = macro_icons.find(name);
  if (it != macro_icons.end()) return it->second;
  LOG_ERROR("Unknown macro icon '{}', using refresh", name);
  return Icons::REFRESH_IMG;
}

MacroPanel::MacroPanel(lv_obj_t *parent, std::function<void(const std::string &)> send_gcode)
  : send(std::move(send_gcode))
  , grid(create_screen(parent))
{
  for (auto &m : Config::get_instance()->get_objects("/macro")) {
    std::string gcode = m.value("id", std::string());
    if (gcode.empty()) continue;
    std::string name = m.value("display_name", gcode);
    const lv_img_dsc_t *icon = macro_icon(m.value("icon", std::string("refresh")));
    buttons.push_back(std::make_unique<ButtonContainer>(grid, icon, name.c_str(),
                                                        &MacroPanel::handle_click, this));
    lv_obj_add_event_cb(buttons.back()->get_container(), &MacroPanel::handle_long_press,
                        LV_EVENT_LONG_PRESSED, this);
    gcodes.push_back(gcode);
  }
  params.resize(buttons.size());
  badges.assign(buttons.size(), NULL);

  if (buttons.empty()) {
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_label_set_text(lv_label_create(grid), ::tr("No macros configured"));
    return;
  }

  const size_t cols = 4;
  row_dsc.assign((buttons.size() + cols - 1) / cols, LV_GRID_CONTENT);
  row_dsc.push_back(LV_GRID_TEMPLATE_LAST);
  lv_obj_set_grid_dsc_array(grid, grid_col_dsc, row_dsc.data());

  for (size_t i = 0; i < buttons.size(); i++) {
    lv_obj_t *tile = buttons[i]->get_container();
    buttons[i]->use_card();
    lv_obj_set_height(tile, scale_r(92));
    lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i % cols, 1,
                         LV_GRID_ALIGN_STRETCH, i / cols, 1);
  }
  manage_scroll(grid);

  // Klipper's config (the macros' parameters) arrives after start-up and can
  // change on a restart: look until it is there, then now and then.
  refresh_timer = lv_timer_create(&MacroPanel::handle_refresh, 2000, this);
}

MacroPanel::~MacroPanel() {
  if (refresh_timer != NULL) lv_timer_del(refresh_timer);
  close_dialog();
}

int MacroPanel::tile_index(lv_obj_t *tile) {
  for (size_t i = 0; i < buttons.size(); i++)
    if (buttons[i]->get_container() == tile) return (int)i;
  return -1;
}

void MacroPanel::handle_click(lv_event_t *event) {
  MacroPanel *panel = (MacroPanel *)event->user_data;
  if (panel->suppress_click) {   // the release after a long press
    panel->suppress_click = false;
    return;
  }
  int i = panel->tile_index(lv_event_get_current_target(event));
  if (i >= 0) panel->send(panel->gcodes[i]);
}

void MacroPanel::handle_long_press(lv_event_t *event) {
  MacroPanel *panel = (MacroPanel *)event->user_data;
  int i = panel->tile_index(lv_event_get_current_target(event));
  if (i < 0 || panel->params[i].empty()) return;
  panel->suppress_click = true;
  panel->open_dialog(i);
}

// params.NAME / params['NAME'], each optionally |default(...). Only literal
// defaults (numbers, quoted strings, words) are kept for display.
static std::vector<MacroPanel::Param> parse_params(const std::string &gcode) {
  static const std::regex dotted(R"(params\.([A-Za-z_][A-Za-z0-9_]*)(?:\s*\|\s*default\(\s*([^)]*?)\s*\))?)");
  static const std::regex indexed(R"(params\[\s*['"]([A-Za-z_][A-Za-z0-9_]*)['"]\s*\](?:\s*\|\s*default\(\s*([^)]*?)\s*\))?)");
  static const std::regex literal(R"(^(-?[0-9]+(\.[0-9]*)?|'[^']*'|"[^"]*"|[A-Za-z_][A-Za-z0-9_]*)$)");
  std::vector<std::pair<size_t, MacroPanel::Param>> found;
  for (const std::regex *re : {&dotted, &indexed}) {
    for (auto it = std::sregex_iterator(gcode.begin(), gcode.end(), *re); it != std::sregex_iterator(); ++it) {
      MacroPanel::Param p;
      p.name = (*it)[1].str();
      std::transform(p.name.begin(), p.name.end(), p.name.begin(), [](unsigned char c) { return std::toupper(c); });
      std::string def = (*it)[2].matched ? (*it)[2].str() : "";
      if (std::regex_match(def, literal)) {
        if (def.size() >= 2 && (def.front() == '\'' || def.front() == '"')) def = def.substr(1, def.size() - 2);
        if (def != "none" && def != "None") p.def = def;
      }
      found.push_back({(size_t)it->position(), p});
    }
  }
  std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<MacroPanel::Param> out;
  std::set<std::string> seen;
  for (auto &f : found) {
    if (seen.insert(f.second.name).second) {
      out.push_back(f.second);
    } else if (!f.second.def.empty()) {
      for (auto &o : out)
        if (o.name == f.second.name && o.def.empty()) o.def = f.second.def;
    }
  }
  return out;
}

void MacroPanel::handle_refresh(lv_timer_t *timer) {
  MacroPanel *panel = (MacroPanel *)timer->user_data;
  panel->refresh_params();
  if (panel->params_loaded) lv_timer_set_period(timer, 30000);
}

void MacroPanel::refresh_params() {
  json &settings = State::get_instance()->get_data("/printer_state/configfile/settings"_json_pointer);
  if (!settings.is_object()) return;
  params_loaded = true;
  for (size_t i = 0; i < buttons.size(); i++) {
    std::string name = gcodes[i].substr(0, gcodes[i].find(' '));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    std::vector<Param> found;
    auto section = settings.find("gcode_macro " + name);
    if (section != settings.end() && section->is_object() && section->contains("gcode")
        && (*section)["gcode"].is_string())
      found = parse_params((*section)["gcode"].get<std::string>());
    params[i] = found;
    lv_obj_t *tile = buttons[i]->get_container();
    if (!found.empty() && badges[i] == NULL) {
      // a small settings badge in the tile's corner: hold for settings
      badges[i] = lv_label_create(tile);
      lv_label_set_text(badges[i], LV_SYMBOL_SETTINGS);
      lv_obj_add_flag(badges[i], LV_OBJ_FLAG_FLOATING);
      lv_obj_clear_flag(badges[i], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_text_color(badges[i], theme_secondary(), 0);
      lv_obj_align(badges[i], LV_ALIGN_TOP_RIGHT, -scale_r(4), scale_r(4));
    } else if (found.empty() && badges[i] != NULL) {
      lv_obj_del(badges[i]);
      badges[i] = NULL;
    }
  }
}

void MacroPanel::open_dialog(size_t idx) {
  close_dialog();
  dialog_idx = idx;
  lv_coord_t w = lv_disp_get_hor_res(NULL), h = lv_disp_get_ver_res(NULL);

  dialog = lv_obj_create(lv_layer_top());
  lv_obj_set_size(dialog, w, h);
  lv_obj_set_style_bg_color(dialog, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(dialog, LV_OPA_70, 0);
  lv_obj_set_style_border_width(dialog, 0, 0);
  lv_obj_set_style_radius(dialog, 0, 0);
  lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
  // a tap on the dimmed background puts the keyboard away
  lv_obj_add_event_cb(dialog, &MacroPanel::handle_dialog, LV_EVENT_CLICKED, this);

  // full height while the keyboard is away; show_keyboard() halves it
  card = lv_obj_create(dialog);
  lv_obj_set_size(card, w * 9 / 10, h - 2 * gap());
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, gap());
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, gap(), 0);
  // styled like the other popout boxes
  lv_obj_set_style_bg_color(card, col(SURFACE), 0);
  lv_obj_set_style_border_color(card, col(BORDER), 0);
  lv_obj_set_style_border_width(card, border_w(), 0);
  lv_obj_set_style_radius(card, radius_lg(), 0);
  lv_obj_set_style_pad_all(card, popout_pad(), 0);

  lv_obj_t *head = lv_obj_create(card);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *title = lv_label_create(head);
  lv_label_set_text(title, (std::string(LV_SYMBOL_SETTINGS "  ") + gcodes[idx]).c_str());
  lv_obj_set_style_text_color(title, col(TEXT), 0);
  lv_obj_t *buttons_row = lv_obj_create(head);
  lv_obj_remove_style_all(buttons_row);
  lv_obj_set_size(buttons_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(buttons_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(buttons_row, gap(), 0);
  // the theme's flat buttons; Send carries the accent as the dialog's action
  cancel_btn = create_text_btn(buttons_row, ::tr("Cancel"), &MacroPanel::handle_dialog, this);
  send_btn = create_text_btn(buttons_row, ::tr("Send"), &MacroPanel::handle_dialog, this);
  set_action_btn(send_btn, true, theme_primary());
  lv_obj_set_style_text_color(lv_obj_get_child(send_btn, 0), col(ON_PRIMARY), 0);
  // compact: sized to the text, so the header stays one slim line
  for (lv_obj_t *b : {cancel_btn, send_btn}) {
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(b, scale_r(6), 0);
    lv_obj_set_style_pad_hor(b, scale_r(14), 0);
  }

  // one row per parameter: its name, and a field with the default as placeholder
  fields.clear();
  for (auto &p : params[idx]) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, p.name.c_str());
    lv_obj_set_width(label, LV_PCT(40));
    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, p.def.empty() ? "default" : p.def.c_str());
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_add_event_cb(ta, &MacroPanel::handle_dialog, LV_EVENT_FOCUSED, this);
    fields.push_back(ta);
  }

  // only while a field is being edited
  keyboard = lv_keyboard_create(dialog);
  lv_obj_set_size(keyboard, w, h / 2);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(keyboard, &MacroPanel::handle_dialog, LV_EVENT_READY, this);
  lv_obj_add_event_cb(keyboard, &MacroPanel::handle_dialog, LV_EVENT_CANCEL, this);
  lv_keyboard_set_textarea(keyboard, NULL);
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void MacroPanel::show_keyboard(lv_obj_t *field) {
  if (dialog == NULL) return;
  lv_coord_t h = lv_disp_get_ver_res(NULL);
  if (field != NULL) {
    lv_keyboard_set_textarea(keyboard, field);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(card, h / 2 - 2 * gap());
    lv_obj_update_layout(card);
    lv_obj_scroll_to_view(lv_obj_get_parent(field), LV_ANIM_ON);
  } else {
    lv_obj_t *ta = lv_keyboard_get_textarea(keyboard);
    if (ta != NULL) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(keyboard, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(card, h - 2 * gap());
  }
}

void MacroPanel::close_dialog() {
  if (dialog != NULL) lv_obj_del(dialog);  // its children (keyboard, fields) go with it
  dialog = card = keyboard = send_btn = cancel_btn = NULL;
  fields.clear();
}

void MacroPanel::handle_dialog(lv_event_t *event) {
  MacroPanel *panel = (MacroPanel *)event->user_data;
  lv_obj_t *target = lv_event_get_target(event);
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_FOCUSED) {
    panel->show_keyboard(target);
    return;
  }
  // the keyboard's check / close keys, or a tap on the background: put it away
  if ((target == panel->keyboard && (code == LV_EVENT_READY || code == LV_EVENT_CANCEL))
      || (target == panel->dialog && code == LV_EVENT_CLICKED)) {
    panel->show_keyboard(NULL);
    return;
  }
  if (target == panel->cancel_btn) {
    panel->close_dialog();
    return;
  }
  if (target == panel->send_btn) {
    // only the filled-in fields: the macro's own defaults cover the rest
    std::string gcode = panel->gcodes[panel->dialog_idx];
    for (size_t i = 0; i < panel->fields.size(); i++) {
      std::string v = lv_textarea_get_text(panel->fields[i]);
      size_t first = v.find_first_not_of(' ');
      if (first == std::string::npos) continue;
      v = v.substr(first, v.find_last_not_of(' ') - first + 1);
      if (v.find(' ') != std::string::npos) v = "\"" + v + "\"";
      gcode += " " + panel->params[panel->dialog_idx][i].name + "=" + v;
    }
    panel->close_dialog();
    panel->send(gcode);
  }
}
