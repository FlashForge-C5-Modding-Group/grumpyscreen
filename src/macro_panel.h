#ifndef __MACRO_PANEL_H__
#define __MACRO_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"
#include "numpad.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// One button per [macro "GCODE"] section in grumpyscreen.cfg, sent through the console.
// A macro with parameters gets a settings cog; holding it opens them to fill in.
class MacroPanel {
 public:
  MacroPanel(lv_obj_t *parent, std::function<void(const std::string &)> send);
  ~MacroPanel();

  struct Param {
    std::string name;
    std::string def;  // literal default, or empty
  };

 private:
  struct Parameter {
    enum class Type { Choices, IntegerRange };

    std::string name;
    Type type;
    std::vector<std::string> choices;
    int minimum = 0;
    int maximum = 0;
    lv_obj_t *label = nullptr;
    lv_obj_t *control = nullptr;
  };

  struct Macro {
    // Each part is either literal gcode or a parameter index encoded as a
    // negative value (-1 is parameter 0).  Keeping the literals means fixed
    // arguments in a macro header survive the round trip unchanged.
    std::vector<std::string> literals;
    std::vector<int> parts;
    std::vector<Parameter> parameters;
    bool unsupported_parameter_count = false;
  };

  static void handle_click(lv_event_t *event);
  static void handle_pressed(lv_event_t *event);
  static void handle_long_press(lv_event_t *event);
  static void handle_refresh(lv_timer_t *timer);
  static void handle_dialog(lv_event_t *event);
  static void handle_popup_action(lv_event_t *event);
  static void handle_range_input(lv_event_t *event);
  static bool parse_macro(const std::string &gcode, Macro &macro);
  void open_popup(size_t macro_index, const std::string &title);
  void close_popup();
  void send_popup_macro();
  void refresh_params();
  void open_dialog(size_t idx);
  void close_dialog();
  void show_keyboard(lv_obj_t *field);  // NULL hides it
  int tile_index(lv_obj_t *tile);

  std::function<void(const std::string &)> send;
  lv_obj_t *grid;
  std::vector<std::unique_ptr<ButtonContainer>> buttons;
  std::vector<std::string> gcodes;
  std::vector<Macro> macros;
  static constexpr size_t max_parameters = 5;
  lv_obj_t *popup = nullptr;
  std::unique_ptr<Numpad> popup_numpad;
  lv_obj_t *active_range_control = nullptr;
  size_t active_macro = 0;
  std::vector<std::vector<Param>> params;   // per button, from Klipper's config
  std::vector<lv_obj_t *> badges;           // per button, NULL without settings
  lv_timer_t *refresh_timer = NULL;
  bool params_loaded = false;
  bool suppress_click = false;              // the click that ends a long press

  // settings for a macro with no {} parameters of its own
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
