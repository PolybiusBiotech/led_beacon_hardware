
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
const uint32_t led_loop = 100; // LED loop duration in ms
const uint32_t ctrl_loop = 5000; // control loop duration in ms
const int32_t warning_temp = 500000; // temp to disable LEDs in millicelcius

/** Pin Map **/
#define PIN_TEMP_MON  (0)
#define PIN_VCC_MON   (1)
#define PIN_SDA       (2)
#define PIN_SCL       (3)
#define PIN_I2C_INT   (4)
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
dmx_port_t dmx_num = DMX_NUM_1;
AsyncWebServer server(80);
// OTA
uint32_t last_led_update = 0x00;
uint32_t last_ctrl_update = 0x00;
int32_t current_temp = 0x00, min_temp, max_temp;
uint32_t current_volts = 0x00, min_volts, max_volts;
uint32_t current_uptime = 0x00UL;
uint32_t dmx_address = 0x00;
uint8_t dmxData[slot_count];
uint16_t led_brightness[led_count];
uint8_t led_strobe[led_count];


// put function declarations here:
int32_t get_temp(void);
uint32_t get_volts(void);
uint16_t map_led_brightness(uint8_t);

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
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED_OE, HIGH);
  digitalWrite(PIN_DMX_EN, LOW);
  digitalWrite(PIN_LED, LOW);
  analogSetAttenuation(ADC_11db);
  Serial.println("GPIO initialised...");

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
  
  // DMX stuff
  dmx_packet_t packet;
  if (dmx_receive(dmx_num, &packet, DMX_TIMEOUT_TICK)) {
    if (!packet.err) {
        dmx_read_offset(dmx_num, dmx_address, dmxData, slot_count);
        //lastValidDmxTime = millis();
        //dmxSignalLost = false;

        // check if it's changing a value, if so set a flag for that LED
        // update new value
    }
  }
  
  if (millis() - last_led_update >= led_loop) {
    last_led_update = millis();

    // do led stuff
    for (int i = 0; i < slot_count; i++) {
      if (i < slot_count/2) {
        // check to see if brightness has changed
          // if yes then update and raise 'changed' flag
          // map to 12bit brightness
      } else {
        // check to see if strobe has changed
          // if yes then update and raise 'changed' flag
      }
    }

    // animations

    // how to handle strobes?
    // freq: 1-30Hz, 0.289-16.667Hz
    // msg: 0 = solid, 1 = slow, 255 = fast
    // DMX 1   = 5.000 seconds
    // DMX 255 = 0.033 seconds
    // probably going to have to move this to an interrupt ISR.



  }
  
  if (millis() - last_ctrl_update >= ctrl_loop) {
    last_ctrl_update = millis();

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
    
    // not sure how to handle:      
      // under voltage
      // over voltage
    
    current_uptime = esp_timer_get_time() / 1000000;
    // time seconds is hard to digest, may need a function to make it pretty e.g. minutes/hours/days
        
    // check DMX address
    if (digitalRead(PIN_I2C_INT) == LOW) {
      uint16_t dmx_adr_switches = ~mcp.readGPIOAB(); // read and flip switch states from ACTIVE_LOW to ACTIVE_HIGH
      dmx_adr_switches = (dmx_adr_switches & 0x7F) | ((dmx_adr_switches & 0x0300) >> 1);  // keep 0→6 & 8→9 and concatenate to remove 7
      dmx_address = dmx_adr_switches + 1; // offset and save DMX address
    }

    // update USB status
      
  }

}

/** 8bit to 12bit gamma look up table
 * uint16_t = (uint8_t / (2^8 - 1)) ^ 2.8 * (2^12 - 1)
 */
static const uint16_t gamma12_table[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 8,
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 24, 25, 27,
    29, 31, 33, 35, 37, 39, 42, 44, 47, 49, 52, 55, 58, 61, 64, 68,
    71, 75, 78, 82, 86, 90, 94, 99, 103, 108, 113, 118, 123, 128, 133, 139,
    144, 150, 156, 162, 169, 175, 182, 189, 196, 203, 211, 218, 226, 234, 242, 251,
    260, 269, 278, 287, 297, 307, 317, 327, 338, 348, 359, 370, 382, 393, 405, 417,
    430, 442, 455, 468, 482, 495, 509, 524, 538, 553, 568, 583, 599, 615, 631, 647,
    664, 681, 698, 716, 734, 752, 770, 789, 808, 828, 847, 867, 888, 908, 929, 951,
    972, 994, 1017, 1039, 1063, 1086, 1110, 1134, 1159, 1183, 1209, 1234, 1260, 1287, 1313, 1341,
    1368, 1396, 1424, 1453, 1482, 1511, 1541, 1571, 1601, 1632, 1663, 1695, 1727, 1759, 1792, 1825,
    1858, 1892, 1926, 1961, 1996, 2032, 2068, 2104, 2141, 2178, 2216, 2254, 2292, 2331, 2370, 2410,
    2450, 2490, 2531, 2572, 2614, 2656, 2699, 2742, 2785, 2829, 2873, 2918, 2963, 3008, 3054, 3101,
    3148, 3195, 3243, 3291, 3340, 3389, 3438, 3488, 3539, 3589, 3641, 3693, 3745, 3798, 3851, 3905,
    3959, 4014, 4069, 4095
};

uint16_t map_led_brightness(uint8_t brightness) {
  
  return gamma12_table[brightness];
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
