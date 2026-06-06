#include "wled.h"
#include <HTTPClient.h>

class snapmaker_u1 : public Usermod {
private:
  unsigned long lastPoll = 0;
  String snapIp = "";
  String apiKey = "";
  uint16_t snapPort = 7125;

  // Theme colors
 uint32_t colorIdleA = RGBW32(255,0,200,0);       // pink-purple
 uint32_t colorIdleB = RGBW32(255,255,255,0);     // white

 uint32_t colorPrint = RGBW32(255,0,200,0);       // progress fill
 uint32_t colorRemain = RGBW32(255,255,255,0);    // progress remaining

 uint32_t colorPause = RGBW32(255,255,255,0);     // white pulse
 uint32_t colorComplete = RGBW32(255,0,200,0);    // pink-purple blink

 uint32_t colorPreheatA = RGBW32(120,0,90,0);     // dim pink-purple
 uint32_t colorPreheatB = RGBW32(255,0,200,0);    // bright pink-purple
 uint32_t colorHeatA = RGBW32(255,255,255,0);     // white
 uint32_t colorHeatB = RGBW32(255,0,200,0);       // pink-purple

 uint32_t colorError = RGBW32(255,0,0,0);         // red

  float progress = 0.0f;
  String ps = "idle";
  bool heating = false;
  unsigned long completeUntil = 0;

  static const char _name[];
  bool enabled = true;

  void fillAll(uint32_t color) {
    uint16_t count = strip.getLengthTotal();
    for (uint16_t i = 0; i < count; i++) strip.setPixelColor(i, color);
  }
  uint32_t scaleColor(uint32_t color, uint8_t scale) {
    uint8_t r = ((color >> 16) & 0xFF) * scale / 255;
    uint8_t g = ((color >> 8) & 0xFF) * scale / 255;
    uint8_t b = (color & 0xFF) * scale / 255;
    return RGBW32(r, g, b, 0);
  }

  uint32_t blendColor(uint32_t colorA, uint32_t colorB, uint8_t amount) {
   uint8_t ar = (colorA >> 16) & 0xFF;
   uint8_t ag = (colorA >> 8) & 0xFF;
   uint8_t ab = colorA & 0xFF;

   uint8_t br = (colorB >> 16) & 0xFF;
   uint8_t bg = (colorB >> 8) & 0xFF;
   uint8_t bb = colorB & 0xFF;

   uint8_t r = ar + ((br - ar) * amount) / 255;
   uint8_t g = ag + ((bg - ag) * amount) / 255;
   uint8_t b = ab + ((bb - ab) * amount) / 255;

   return RGBW32(r,g,b,0);
  }

  void drawProgressBar(float p) {
    uint16_t count = strip.getLengthTotal();
    if (count == 0) return;

    p = constrain(p, 0.0f, 1.0f);
    uint16_t lit = max((uint16_t)1, (uint16_t)(p * count));

    for (uint16_t i = 0; i < count; i++) {
      if (i < lit) {
        strip.setPixelColor(i, colorPrint);
      } else {
        strip.setPixelColor(i, colorRemain);
      }
    }
  }

  void updateSnapmakerStatus() {
    if (snapIp.isEmpty() || WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = "http://" + snapIp + ":" + String(snapPort) +
                 "/printer/objects/query?print_stats&display_status&heater_bed&extruder";

    if (!http.begin(url)) return;
    if (!apiKey.isEmpty()) http.addHeader("X-Api-Key", apiKey);

    int code = http.GET();
    if (code == 200) {
      StaticJsonDocument<2048> doc;
      if (!deserializeJson(doc, http.getStream())) {

        JsonObject status = doc["result"]["status"];

        ps = status["print_stats"]["state"] | "idle";
        progress = status["display_status"]["progress"] | 0.0f;

        float bt = status["heater_bed"]["target"] | 0.0f;
        float bc = status["heater_bed"]["temperature"] | 0.0f;
        float et = status["extruder"]["target"] | 0.0f;
        float ec = status["extruder"]["temperature"] | 0.0f;

        heating = (bt > bc + 2.0f) || (et > ec + 2.0f);

        if (ps == "complete") {
          if (completeUntil == 0) completeUntil = millis() + 10000;
        } else {
          completeUntil = 0;
        }
      }
    }

    http.end();
  }

public:
  void setup() override {}

  void loop() override {
    if (!enabled) return;

    if (millis() - lastPoll > 1200) {
      lastPoll = millis();
      updateSnapmakerStatus();
    }
  }

  void handleOverlayDraw() override {
    if (!enabled) return;

    float pulse = (sin(millis() * 0.002f) + 1.0f) * 0.5f;
    bool blink = ((millis() / 500) % 2) == 0;

    // 💔 Error (printer fault)
    if (ps == "error") {
      uint8_t v = 120 + (uint8_t)(135 * pulse);
      fillAll(RGBW32(v,0,0,0));
      return;
    }

    // 🤍 Paused (white pulse)
    if (ps == "paused") {
      uint8_t v = 120 + (uint8_t)(135 * pulse);
      fillAll(scaleColor(colorPause, v));
      return;
    }

    // 🩷 Complete (pink-purple blink)
    if (completeUntil > 0) {
      if (millis() < completeUntil) {
        uint8_t scale = blink ? 100 : 45;
        fillAll(scaleColor(colorComplete, (255 * scale) / 100));
        return;
      }
      completeUntil = 0;
    }

    // 🩷 Preheat (pink-purple breathing)
    if (ps == "printing" && progress < 0.01f) {
      uint8_t amount = (uint8_t)(255 * pulse);
      fillAll(blendColor(colorPreheatA, colorPreheatB, amount));
      return;
    }

    // 🩷🤍 Printing (progress bar)
    if (ps == "printing") {
      drawProgressBar(progress);
      return;
    }

    // 🤍🩷 Heating fallback (white ↔ pink-purple)
    if (heating) {
      uint8_t amount = (uint8_t)(255 * pulse);
      fillAll(blendColor(colorHeatA, colorHeatB, amount));
      return;
    }

    // 🩷🤍 Idle (color swap wipe)
    uint16_t count = strip.getLengthTotal();
    if (count == 0) return;

    uint16_t pos = (millis() / 100) % count;
    bool phase = ((millis() / 100) / count) % 2;

    for (uint16_t i = 0; i < count; i++) {
     if (!phase) {
     strip.setPixelColor(i, i <= pos ? colorIdleA : colorIdleB);
     } else {
     strip.setPixelColor(i, i <= pos ? colorIdleB : colorIdleA);
     }
    }
 
  }
  

  void addToConfig(JsonObject &root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    

    top["Connection"] = snapIp;
    top["port"] = snapPort;
    top["api"] = apiKey;

    top["idleA_R"] = (colorIdleA >> 16) & 0xFF;
    top["idleA_G"] = (colorIdleA >> 8) & 0xFF;
    top["idleA_B"] = colorIdleA & 0xFF;

    top["idleB_R"] = (colorIdleB >> 16) & 0xFF;
    top["idleB_G"] = (colorIdleB >> 8) & 0xFF;
    top["idleB_B"] = colorIdleB & 0xFF;

    top["preheatA_R"] = (colorPreheatA >> 16) & 0xFF;
    top["preheatA_G"] = (colorPreheatA >> 8) & 0xFF;
    top["preheatA_B"] = colorPreheatA & 0xFF;

    top["preheatB_R"] = (colorPreheatB >> 16) & 0xFF;
    top["preheatB_G"] = (colorPreheatB >> 8) & 0xFF;
    top["preheatB_B"] = colorPreheatB & 0xFF;

    top["print_R"] = (colorPrint >> 16) & 0xFF;
    top["print_G"] = (colorPrint >> 8) & 0xFF;
    top["print_B"] = colorPrint & 0xFF;

    top["remain_R"] = (colorRemain >> 16) & 0xFF;
    top["remain_G"] = (colorRemain >> 8) & 0xFF;
    top["remain_B"] = colorRemain & 0xFF;

    top["pause_R"] = (colorPause >> 16) & 0xFF;
    top["pause_G"] = (colorPause >> 8) & 0xFF;
    top["pause_B"] = colorPause & 0xFF;

    top["complete_R"] = (colorComplete >> 16) & 0xFF;
    top["complete_G"] = (colorComplete >> 8) & 0xFF;
    top["complete_B"] = colorComplete & 0xFF;

    top["heatA_R"] = (colorHeatA >> 16) & 0xFF;
    top["heatA_G"] = (colorHeatA >> 8) & 0xFF;
    top["heatA_B"] = colorHeatA & 0xFF;

    top["heatB_R"] = (colorHeatB >> 16) & 0xFF;
    top["heatB_G"] = (colorHeatB >> 8) & 0xFF;
    top["heatB_B"] = colorHeatB & 0xFF;

  }

  bool readFromConfig(JsonObject &root) override {
    JsonObject top = root[FPSTR(_name)];
    if (top.isNull()) return false;

    snapIp = top["Connection"] | "";
    snapPort = top["port"] | 7125;
    apiKey = top["api"] | "";

    uint8_t idleA_R = top["idleA_R"] | 255;
    uint8_t idleA_G = top["idleA_G"] | 0;
    uint8_t idleA_B = top["idleA_B"] | 200;
    colorIdleA = RGBW32(idleA_R, idleA_G, idleA_B, 0);

    uint8_t idleB_R = top["idleB_R"] | 255;
    uint8_t idleB_G = top["idleB_G"] | 255;
    uint8_t idleB_B = top["idleB_B"] | 255;
    colorIdleB = RGBW32(idleB_R, idleB_G, idleB_B, 0);

    uint8_t preheatA_R = top["preheatA_R"] | 120;
    uint8_t preheatA_G = top["preheatA_G"] | 0;
    uint8_t preheatA_B = top["preheatA_B"] | 90;
    colorPreheatA = RGBW32(preheatA_R, preheatA_G, preheatA_B, 0);

    uint8_t preheatB_R = top["preheatB_R"] | 255;
    uint8_t preheatB_G = top["preheatB_G"] | 0;
    uint8_t preheatB_B = top["preheatB_B"] | 200;
    colorPreheatB = RGBW32(preheatB_R, preheatB_G, preheatB_B, 0);

    uint8_t print_R = top["print_R"] | 255;
    uint8_t print_G = top["print_G"] | 0;
    uint8_t print_B = top["print_B"] | 200;
    colorPrint = RGBW32(print_R, print_G, print_B, 0);

    uint8_t remain_R = top["remain_R"] | 255;
    uint8_t remain_G = top["remain_G"] | 255;
    uint8_t remain_B = top["remain_B"] | 255;
    colorRemain = RGBW32(remain_R, remain_G, remain_B, 0);

    uint8_t pause_R = top["pause_R"] | 255;
    uint8_t pause_G = top["pause_G"] | 255;
    uint8_t pause_B = top["pause_B"] | 255;
    colorPause = RGBW32(pause_R, pause_G, pause_B, 0);

    uint8_t complete_R = top["complete_R"] | 255;
    uint8_t complete_G = top["complete_G"] | 0;
    uint8_t complete_B = top["complete_B"] | 200;
    colorComplete = RGBW32(complete_R, complete_G, complete_B, 0);

    uint8_t heatA_R = top["heatA_R"] | 255;
    uint8_t heatA_G = top["heatA_G"] | 255;
    uint8_t heatA_B = top["heatA_B"] | 255;
    colorHeatA = RGBW32(heatA_R, heatA_G, heatA_B, 0);

    uint8_t heatB_R = top["heatB_R"] | 255;
    uint8_t heatB_G = top["heatB_G"] | 0;
    uint8_t heatB_B = top["heatB_B"] | 200;
    colorHeatB = RGBW32(heatB_R, heatB_G, heatB_B, 0);
    return true;
  }

  void appendConfigData() override {

  // Connection
  oappend(F("addInfo('SnapmakerU1:Connection',1,'','<hr><b>Printer IP only.</b><br>Do not include http://, https://, :7125, or .local hostnames.<br><b>IP:</b>');"));
  oappend(F("addInfo('SnapmakerU1:port',1,'Moonraker port. Default is 7125.');"));
  oappend(F("addInfo('SnapmakerU1:api',1,'Moonraker API key. Leave blank if your Moonraker setup does not require one.<hr> All color values use <font color=\"red\">R</font><font color=\"lime\">G</font><font color=\"deepskyblue\">B</font> channels from 0-255. Example: #FF00C8 = <font color=\"red\">R</font> 255, <font color=\"lime\">G</font> 0, <font color=\"deepskyblue\">B</font> 200.<hr> <br><b>Idle Colors</b><br>Idle Color A is the moving wipe color.');"));
  
  // Idle
  oappend(F("addInfo('SnapmakerU1:idleA_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:idleA_B',1,'<br><b>Idle Color B</b><br>Background wipe color.','');"));
  oappend(F("addInfo('SnapmakerU1:idleB_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:idleB_B',1,'<hr><b>Preheat Colors</b><br>Preheat Color A is the dim side of the breathing animation.');"));
  

  // Printing
  oappend(F("addInfo('SnapmakerU1:print_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:print_B',1,'<br><b>Remaining Color</b><br>Unfilled portion of the progress bar.','');"));
  oappend(F("addInfo('SnapmakerU1:remain_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:remain_B',1,'<hr><b>Paused / Complete Colors</b><br>Pause Color controls the paused pulse.');"));

  // Pause / Complete
  oappend(F("addInfo('SnapmakerU1:pause_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:pause_B',1,'<br><b>Complete Color</b><br>Controls the print-complete blink.','');"));
  oappend(F("addInfo('SnapmakerU1:complete_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:complete_B',1,'<hr><b>Heating Fallback Colors</b><br>Heating Color A is one side of the heating fallback blend.','');"));

  // Preheat
  oappend(F("addInfo('SnapmakerU1:preheatA_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:preheatA_B',1,'<br><b>Preheat Color B</b><br>Bright side of the breathing animation.','');"));
  oappend(F("addInfo('SnapmakerU1:preheatB_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:preheatB_B',1,'<hr><b>Printing Colors</b><br>Print Color is the filled portion of the progress bar.','');"));

   // Heating fallback
  oappend(F("addInfo('SnapmakerU1:heatA_R',1,'','');"));
  oappend(F("addInfo('SnapmakerU1:heatA_B',1,'<br><b>Heating Color B</b><br>Other side of the heating fallback blend.','');"));
  oappend(F("addInfo('SnapmakerU1:heatB_R',1,'','');"));

}

  uint16_t getId() override {
    return 0xBEEF;
  }
};

const char snapmaker_u1::_name[] PROGMEM = "SnapmakerU1";

static snapmaker_u1 usermod_snapmaker;
REGISTER_USERMOD(usermod_snapmaker);
