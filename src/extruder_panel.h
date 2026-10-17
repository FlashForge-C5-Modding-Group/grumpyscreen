#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "spoolman_panel.h"
#include "selector.h"
#include "button_container.h"
#include "sensor_container.h"
#include "numpad.h"
#include "lvgl/lvgl.h"
#include <memory>
#include <string>
#include <vector>

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np, SpoolmanPanel &sm);
  ~ExtruderPanel();

  void foreground();
  void background();
  void enable_spoolman();  
  void consume(json &j);
  void handle_callback(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_extruder_select(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->select_extruder(lv_dropdown_get_selected(lv_event_get_target(event)), true);
  };

 private:
  // Multi-extruder: every extruder Klipper reports gets a readout, a drop-down
  // picks which one the panel acts on, and the actions switch to it first.
  void refresh_extruders();
  void select_extruder(size_t idx, bool by_user);
  std::string activate_selected();

  KWebSocketClient &ws;
  Numpad &numpad;
  std::vector<std::string> extruders;              // "extruder", "extruder1", ...
  std::vector<std::unique_ptr<SensorContainer>> extra_temps;  // readouts after the first
  lv_obj_t *extruder_dropdown = NULL;
  size_t selected = 0;
  bool user_selected = false;
  std::string active_extruder = "extruder";
  lv_obj_t *panel_cont;
  SpoolmanPanel &spoolman_panel;
  std::vector<std::string> temp_options;
  std::vector<const char*> temp_option_map;
  uint32_t temp_default_idx;
  std::vector<std::string> length_options;
  std::vector<const char*> length_option_map;
  uint32_t length_default_idx;
  std::vector<std::string> speed_options;
  std::vector<const char*> speed_option_map;
  uint32_t speed_default_idx;
  SensorContainer extruder_temp;
  Selector temp_selector;
  Selector length_selector;
  Selector speed_selector;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;
  ButtonContainer cooldown_btn;
  // the right-hand tiles share a column of their own, spanning every row, so
  // the header can stay the height of the readout while they still get a full
  // tile's worth of room each. Declared before them: it is their parent.
  lv_obj_t *right_col;
  ButtonContainer spoolman_btn;
  ButtonContainer extrude_btn;
  ButtonContainer retract_btn;
  ButtonContainer back_btn;
};

#endif // __EXTRUDER_PANEL_H__
