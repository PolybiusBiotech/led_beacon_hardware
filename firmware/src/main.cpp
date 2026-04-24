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
const uint32_t led_loop = 100;
const uint32_t ctrl_loop = 100;

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
uint32_t currentTemp = 0x00, currentVolts = 0x00;
uint8_t dmxData[slot_count];
uint32_t dmx_address = 0x00;

// put function declarations here:
uint8_t update_analogue(uint32_t*, uint32_t*);

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
  Serial.println("GPIO initialised...");

  // init I2C
  Wire.begin();
  Wire.setClock(1000000);
  Serial.println("I²C initialised...");

  // init MCP23017
  mcp.begin_I2C(0x20, &Wire);
  mcp.setupInterrupts(true, true, LOW);
  for(int i = 0; i < 16; i++) {
    mcp.pinMode(i, INPUT_PULLUP);
    if (i == 7 or i > 9) continue; // only configures 0→6 & 8→9
    mcp.setupInterruptPin(i, CHANGE);
  }

  // init PCA9685
  leds.begin();
  leds.setPWMFreq(1000);
  leds.setOutputMode(true); // LEDs driven by external NMOS
  for (int i = 0; i < led_count; i++) leds.setPWM(i, 410*i, 0);
  digitalWrite(PIN_LED_OE, LOW); // enable LEDs!
  

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
    html += "<p>VCC: " + String(currentVolts, 2) + "V</p>";
    html += "<p>Temp: " + String(currentTemp, 2) + "V</p>";
    request->send(200, "text/html", html); 
  });
  server.begin();

  // init OTA

}

void loop(void) {
  
  // DMX stuff
  dmx_packet_t packet;
  if (dmx_receive(dmx_num, &packet, DMX_TIMEOUT_TICK)) {
    if (!packet.err) {
        dmx_read_offset(dmx_num, dmx_address, dmxData, slot_count);
        //lastValidDmxTime = millis();
        //dmxSignalLost = false;
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
        // check to see if stobe has changed
          // if yes then update and raise 'changed' flag
      }
    }

    // animations

    // how to handle strobes?


  }
  
  if (millis() - last_ctrl_update >= ctrl_loop) {
    last_ctrl_update = millis();

    // do control stuff
    update_analogue(&currentVolts, &currentTemp);

    // update min/max temp/volts

    // optional warnings for:
      // excessive temp
      // under voltage
      // over voltage
        
    // check DMX address
    if (digitalRead(PIN_I2C_INT) == LOW) {
      uint16_t dmx_adr_switches = ~mcp.readGPIOAB(); // read and flip switch states from ACTIVE_LOW to ACTIVE_HIGH
      dmx_adr_switches = (dmx_adr_switches & 0x7F) | ((dmx_adr_switches & 0x0300) >> 1);  // keep 0→6 & 8→9 and concatenate to remove 7
      dmx_address = dmx_adr_switches + 1; // offset and save DMX address
    }

    // update USB status
    
    // update webpage (optional)
   
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



uint8_t update_analogue(uint32_t *voltage, uint32_t *temperature) {
    
    const uint32_t lsb_2_uvolts = 10; // DO THE MATHS
    static const uint16_t ntc_adc_table[] =  {3381, 2990, 2530, 2056, 1618, 1243, 943, 711, 536}; // maps from 0°C to 80°C in 10°C steps
    
    uint32_t raw_volts = 0x00;
    uint32_t raw_temp = 0x00;
    
    for (int i = 0; i < 8; i++) {
        raw_volts += analogRead(PIN_VCC_MON);
        raw_temp += analogRead(PIN_TEMP_MON);
    }
    raw_volts /= 8;
    raw_temp /= 8;
    
    // convert volts
    raw_volts = ((raw_volts * lsb_2_uvolts) + 500) / 1000; // outputs millivolts
    
    // convert temp
    if (raw_temp >= ntc_adc_table[0] || raw_temp <= ntc_adc_table[8]) raw_temp = 255000;
    else {
        for (int i = 0; i < 8; i++) {
            if (raw_temp < ntc_adc_table[i] && raw_temp > ntc_adc_table[i+1]) {
                raw_temp = map(raw_temp, ntc_adc_table[i], ntc_adc_table[i+1], i*10000, (i+1)*10000);
            }
        }
    }
    
    // save out to the pointers
    *voltage = raw_volts;
    *temperature = raw_temp;
    
    return 0;
}
