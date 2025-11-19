#define LGFX_USE_V1
#include <lvgl.h>               // For graphical interface
#include <LovyanGFX.hpp>        // For display driver

#include <Adafruit_NeoPixel.h>  // For controlling LED
#include <WiFi.h>
#include <WiFiUdp.h>

#include <Adafruit_SSD1306.h>

#include "CST816D.h"      // Touch driver file
#include "ui.h"           // All LVGL interfaces designed, indexed by this

// I2C pins for the touch screen
#define TP_I2C_SDA_PIN 6
#define TP_I2C_SCL_PIN 7
#define I2C_SDA_PIN 38
#define I2C_SCL_PIN 39

String wifiId = "elecrow888";            // WiFi SSID to connect to
String wifiPwd = "elecrow2014";          // PASSWORD of the WiFi SSID to connect to

/*
  Five LEDs below the 1.28-inch display
*/
#define POWER_LIGHT_PIN 40            // Power light pin                               
#define LED_PIN 48                    // LED pin
#define LED_NUM 5                     // Total of 5 rendering LEDs

Adafruit_NeoPixel led = Adafruit_NeoPixel(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);  
bool isLed = true;

#define ENCODER_A_PIN 45              // Rotary display control pin 1, CLK pin
#define ENCODER_B_PIN 42              // Rotary display control pin 2, DT pin
#define SWITCH_PIN 41                 // Interrupt pin for rotary knob press

volatile int8_t position_tmp = -1;    // Temporary position of the rotary knob (0: clockwise, 1: counterclockwise)
volatile int8_t currentA = 0;         // For rotary encoder state detection, current state
volatile int8_t lastA = 0;            // For rotary encoder state detection, previous pin state

volatile unsigned long lastPressTime = 0;     // Record the last button press time (for debounce and double-click detection)
volatile bool pressFlag = false;      // Button press flag
volatile int clickCount = 0;          // Number of clicks (for detecting double-click)
const unsigned long debounceTime = 20;        // Debounce time (milliseconds)
const unsigned long doubleClickTime = 300;    // Double-click interval (milliseconds)

void IRAM_ATTR buttonISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > debounceTime) {
    if (digitalRead(SWITCH_PIN)) {
      pressFlag = false;
    } else {
      pressFlag = true;
      lastPressTime = interruptTime;
      clickCount++;
    }
  }
  lastInterruptTime = interruptTime;
}

TaskHandle_t ledTestTaskHandle = NULL;
TaskHandle_t encTaskHandle = NULL;

#define TP_INT 5                  // Touch interrupt pin
#define TP_RST 13                 // Touch reset pin
#define SCREEN_BACKLIGHT_PIN 46   // Screen backlight pin

const int pwmFreq = 5000;
const int pwmChannel = 0;
const int pwmResolution = 8;


class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST; 
      cfg.spi_mode = 0;      
      cfg.freq_write = 80000000;    
      cfg.freq_read = 20000000;         
      cfg.spi_3wire = true;      
      cfg.use_lock = true;       
      cfg.dma_channel = SPI_DMA_CH_AUTO;  
      cfg.pin_sclk = 10; 
      cfg.pin_mosi = 11;  
      cfg.pin_miso = -1; 
      cfg.pin_dc = 3;  
      _bus_instance.config(cfg);          
      _panel_instance.setBus(&_bus_instance); 
    }
    {                            
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 9;                 
      cfg.pin_rst = 14;       
      cfg.pin_busy = -1;         
      cfg.memory_width = 240; 
      cfg.memory_height = 240;  
      cfg.panel_width = 240;
      cfg.panel_height = 240; 
      cfg.offset_x = 0;   
      cfg.offset_y = 0;   
      cfg.offset_rotation = 0;  
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;  
      cfg.readable = false;  
      cfg.invert = true;    
      cfg.rgb_order = false;   
      cfg.dlen_16bit = false;   
      cfg.bus_shared = false;   
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);  
  }
};

LGFX gfx;                                                  
CST816D touch(TP_I2C_SDA_PIN, TP_I2C_SCL_PIN, TP_RST, TP_INT); 

static const uint32_t screenWidth = 240;    
static const uint32_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;     // Drawing buffer
static lv_color_t *buf = NULL;          // Buffer required by LVGL
static lv_color_t *buf1 = NULL;         // Buffer required by LVGL

lv_obj_t *current_screen = NULL;        // Pointer to the current screen   // Currently active screen object
int screen1_index = 1;                  // Main screen index    // Current screen index (0: Volume, 1: Temperature, 2: Brightness)

// Variables related to servo control
int lastServoAngle = -1; // Record the last servo angle

// Variables related to UDP communication
WiFiUDP udp;
const char* ADVANCE_IP = "192.168.50.131";  // IP address of the Advance device, modify as needed
const int ADVANCE_PORT = 8888;             // UDP port of the Advance device
int lastVolumeSent = -1;                   // Record the last sent volume value

// Forward declarations: LVGL slider event callbacks
void volumeArcEventCb(lv_event_t *e);
// void volumeIconEventCb(lv_event_t *e);

void tempArcEventCb(lv_event_t *e);

// Initial interface values (applied when entering the screen)
int volumeValue = 50;  // 0-100
int tempValue = 0;     // 0-200
int lightValue = 50;   // 0-100

//****************************************************SETUP****************************************************
void setup() {
  Serial.begin(115200);

  pinMode(POWER_LIGHT_PIN, OUTPUT);   
  digitalWrite(POWER_LIGHT_PIN, LOW); 

  pinMode(1, OUTPUT);         // Power pin 1, output current
  digitalWrite(1, HIGH);  
  pinMode(2, OUTPUT);         // Power pin 2, output current
  digitalWrite(2, HIGH);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);  // Initialize I2C bus
  touch.begin();              // Start the touch screen

  // Initialize display and set background color
  gfx.init();               
  gfx.initDMA();
  gfx.startWrite();
  gfx.setColor(0, 0, 0);
  gfx.setTextSize(2);
  gfx.fillScreen(TFT_BLACK);

  pinMode(ENCODER_A_PIN, INPUT);        // Set rotary encoder pins
  pinMode(ENCODER_B_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);    // Set switch (press) pin -- (with interrupt) buttonISR
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), buttonISR, CHANGE);

  lv_init();        // Initialize LVGL

  size_t buffer_size = sizeof(lv_color_t) * screenWidth * screenHeight;
  buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);  //MALLOC_CAP_DEFAULT,MALLOC_CAP_DMA,MALLOC_CAP_INTERNAL
  buf1 = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  if (!buf)
    Serial.println("Failed to allocate for LVGL buf!");
  if (!buf1)
    Serial.println("Failed to allocate for LVGL buf1!");
  lv_disp_draw_buf_init(&draw_buf, buf, buf1, screenWidth * screenHeight);

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;  //Used to refresh the display
  disp_drv.draw_buf = &draw_buf;
  // disp_drv.full_refresh = 1;  //0
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read; // Used to read touch events
  lv_indev_drv_register(&indev_drv);
  delay(100);

  ui_init();

  // Bind event for volume slider value change
  lv_obj_add_event_cb(ui_VolumeArc, volumeArcEventCb, LV_EVENT_VALUE_CHANGED, NULL);
  // Bind event for volume icon click
  // lv_obj_add_event_cb(ui_volumeBlue, volumeIconEventCb, LV_EVENT_RELEASED, NULL);
  // Bind event for temperature slider value change
  lv_obj_add_event_cb(ui_TempArc, tempArcEventCb, LV_EVENT_VALUE_CHANGED, NULL);

  // Initialize LED strip (Adafruit_NeoPixel)
  // Five LEDs at the bottom of the device (for ambiance)
  led.begin();            
  led.setBrightness(25);  
  led.clear();
  led.show();

  delay(500);
  initBacklight(); // Initialize backlight (controlled via PWM)

  xTaskCreatePinnedToCore(ledTestTask, "LED Test", 2048, NULL, 1, &ledTestTaskHandle, 0);
  xTaskCreatePinnedToCore(encTask, "ENC", 2048, NULL, 1, &encTaskHandle, 0);

  // Initialize servo control variables
  Serial.println("Initializing servo control...");
  lastServoAngle = 0;
  
  Serial.println("Servo control initialization completed");

  Serial.println("Settings completed, ready to connect to WiFi...");
  connectWiFi();

  // After WiFi is connected, get the IP address of the Advance device
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected successfully, preparing for UDP communication...");
    Serial.print("Local IP address: ");
    Serial.println(WiFi.localIP());
  }
}


void loop() {
  lv_timer_handler(); /* let the GUI do its work */
  delay(5);
}

//  WiFi connection function
void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(wifiId.c_str(), wifiPwd.c_str());
  
  // Wait for connection, up to 10 seconds
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

// Send volume control command to Advance device
void sendVolumeCommand(int volume) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send volume command");
    return;
  }
  
  // Construct volume control command
  String command = "VOLUME:" + String(volume);
  
  // Send UDP packet
  udp.beginPacket(ADVANCE_IP, ADVANCE_PORT);
  udp.write((uint8_t*)command.c_str(), command.length());
  bool sent = udp.endPacket();
  
  if (sent) {
    Serial.printf("Volume command sent successfully: %s\n", command.c_str());
  } else {
    Serial.println("Failed to send volume command");
  }
}

// Send servo control command to Advance device
void sendServoCommand(int angle) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send servo command");
    return;
  }
  // Construct servo control command
  String command = "SERVO:" + String(angle);
  // Send UDP packet
  udp.beginPacket(ADVANCE_IP, ADVANCE_PORT);
  udp.write((uint8_t*)command.c_str(), command.length());
  bool sent = udp.endPacket();

  if (sent) {
    Serial.printf("Servo command sent successfully: %s\n", command.c_str());
  } else {
    Serial.println("Failed to send servo command");
  }
}

void updateServoByTemperature(int temp) {
  // Constrain temperature to 0-180 range (only use 0-180; 180-200 only for display)
  temp = constrain(temp, 0, 180);
  
  // Map 0-180 temperature range to 0-180 servo angle
  int servoAngle = map(temp, 0, 180, 0, 180);
  
  // Check if update is needed
  if (servoAngle == lastServoAngle) return;
  
  // Save current servo angle
  lastServoAngle = servoAngle;
  
  // Send servo command to Advance device
  sendServoCommand(servoAngle);
  
  // Debug output
  Serial.printf("Temperature: %d°C → Servo angle: %d°\n", temp, servoAngle);
}


// Volume slider event: update value and send via UDP to Advance
void volumeArcEventCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *arc = lv_event_get_target(e);
  int value = lv_arc_get_value(arc); // 0-100

  // Update UI text, keeping format consistent with rotary logic
  char volText[8];
  if (value == 100) {
    snprintf(volText, sizeof(volText), "%d%%", value);
    lv_label_set_text(ui_VolNum, volText);
  } else {
    snprintf(volText, sizeof(volText), " %d%%", value);
    lv_label_set_text(ui_VolNum, volText);
  }

  // Send to Advance (only if value changed)
  if (value != lastVolumeSent) {
    sendVolumeCommand(value);
    lastVolumeSent = value;
  }
}

// Temperature slider event: update temperature text and control servo
void tempArcEventCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *arc = lv_event_get_target(e);
  int value = lv_arc_get_value(arc); // 0-200

  // Update temperature text (consistent with existing format)
  char tempText[8];
  if (value >= 100 && value <= 200) {
    snprintf(tempText, sizeof(tempText), " %d%°", value);
    lv_label_set_text(ui_TempNum, tempText);
  } else {
    snprintf(tempText, sizeof(tempText), "  %d%°", value);
    lv_label_set_text(ui_TempNum, tempText);
  }

  // Control servo (only use 0-180 range)
  updateServoByTemperature(value);
}


int last_counter = 0;       // Last counter value
int counter = 0;            // Rotation counter (used to record direction)
int currentStateCLK;        // Current CLK pin state
int lastStateCLK;           // Previous CLK pin state
String currentDir = "";     // Current rotation direction string (for debugging)
bool one_test = false;      // Prevents a single false trigger

// Click to execute function
void performClickAction() {
  // Serial.println("Click");
  current_screen = lv_scr_act();
  if (current_screen == ui_Screen1) {
    if (screen1_index == 0) {
      _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen2_screen_init);

    } else if (screen1_index == 1) {
      _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen3_screen_init);

    } else if (screen1_index == 2) {
      _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen4_screen_init);
    }
  }
}

//Double click to execute function
void performDoubleClickAction() {
  // Serial.println("DoubleClick");
  current_screen = lv_scr_act();
  if (current_screen == ui_Screen2 || current_screen == ui_Screen3 || current_screen == ui_Screen4) {
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen1_screen_init);
  }
}
/*
Functionality:
  1. Read the rotation direction and steps of the rotary encoder
  2. Detect single and double click events of the button
  3. Update the corresponding UI based on the current screen (volume, temperature, brightness progress bars and values)
  4. Control backlight brightness (when adjusting brightness)
*/
void encTask(void *pvParameters) {
  while (1) {

    // Read the current state of CLK
    currentStateCLK = digitalRead(ENCODER_A_PIN);   // CLK: 45

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentStateCLK != lastStateCLK && currentStateCLK == 1) {      // Detect CLK pin state change (rotation), current state is HIGH (rising edge)
      current_screen = lv_scr_act();
      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      // Read DT pin state to determine rotation direction --- DT: 42  
      if (digitalRead(ENCODER_B_PIN) != currentStateCLK) {          // Clockwise rotation (note: here DT and CLK states are different, CLK is HIGH, DT is LOW)
        if (abs(last_counter - counter) > 200) {                     // Limit rotation speed: if the interval between two rotations is too short (counter difference > 10), skip
          continue;
        }
        position_tmp = 1;             // Mark as clockwise

        if (current_screen == ui_Screen2) {
          int currentVol = lv_arc_get_value(ui_VolumeArc);            // Get current volume value from the dial
          Serial.printf(" ++ currentVol = %d\n", currentVol);
          int newVol = (currentVol + 5) > 100 ? 100 : currentVol + 5; // Increase by 5, not exceeding 100
          Serial.printf(" ++ END currentVol = %d\n", newVol);
          lv_arc_set_value(ui_VolumeArc, newVol);

          volumeValue = newVol; // Sync global volume

          char volText[8];
          if (newVol == 100) {
            snprintf(volText, sizeof(volText), "%d%%", newVol);
            lv_label_set_text(ui_VolNum, volText);
          } else {
            snprintf(volText, sizeof(volText), " %d%%", newVol);
            lv_label_set_text(ui_VolNum, volText);
          }

          // Send volume command to Advance device
          if (newVol != lastVolumeSent) {
            sendVolumeCommand(newVol);
            lastVolumeSent = newVol;
          }
          
        } else if (current_screen == ui_Screen3) {
          int currentTemp = lv_arc_get_value(ui_TempArc);
          Serial.printf(" ++ currentVol = %d\n", currentTemp);
          int newTemp = (currentTemp + 5) > 200 ? 200 : currentTemp + 5;
          Serial.printf(" ++ END currentVol = %d\n", newTemp);
          lv_arc_set_value(ui_TempArc, newTemp);
          char TempText[8];
          if (newTemp >= 100 && newTemp <= 200) {
            snprintf(TempText, sizeof(TempText), " %d%°", newTemp);
            lv_label_set_text(ui_TempNum, TempText);
          } else {
            snprintf(TempText, sizeof(TempText), "  %d%°", newTemp);
            lv_label_set_text(ui_TempNum, TempText);
          }

          // Update servo
          updateServoByTemperature(newTemp);

        } else if (current_screen == ui_Screen4) {
          int currentLight = lv_arc_get_value(ui_lightArc);
          Serial.printf(" ++ currentLight = %d\n", currentLight);
          int newLight = (currentLight + 5) > 100 ? 100 : currentLight + 5;
          Serial.printf(" ++ END currentLight = %d\n", newLight);
          lv_arc_set_value(ui_lightArc, newLight);

          lightValue = newLight; // Sync global brightness

          char LightText[8];
          if (newLight == 100) {
            snprintf(LightText, sizeof(LightText), "%d%%", newLight);
            lv_label_set_text(ui_LightNum, LightText);
          } else {
            snprintf(LightText, sizeof(LightText), " %d%%", newLight);
            lv_label_set_text(ui_LightNum, LightText);
          }

          int pwm_value = (newLight * 255) / 100;
          ledcSetup(pwmChannel, pwmFreq, pwmResolution);
          ledcAttachPin(SCREEN_BACKLIGHT_PIN, pwmChannel);
          ledcWrite(pwmChannel, pwm_value);
        }

        counter++;                // Increment counter
        currentDir = "CCW";       // Record direction (counterclockwise)
      } else {                    // Counterclockwise rotation (CLK HIGH, DT also HIGH)
        if (one_test == false) 
        {
          one_test = true;
          continue;
        }

        if (current_screen == ui_Screen2) {
          int currentVol = lv_arc_get_value(ui_VolumeArc);
          Serial.printf(" -- currentVol = %d\n", currentVol);
          int newVol = (currentVol - 5) < 0 ? 0 : currentVol - 5;
          Serial.printf(" -- END currentVol = %d\n", newVol);
          lv_arc_set_value(ui_VolumeArc, newVol);

          volumeValue = newVol; // Sync global volume

          char volText[8];
          if (newVol == 100) {
            snprintf(volText, sizeof(volText), "%d%%", newVol);
            lv_label_set_text(ui_VolNum, volText);
          } else {
            snprintf(volText, sizeof(volText), " %d%%", newVol);
            lv_label_set_text(ui_VolNum, volText);
          }

          // Send volume command to Advance device
          if (newVol != lastVolumeSent) {
            sendVolumeCommand(newVol);
            lastVolumeSent = newVol;
          }

        } else if (current_screen == ui_Screen3) {
          int currentTemp = lv_arc_get_value(ui_TempArc);
          Serial.printf(" -- currentVol = %d\n", currentTemp);
          int newTemp = (currentTemp - 5) < 0 ? 0 : currentTemp - 5;
          Serial.printf(" -- END currentVol = %d\n", newTemp);
          lv_arc_set_value(ui_TempArc, newTemp);

          char TempText[8];
          if (newTemp >= 100 && newTemp <= 200) {
            snprintf(TempText, sizeof(TempText), " %d%°", newTemp);
            lv_label_set_text(ui_TempNum, TempText);
          } else {
            snprintf(TempText, sizeof(TempText), "  %d%°", newTemp);
            lv_label_set_text(ui_TempNum, TempText);
          }

          // Update servo
          updateServoByTemperature(newTemp);

        } else if (current_screen == ui_Screen4) {
          int currentLight = lv_arc_get_value(ui_lightArc);
          Serial.printf(" -- currentLight = %d\n", currentLight);
          int newLight = (currentLight - 5) < 0 ? 0 : currentLight - 5;
          Serial.printf(" -- END currentLight = %d\n", newLight);
          lv_arc_set_value(ui_lightArc, newLight);

          lightValue = newLight; // Sync global brightness

          char LightText[8];
          if (newLight == 100) {
            snprintf(LightText, sizeof(LightText), "%d%%", newLight);
            lv_label_set_text(ui_LightNum, LightText);
          } else {
            snprintf(LightText, sizeof(LightText), " %d%%", newLight);
            lv_label_set_text(ui_LightNum, LightText);
          }
          
          int pwm_value = (newLight * 255) / 100;
          ledcSetup(pwmChannel, pwmFreq, pwmResolution);
          ledcAttachPin(SCREEN_BACKLIGHT_PIN, pwmChannel);
          ledcWrite(pwmChannel, pwm_value);
        }

        position_tmp = 0;       // Mark as counterclockwise
        counter--;              // Decrement counter
        currentDir = "CW";
      }

      Serial.print("Direction: ");
      Serial.print(currentDir);
      Serial.print(" | Counter: ");
      Serial.println(counter);
      last_counter = counter;     // Record last counter value (for speed limit)
      processEncoder();           // Handle rotation event (update main screen UI)
    }

    // Remember last CLK state
    lastStateCLK = currentStateCLK;
    if (clickCount == 1 && millis() - lastPressTime > doubleClickTime) {
      Serial.println("click ");
      performClickAction();
      clickCount = 0; 
    }
    if (clickCount >= 2) {
      Serial.println("double click");
      performDoubleClickAction();
      clickCount = 0; 
    }
    if (clickCount > 2) {
      clickCount = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}


void processEncoder() {
  current_screen = lv_scr_act();
  if (current_screen == ui_Screen1) {
    if (position_tmp == 1) {  
      if (screen1_index < 2) {
        screen1_index++;
      }

      Serial.printf("cur_index: %d\n", screen1_index);

    } else if (position_tmp == 0) { 
      if (screen1_index > 0) {
        screen1_index--;
      }

      Serial.printf("cur_index: %d\n", screen1_index);
    }
    updateScreen(screen1_index);
    position_tmp = -1; 
  }
}

void updateScreen(int index) {
  if (index < 0) {
    index = 0;
  } else if (index > 2) {
    index = 2;
  }
  Serial.printf("cur_index: %d\n", screen1_index);

  lv_obj_add_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_lightBlue, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);

  switch (index) {
    case 0:  // Volume
      // volume
      lv_obj_clear_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_volumeTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_volumeBlue, 0);
      lv_obj_set_x(ui_volumeTextBlue, 0);
      lv_obj_set_x(ui_volumeWhite, 0);
      lv_obj_set_x(ui_volumeTextWhite, 0);

      // temp
      lv_obj_clear_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_tempTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_tempBlue, 70);
      lv_obj_set_x(ui_tempTextBlue, 70);
      lv_obj_set_x(ui_tempWhite, 70);
      lv_obj_set_x(ui_tempTextWhite, 70);

      // light
      lv_obj_add_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lightTextWhite, LV_OBJ_FLAG_HIDDEN);
      break;

    case 1:  // Temperature
      // volume
      lv_obj_clear_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_volumeTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_volumeBlue, -70);
      lv_obj_set_x(ui_volumeTextBlue, -70);
      lv_obj_set_x(ui_volumeWhite, -70);
      lv_obj_set_x(ui_volumeTextWhite, -70);

      //temp
      lv_obj_add_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_tempTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_tempBlue, 0);
      lv_obj_set_x(ui_tempTextBlue, 0);
      lv_obj_set_x(ui_tempWhite, 0);
      lv_obj_set_x(ui_tempTextWhite, 0);

      //light
      lv_obj_add_flag(ui_lightBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lightTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_lightTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_lightBlue, 70);
      lv_obj_set_x(ui_lightTextBlue, 70);
      lv_obj_set_x(ui_lightWhite, 70);
      lv_obj_set_x(ui_lightTextWhite, 70);
      break;

    case 2:  // Light
      // volume
      lv_obj_add_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextBlue, LV_OBJ_FLAG_HIDDEN);

      // temp
      lv_obj_add_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_tempTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_tempBlue, -70);
      lv_obj_set_x(ui_tempTextBlue, -70);
      lv_obj_set_x(ui_tempWhite, -70);
      lv_obj_set_x(ui_tempTextWhite, -70);

      // light
      lv_obj_add_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lightTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_lightBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_lightTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_lightBlue, 0);
      lv_obj_set_x(ui_lightTextBlue, 0);
      lv_obj_set_x(ui_lightWhite, 0);
      lv_obj_set_x(ui_lightTextWhite, 0);
      break;
  }
}

/* Display flushing */
// 将LVGL的绘图区域通过DMA推送到屏幕
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  if (gfx.getStartCount() > 0) {
    gfx.endWrite();
  }
  gfx.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (lgfx::rgb565_t *)&color_p->full);

  lv_disp_flush_ready(disp);
}

/*Read the touchpad*/
// 读取触摸屏的状态和坐标，提供给LVGL
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  bool touched;
  uint8_t gesture;
  uint16_t touchX, touchY;

  touched = touch.getTouch(&touchX, &touchY, &gesture);

  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;

    /*Set the coordinates*/
    data->point.x = touchX;
    data->point.y = touchY;
  }
}

void initBacklight() {
  ledcSetup(pwmChannel, pwmFreq, pwmResolution);
  ledcAttachPin(SCREEN_BACKLIGHT_PIN, pwmChannel);
  ledcWrite(pwmChannel, (50 * 255) / 100);
}

uint8_t ledCount = 0;
int8_t ledBrightness = 0;
void ledTestTask(void *pvParameters) {
  while (1) {
    led.clear();
    led.show();
    //Five circles of white flowing water lights
    while (ledCount++ < 5) {
      for (int i = 0; i < 5; i++) {
        led.setPixelColor(i, led.Color(255, 255, 255));
        led.show();
        vTaskDelay(pdMS_TO_TICKS(250));
        led.clear();
        led.show();
      }
    }
    ledCount = 0;

    //All the lights flash in rapid succession in various colors simultaneously.
    for (int i = 0; i < 5; i++) {
      led.setPixelColor(0, led.Color(255, 0, 0));
      led.setPixelColor(1, led.Color(0, 255, 0));
      led.setPixelColor(2, led.Color(0, 0, 255));
      led.setPixelColor(3, led.Color(255, 255, 0));
      led.setPixelColor(4, led.Color(130, 0, 255));
      led.show();
      vTaskDelay(pdMS_TO_TICKS(100));
      led.clear();
      led.show();
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    //Colorful flowing lights in 5 circles
    while (ledCount < 5) {
      for (int i = 0; i < 5; i++) {
        led.clear();
        switch (i) {
          case 0: led.setPixelColor(i, led.Color(255, 0, 0)); break;
          case 1: led.setPixelColor(i, led.Color(0, 255, 0)); break;
          case 2: led.setPixelColor(i, led.Color(0, 0, 255)); break;
          case 3: led.setPixelColor(i, led.Color(255, 255, 0)); break;
          case 4: led.setPixelColor(i, led.Color(130, 0, 255)); break;
        }
        led.show();
        vTaskDelay(pdMS_TO_TICKS(250));
      }
      ledCount++;
    }
    ledCount = 0;

    //All the lights flash in a slow, colored pattern simultaneously.
    for (int i = 0; i < 5; i++) {
      led.setPixelColor(0, led.Color(255, 0, 0));
      led.setPixelColor(1, led.Color(0, 255, 0));
      led.setPixelColor(2, led.Color(0, 0, 255));
      led.setPixelColor(3, led.Color(255, 255, 0));
      led.setPixelColor(4, led.Color(130, 0, 255));
      led.show();
      vTaskDelay(pdMS_TO_TICKS(250));
      led.clear();
      led.show();
      vTaskDelay(pdMS_TO_TICKS(250));
    }

    //Colorful breathing light, breathing 5 times
    led.setPixelColor(0, led.Color(255, 0, 0));
    led.setPixelColor(1, led.Color(0, 255, 0));
    led.setPixelColor(2, led.Color(0, 0, 255));
    led.setPixelColor(3, led.Color(255, 255, 0));
    led.setPixelColor(4, led.Color(130, 0, 255));
    while (ledCount++ < 10) {
      for (ledBrightness = 0; ledBrightness <= 25; ledBrightness++) {
        led.setBrightness(ledBrightness);
        led.setPixelColor(0, led.Color(255, 0, 0));
        led.setPixelColor(1, led.Color(0, 255, 0));
        led.setPixelColor(2, led.Color(0, 0, 255));
        led.setPixelColor(3, led.Color(255, 255, 0));
        led.setPixelColor(4, led.Color(130, 0, 255));
        led.show();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      for (; ledBrightness >= 0; ledBrightness--) {
        led.setBrightness(ledBrightness);
        led.show();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      ledCount++;
    }

    ledCount = 0;
    ledBrightness = 0;
    led.setBrightness(25);
    led.clear();
    led.show();

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
