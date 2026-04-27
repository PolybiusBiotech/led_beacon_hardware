
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_MCP23X17.h>
#include <esp_dmx.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// OTA

/** Settings **/
const uint32_t led_count = 10;
const uint32_t slot_count = led_count * 2; // 2 slots per LED
const uint32_t ctrl_loop = 5000; // control loop duration in ms
const uint32_t hold_last = 4000; // how long to keep last value in ms
const int32_t warning_temp = 500000; // temp to disable LEDs in millicelcius

/** Pin Map **/
#define PIN_TEMP_MON  (0)
#define PIN_VCC_MON   (1)
#define PIN_SDA       (2)
#define PIN_SCL       (3)
#define PIN_I2C_INT   (4)
#define PIN_WIFI_EN   (5)
#define PIN_DMX_EN    (6)
#define PIN_LED_OE    (7)
#define PIN_LED       (8)
//#define PIN_DMX_RX   (20)
//#define PIN_DMX_TX   (21)
// ESP32-C3-DEVKITM-1 uses a USB↔Serial converter attached to IO20/21 not USB on IO18/19!
#define PIN_DMX_RX   (19)
#define PIN_DMX_TX   (18)

/** Hardware instantiations  **/
Adafruit_PWMServoDriver leds = Adafruit_PWMServoDriver(0x40);
Adafruit_MCP23X17 mcp;
hw_timer_t *led_timer = NULL;
dmx_port_t dmx_num = DMX_NUM_1;
AsyncWebServer server(80);
// OTA
uint32_t last_ctrl_update = 0x00;
uint32_t last_valid_dmx;
int32_t current_temp = 0x00, min_temp, max_temp;
uint32_t current_volts = 0x00, min_volts, max_volts;
uint32_t current_uptime = 0x00;
uint32_t dmx_address = 0x00;
uint8_t dmx_data[slot_count] = {0x00};
uint16_t led_brightness[led_count] = {0x00};
uint32_t led_half_period[led_count] = {0x00};
volatile uint32_t led_strobe_cnt[led_count] = {0x00};
volatile bool led_state[led_count] = {false};
volatile bool led_update_req[led_count] = {false};
bool wifi_ap_active = false;



// put function declarations here:
int32_t get_temp(void);
uint32_t get_volts(void);
uint16_t map_led_brightness(uint8_t);
uint32_t map_led_period(uint8_t);
void onTimer(void);

char init_string[] = "\r\nLED Beacon\r\nCompiled on " __DATE__ " " __TIME__ "\r\n";

void setup(void) {
  delay(2000);

  // init Serial
  Serial.begin(115200);
  Serial.write(init_string);

  // init GPIO
  pinMode(PIN_I2C_INT, INPUT);
  pinMode(PIN_LED_OE, OUTPUT);
  pinMode(PIN_DMX_EN, OUTPUT);
  pinMode(PIN_WIFI_EN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED_OE, HIGH);
  digitalWrite(PIN_DMX_EN, LOW);
  digitalWrite(PIN_LED, LOW);
  analogSetAttenuation(ADC_11db);
  Serial.println("GPIO initialised...");

  // init Timer
  led_timer = timerBegin(0, 80, true); // 80Mhz/80 = 1M ticks/sec for 1us resolution
  timerAttachInterrupt(led_timer, &onTimer, true);
  timerAlarmWrite(led_timer, 1000, true); // trigger every 1000us
  timerAlarmEnable(led_timer);
  Serial.println("Timer initialised...");

  // init I2C
  Wire.begin(PIN_SDA, PIN_SCL);
  //Wire.setClock(1000000);
  Wire.setClock(400000); // because of crap breadboard
  Serial.println("I²C initialised...");

  /*
  uint8_t error;
  Serial.println("\r\nScanning for I²C devices...");
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I²C device found at 0x");
      if (address < 0x10) Serial.print("0");
      Serial.println(address, HEX);
    } else if (error == 4) {
      Serial.print("Unknown error at 0x");
      if (address < 0x10) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  Serial.println("Finished scanning for I²C devices!\r\n");
  */

  // init MCP23017
  mcp.begin_I2C(0x20, &Wire);
  mcp.setupInterrupts(true, true, LOW);
  for(int i = 0; i < 16; i++) {
    mcp.pinMode(i, INPUT_PULLUP);
    if (i == 7 or i > 9) continue; // only configures 0→6 & 8→9
    mcp.setupInterruptPin(i, CHANGE);
  }
  Serial.println("MCP23017 initialised...");

  // init PCA9685
  leds.begin();
  leds.setPWMFreq(1000);
  leds.setOutputMode(true); // LEDs driven by external NMOS
  for (int i = 0; i < led_count; i++) leds.setPWM(i, 410*i, 0);
  digitalWrite(PIN_LED_OE, LOW); // enable LEDs!
  Serial.println("PCA9685 initialised...");

  // init DMX
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  static dmx_personality_t personalities[] {
    {20, "10-Light Mode"} // 20 slots, custom name
  };
  int personality_count = 1;
  dmx_driver_install(dmx_num, &config, personalities, personality_count);
  dmx_set_pin(dmx_num, PIN_DMX_TX, PIN_DMX_RX, PIN_DMX_EN);
  Serial.println("DMX initialised...");
  
  // init Watchdog
  
  // init WiFi
  WiFi.softAP("LED_Beacon_Ctrl", "password123");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP Address: ");
  Serial.println(IP);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Request received!");
    String html = "<h1>LED Beacon Diagnostics</h1>";
    html += "<p>DMX Address: " + String(dmx_address) + "</p>";
    html += "<p>VCC: " + String(((float)current_volts/1000), 2) + "V</p>";
    html += "<p>Temp: " + String(((float)current_temp/1000), 2) + "&deg;C</p>";
    html += "<p>Uptime: " + String(current_uptime) + "s</p>";
    html += "<p>Last DMX packet received: " + String(last_valid_dmx/1000, 0) + "s</p>";
    request->send(200, "text/html", html); 
  });
  server.begin();

  // init OTA

  // set starting min & max
  current_temp = get_temp();
  min_temp = current_temp;
  max_temp = current_temp;
  current_volts = get_volts();
  min_volts = current_volts;
  max_volts = current_volts;

  Serial.println("Setup complete!\r\nStarting loop...");
}

void loop(void) {
  
  // Get DMX packet and update LEDs
  dmx_packet_t packet;
  if (dmx_receive(dmx_num, &packet, 0)) {
    if (!packet.err) {
      dmx_read_offset(dmx_num, dmx_address, dmx_data, slot_count);
      last_valid_dmx = millis();
      //dmxSignalLost = false;

      uint16_t new_brightness;
      uint32_t new_half_period;
      for (int i = 0; i < led_count; i++) {
        new_brightness = map_led_brightness(dmx_data[i]);
        if (led_brightness[i] != new_brightness) {
          led_brightness[i] = new_brightness;
          led_update_req[i] = true;
        }

        new_half_period = map_led_period(dmx_data[i + led_count]);
        if (led_half_period[i] != new_half_period) {
          led_half_period[i] = new_half_period;
          if (new_half_period == 0) {
            // force an update if LED is to be fully on
            led_update_req[i] = true;
          }
        }          
      }
    }
  }

  // handle DMX timeout
  if (millis() - last_valid_dmx > hold_last) {
    for (int i = 0; i < led_count; i++) {
      if (led_brightness[i] != 0x00) {
        led_brightness[i] = 0x00;
        led_half_period[i] = 0x00;
        led_update_req[i] = true;
        led_State[i] = true;
      }
    }
  }

  // update LEDs
  for (int i = 0; i < led_count; i++) {
    if (led_update_req[i]) {
      led_update_req[i] = false;

      uint16_t on_time = 410 * i;
      uint16_t off_time;
      
      if (led_state[i] && led_brightness[i] > 0x00) {
        if(led_brightness[i] == 4095) {
          // special PCA9685 'always on' mode
          on_time = 4096;
          off_time = 0;
        } else {
          off_time = (on_time + led_brightness[i]) % 4096;
        }
      } else {
        // 'always off' mode
        off_time = on_time;
      }
     
      leds.setPWM(i, on_time, off_time);
    }
  }
  
  if (millis() - last_ctrl_update >= ctrl_loop) {
    last_ctrl_update = millis();

    //enable/disable WiFi AP
    
    bool wifi_enable = ~digitalRead(PIN_WIFI_EN);
    /*
    if (wifi_enable && !wifi_ap_active) {
      WiFi.softAP("LED_Beacon_Ctrl", "password123");
      server.begin();
      apActive = true;
    } 
    else if (!wifi_enable && wifi_ap_active) {
      server.end();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      apActive = false;
    }
    */

    // get current VCC in and temperature
    current_volts = get_volts();
    current_temp = get_temp();

    // update min/max temp/volts    
    if (current_temp > -50000 && current_temp < 150000) {
      // but only if temp is between -50°C and 150°C
      min_temp = min(min_temp, current_temp);
      max_temp = max(max_temp, current_temp);
    }
    min_volts = min(min_volts, current_volts);
    max_volts = max(max_volts, current_volts);

    // dealing with excessive temp
    if (current_temp >= warning_temp) {
      digitalWrite(PIN_LED_OE, HIGH); // disable LEDs while too hot!
    } else if (current_temp <= (warning_temp - 5000)) {
      digitalWrite(PIN_LED_OE, LOW); // enable LEDs now it's cooler!
    }
    
    // not sure what actions to take on:      
      // under voltage
      // over voltage
    
    current_uptime = esp_timer_get_time() / 1000000;
    // uptime in seconds is hard to digest, may need a function to make it pretty e.g. minutes/hours/days
        
    // check DMX address
    if (digitalRead(PIN_I2C_INT) == LOW) {
      uint16_t dmx_adr_switches = ~mcp.readGPIOAB(); // read and flip switch states from ACTIVE_LOW to ACTIVE_HIGH
      dmx_adr_switches = (dmx_adr_switches & 0x7F) | ((dmx_adr_switches & 0x0300) >> 1);  // keep 0→6 & 8→9 and concatenate to remove 7
      dmx_address = dmx_adr_switches + 1; // offset and save DMX address
    }

    // update USB status
      
  }

}

void IRAM_ATTR onTimer(void) {
  for (int i = 0; i < 10; i++) {
    
    if (led_half_period[i] == 0) {
      if (!led_state[i]) { 
        led_state[i] = true;
        led_update_req[i] = true;
      }
      led_strobe_cnt[i] = 0; // Keep counter reset
      continue;
    }

    led_strobe_cnt[i]++;
    if (led_strobe_cnt[i] >= led_half_period[i]) {
      led_strobe_cnt[i] = 0;
      led_state[i] = !led_state[i];
      led_update_req[i] = true;
    }
  }
}

/** 8bit to 12bit gamma look up table
 * uint16_t = (uint8_t / (2^8 - 1)) ^ 2.8 * (2^12 - 1)
 */
static const uint16_t gamma12_table[] PROGMEM = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11,
    12, 13, 15, 16, 17, 18, 20, 21, 23, 25, 26, 28, 30, 32, 34, 36,
    38, 40, 43, 45, 48, 50, 53, 56, 59, 62, 65, 68, 71, 75, 78, 82,
    85, 89, 93, 97, 101, 105, 110, 114, 119, 123, 128, 133, 138, 143, 149, 154,
    159, 165, 171, 177, 183, 189, 195, 202, 208, 215, 222, 229, 236, 243, 250, 258,
    266, 273, 281, 290, 298, 306, 315, 324, 332, 341, 351, 360, 369, 379, 389, 399,
    409, 419, 430, 440, 451, 462, 473, 485, 496, 508, 520, 532, 544, 556, 569, 582,
    594, 608, 621, 634, 648, 662, 676, 690, 704, 719, 734, 749, 764, 779, 795, 811,
    827, 843, 859, 876, 893, 910, 927, 944, 962, 980, 998, 1016, 1034, 1053, 1072, 1091,
    1110, 1130, 1150, 1170, 1190, 1210, 1231, 1252, 1273, 1294, 1316, 1338, 1360, 1382, 1404, 1427,
    1450, 1473, 1497, 1520, 1544, 1568, 1593, 1617, 1642, 1667, 1693, 1718, 1744, 1770, 1797, 1823,
    1850, 1877, 1905, 1932, 1960, 1988, 2017, 2045, 2074, 2103, 2133, 2162, 2192, 2223, 2253, 2284,
    2315, 2346, 2378, 2410, 2442, 2474, 2507, 2540, 2573, 2606, 2640, 2674, 2708, 2743, 2778, 2813,
    2849, 2884, 2920, 2957, 2993, 3030, 3067, 3105, 3143, 3181, 3219, 3258, 3297, 3336, 3376, 3416,
    3456, 3496, 3537, 3578, 3619, 3661, 3703, 3745, 3788, 3831, 3874, 3918, 3962, 4006, 4050, 4095
};

uint16_t map_led_brightness(uint8_t brightness) {
  
  return gamma12_table[brightness];
}

// This table stores the ms delay between TOGGLES (Half-Period)
static const uint32_t strobe_half_period_table[] PROGMEM = {
    0, 2500, 2415, 2336, 2261, 2191, 2126, 2064, 2006, 1951, 1899, 1850, 1803, 1759, 1716, 1676,
    1637, 1600, 1565, 1531, 1499, 1468, 1438, 1409, 1382, 1355, 1329, 1305, 1281, 1258, 1236, 1214,
    1194, 1174, 1154, 1136, 1118, 1100, 1083, 1067, 1051, 1035, 1020, 1006, 992, 978, 965, 952,
    939, 927, 915, 903, 892, 881, 870, 860, 850, 840, 830, 821, 811, 802, 793, 785,
    776, 768, 760, 752, 745, 737, 730, 723, 716, 709, 702, 696, 689, 683, 677, 671,
    665, 659, 653, 647, 642, 636, 631, 626, 621, 616, 611, 606, 601, 597, 592, 588,
    583, 579, 575, 571, 566, 562, 558, 554, 550, 547, 543, 539, 536, 532, 529, 525,
    522, 518, 515, 512, 508, 505, 502, 499, 496, 493, 490, 487, 484, 481, 478, 475,
    472, 470, 467, 464, 462, 459, 456, 454, 451, 449, 446, 444, 441, 439, 437, 434,
    432, 430, 427, 425, 423, 421, 418, 416, 414, 412, 410, 408, 406, 404, 402, 400,
    398, 396, 394, 392, 390, 388, 386, 384, 383, 381, 379, 377, 376, 374, 372, 371,
    369, 367, 366, 364, 362, 361, 359, 358, 356, 355, 353, 352, 350, 349, 348, 346,
    345, 343, 342, 341, 339, 338, 337, 335, 334, 333, 331, 330, 329, 328, 326, 325,
    324, 323, 322, 321, 319, 318, 317, 316, 315, 314, 313, 312, 311, 310, 309, 308,
    306, 305, 304, 303, 302, 301, 300, 299, 298, 297, 296, 295, 294, 293, 292, 291,
    290, 289, 288, 287, 286, 285, 284, 283, 282, 281, 281, 280, 279, 278, 277, 16
};

uint32_t map_led_period(uint8_t period) {

  // FILL ME OUT!
  // freq: 1-30Hz, 0.289-16.667Hz
  // msg: 0 = solid, 1 = slow, 255 = fast
  // DMX 1   = 5.000 seconds
  // DMX 255 = 0.033 seconds
  
  return strobe_half_period_table[period];
}


/** 
 * Lookup table for NCP15XH103F03RC 
 * Based on 10k pull-down (to GND) and 12-bit ADC (0-4095)
 * Format: {ADC_Value, Temp_in_C}
 */
struct TempPoint {
    int16_t adc;
    int16_t temp;
};

// LUT for: 3.3V -> 10k Resistor -> IO0 -> Thermistor + 1uF -> GND
const TempPoint lut[] = {
    {3115, -10}, {2703, 0},  {2263, 10}, {1850, 20}, 
    {1485, 30},  {1176, 40}, {924, 50},  {724, 60}, 
    {568, 70},   {449, 80},  {356, 90},  {286, 100}
};
const size_t LUT_SIZE = sizeof(lut) / sizeof(lut[0]);

int32_t get_temp(void) {

  int32_t raw_temp = 0x00;

  for (int i = 0; i < 8; i++) {
    raw_temp += analogRead(PIN_TEMP_MON);
    delay(1);
  }
  raw_temp /= 8;

  // Failsafe: Thermistor shorted to 3.3V (Vout low)
  if (raw_temp  < 50) return 300000; 
  // Failsafe: Thermistor open/disconnected (Vout high)
  if (raw_temp > 4050) return -300000;

  // Linear Interpolation
  for (size_t i = 0; i < LUT_SIZE - 1; i++) {
    if (raw_temp <= lut[i+1].adc) {
      int32_t x0 = lut[i].adc;
      int32_t x1 = lut[i+1].adc;
      int32_t y0 = lut[i].temp * 1000; // Convert to millidegrees
      int32_t y1 = lut[i+1].temp * 1000;

      // millidegrees = y0 + (raw_temp - x0) * (y1 - y0) / (x1 - x0)
      return y0 + (raw_temp - x0) * (y1 - y0) / (x1 - x0);
    }
  }

  return 300000; // Out of range high
}

uint32_t get_volts(void) {
  const uint32_t lsb_2_uvolts = 5802; // Vadc × ((62kΩ + 10kΩ) ÷ 10kΩ) = VCC
  uint32_t raw_volts = 0x00;

  for (int i = 0; i < 8; i++) {
    raw_volts += analogRead(PIN_VCC_MON);
    delay(1);
  }
  raw_volts /= 8;

  // convert to millivolts
  raw_volts = ((raw_volts * lsb_2_uvolts) + 500) / 1000; // outputs millivolts

  return raw_volts;
}
