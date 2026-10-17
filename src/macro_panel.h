#ifndef __MACRO_PANEL_H__
#define __MACRO_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// One button per [macro "GCODE"] section in grumpyscreen.cfg, sent through the console.
// A macro whose Klipper G-code takes parameters gets a settings badge; holding
// it opens a dialog to fill them in before sending.
class MacroPanel {
 public:
  MacroPanel(lv_obj_t *parent, std::function<void(const std::string &)> send);
  ~MacroPanel();

  struct Param {
    std::string name;
    std::string def;  // literal default, or empty
  };

 private:
  static void handle_click(lv_event_t *event);
  static void handle_long_press(lv_event_t *event);
  static void handle_refresh(lv_timer_t *timer);
  static void handle_dialog(lv_event_t *event);
  void refresh_params();
  void open_dialog(size_t idx);
  void close_dialog();
  void show_keyboard(lv_obj_t *field);  // NULL hides it
  int tile_index(lv_obj_t *tile);

  std::function<void(const std::string &)> send;
  lv_obj_t *grid;
  std::vector<std::unique_ptr<ButtonContainer>> buttons;
  std::vector<std::string> gcodes;
  std::vector<std::vector<Param>> params;   // per button, from Klipper's config
  std::vector<lv_obj_t *> badges;           // per button, NULL until it has params
  lv_timer_t *refresh_timer = NULL;
  bool params_loaded = false;
  bool suppress_click = false;              // the click that ends a long press

  // settings dialog
  lv_obj_t *dialog = NULL;
  lv_obj_t *card = NULL;
  lv_obj_t *keyboard = NULL;
  lv_obj_t *send_btn = NULL;
  lv_obj_t *cancel_btn = NULL;
  std::vector<lv_obj_t *> fields;
  size_t dialog_idx = 0;

  // LVGL keeps this pointer for later layout passes
  std::vector<lv_coord_t> row_dsc;
};

#endif // __MACRO_PANEL_H__
