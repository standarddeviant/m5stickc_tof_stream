#include <Arduino.h>
#include <M5StickC.h>
#include <WiFi.h>
#include <ESPmDNS.h>
// #include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "WIFI_SECRETS.h"
// if you don't have WIFI_SECRETS.H, fill in and uncomment the following lines
// and copy it to WIFI_SECRETS.H in the same folder as main.cpp
// const char* ssid = "WIFI_SSID_FROM_WIFI_SECRETS.H";
// const char* password = "WIFI_PASSWORD_FROM_WIFI_SECRETS.H";

#include <VL53L0X.h>
VL53L0X g_tof_sensor;

#define FETCH_PERIOD_MICROS 25000 // 1e6 micros / 40 Hz = 25e3 micros
#define WS_SERVER_PORT 42000
#define WS_SERVER_HOSTNAME "m5c-tof"
WebSocketsServer webSocket = WebSocketsServer(WS_SERVER_PORT);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            {
                Serial.print("We lost one!\n");
            }
            break;
        case WStype_CONNECTED:
            {
                Serial.print("We got one!\n");
            }
            break;
        case WStype_ERROR:
        case WStype_TEXT:
        case WStype_BIN:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        case WStype_PING:
        case WStype_PONG:
            break;
    }
}

volatile int g_do_fetch = 0;
static void fetch_timer_callback(void* arg);
static void fetch_timer_callback(void* arg) {
    // fixme - get timing data?
    g_do_fetch = 1;
}

StaticJsonDocument<4000> g_doc;
#define IMU_MPK_MAX_SZ 4000
char g_mpk_buf[IMU_MPK_MAX_SZ];
size_t g_mpk_sz;

// const char* PARAM_MESSAGE = "message";

void setup() {
    // put your setup code here, to run once:

    // Init Vars
    esp_timer_handle_t fetch_timer;
    esp_timer_create_args_t fetch_timer_args;
    fetch_timer_args.callback = &fetch_timer_callback;
    fetch_timer_args.name = "Fetch-Timer"; /* optional, but helpful when debugging */

    // Serial Init
    Serial.begin(115200);
    Wire.begin(0, 26, 100000); // necessary to properly use I2C with TOF sensor?

    // Basic M5Stick-C Init
    M5.begin();
    M5.Axp.ScreenBreath(8); // turn the screen 0=off, 8=dim
    M5.Lcd.setRotation(3);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.printf("Connecting to WiFi...");

    // WiFi Init
    WiFi.begin(ssid, password);
    while(WiFi.status() != WL_CONNECTED) {
        delay(100);
    }

    // WebSocket Init
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    // DNS Init
    if(!MDNS.begin(WS_SERVER_HOSTNAME)) {
        Serial.println("Error starting mDNS");
    }
    MDNS.addService("ws", "tcp", WS_SERVER_PORT);

    // initialize TOF sensor
	g_tof_sensor.setTimeout(500);
    // g_tof_sensor.setMeasurementTimingBudget(FETCH_PERIOD_MICROS);
    if (!g_tof_sensor.init()) {
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 20);
        M5.Lcd.printf("Failed to detect and initialize TOF sensor!");
        while (1) { delay(1000); }
    }
    g_tof_sensor.startContinuous();

    IPAddress ip = WiFi.localIP();
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.printf("Connect to\n%s:%d",
        WS_SERVER_HOSTNAME, WS_SERVER_PORT);

    // Create and Start Timer
    ESP_ERROR_CHECK(esp_timer_create(&fetch_timer_args, &fetch_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(fetch_timer, FETCH_PERIOD_MICROS));
}

#define FETCH_BUF_LEN 20
uint16_t g_tof[FETCH_BUF_LEN] = { 0 };
uint16_t *g_p_tof;
uint32_t g_since[FETCH_BUF_LEN] = { 0 };
uint32_t *g_p_since;
void fetch_reset_pointers(void) {
    g_p_tof = &(g_tof[0]);
    g_p_since = &(g_since[0]);
}

int g_sample_count = 0;

void loop() {
    static uint32_t loop_count = 0;
    static int64_t tprev = 0;
    int64_t tnow;
    int ret;
    uint16_t distance_mm = 0;

    // put your main code here, to run repeatedly:
    loop_count++;
    webSocket.loop();
    if(1) {
        // get imu fetch time
        tnow = esp_timer_get_time();

        g_do_fetch = 0; // set flag to zero
        
        // initialize pointers if necessary
        if(0 == g_sample_count) {
            fetch_reset_pointers();
        }

        // load accel/gyro data via pointers
        distance_mm = g_tof_sensor.readRangeContinuousMillimeters();
        *(g_p_tof++) = distance_mm;
        // Serial.print(distance_mm); Serial.print("\n");

        // load timing data via pointer
        *(g_p_since++) = (uint32_t)(tnow - tprev);
        tprev = tnow;

        // increase sample count
        g_sample_count++;

        // send packet if necessary
        if(FETCH_BUF_LEN == g_sample_count) {
            // package up data
            int ix;

            // init pointers and json doc
            fetch_reset_pointers();
            g_doc.clear();
            
            // pack TOF data
            JsonArray tof = g_doc.createNestedArray("tof");
            for(ix = 0; ix < FETCH_BUF_LEN; ix++) { tof.add(*(g_p_tof++)); }

            // pack timing data
            JsonArray _micros = g_doc.createNestedArray("micros");
            for(ix = 0; ix < FETCH_BUF_LEN; ix++) { _micros.add(*(g_p_since++)); }

            // serialize json object
            g_mpk_sz = serializeMsgPack(g_doc, &(g_mpk_buf[0]), sizeof(g_mpk_buf));

            // send packet to all clients
            ret = webSocket.broadcastBIN((uint8_t *) &(g_mpk_buf[0]), g_mpk_sz);
            Serial.print("ret="); Serial.print(ret);
            Serial.print(", N="); Serial.print(g_mpk_sz);
            Serial.print(", C="); Serial.print(webSocket.connectedClients());
            Serial.print(", LC="); Serial.print(loop_count);
            Serial.print(", t="); Serial.print(millis());
            Serial.print("\n");

            loop_count = 0;
            
            // reset sample count and timing vars
            g_sample_count = 0;

        } // end send packet 

    } // end fetch sample

} // end loop