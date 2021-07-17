#include <string>

#include <Arduino.h>

#include <Wire.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleStream.h>
#include <ezTime.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#include "settings.hpp"

// Init stuff
ADC_MODE(ADC_VCC);                              // for boot-up Vcc display
Adafruit_SSD1306 display(128, 64, &Wire, -1);   // height, width, lib, reset pin
WiFiClient blynkWiFiClient;     // WiFi client for the Blynk features
Timezone tz;    // eztime timezone obj
char buf[24];   // text output buffer

// Store alarm times
time_t alarmOne;
time_t alarmTwo;

const unsigned char jLogoBitmap [] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x02, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x06,
    0x00, 0xfe, 0x40, 0x00, 0x00, 0x0e, 0x00, 0xfe, 0x60, 0x00, 0x00, 0x1e, 0x00, 0xfe, 0x70, 0x00,
    0x00, 0x3e, 0x00, 0xfe, 0x78, 0x00, 0x00, 0x7e, 0x00, 0xfe, 0x7c, 0x00, 0x00, 0xfe, 0x00, 0xfe,
    0x7e, 0x00, 0x00, 0xfe, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0xfe, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0xfc,
    0x00, 0xfe, 0x7f, 0x00, 0x00, 0xf8, 0x00, 0xfe, 0x3f, 0x00, 0x00, 0xf0, 0x00, 0xfe, 0x1f, 0x00,
    0x00, 0xf8, 0x00, 0xfe, 0x3f, 0x00, 0x00, 0xfc, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0xfe, 0x00, 0xfe,
    0x7f, 0x00, 0x00, 0xfe, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0xfe, 0x00, 0xfe, 0x7e, 0x00, 0x00, 0x7e,
    0x40, 0xfe, 0x7c, 0x00, 0x00, 0x3e, 0x60, 0xfe, 0x78, 0x00, 0x00, 0x1e, 0x70, 0xfe, 0x70, 0x00,
    0x00, 0x0e, 0x78, 0xfe, 0x60, 0x00, 0x00, 0x06, 0x7c, 0xfe, 0x40, 0x00, 0x00, 0x02, 0x7e, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xfe, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x03, 0xf8,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Beep! (+ make screen blink)
void beep() {
    analogWrite(15, 128);
    display.fillScreen(WHITE); display.display();

    delay(100);

    analogWrite(15, 0);
    display.clearDisplay(); display.display();
}

// Alarm
void alarm() {
    // Beeping
    for (int i = 0; i < 20; i++) {
        beep();
        delay(100);
    }

    // Refresh time inputs so next alarm gets added
    Blynk.syncVirtual(V2);
}

time_t parseTimeInput(TimeInputParam t) {
    uint8_t dow = atoi(tz.dateTime("N").c_str());   // day of week of today

    // Loop starting from today, but wrap around
    uint8_t i = dow;
    bool done = false;
    while (!done) {
        // Exit before reaching today again
        uint8_t j = (dow - 1) < 0 ? dow + 6 : dow - 1;
        done = j == i;

        if (t.isWeekdaySelected(i)) {
            // Calc days until next weekday (adapted from Boost lib)
            int8_t delta = i - dow;     // both values use Monday as 1
            if (delta < 0) delta += 7;

            // Return time and exit (if alarm time is not in the past)
            time_t nextAlarm = makeTime(t.getStartHour(), t.getStartMinute(), t.getStartSecond(), tz.day(), tz.month(), tz.year()) + (delta * 86400);
            if ((nextAlarm - tz.now()) > 0) {
                return nextAlarm;
            }
        }

        // Increment i or wrap around to 1 if reached Sunday
        i == 7 ? i = 1 : i++;
    }
}

// Signify updates on screen
void indicateUpdate() {
    // Print 'U' on bottom left of display
    display.setCursor(0, 56);
    display.setTextSize(1);
    display.print('U');
    display.display();

    delay(50);

    // Clear
    display.setCursor(0, 56);
    display.print(' ');
    display.display();
}

// Show alarm status on screen
void indicateAlarmOn(bool alarmOneOn, bool alarmTwoOn) {
    // Clear the space
    display.setTextSize(1);
    display.setCursor(116, 56);
    display.print("  ");

    // Print alarms enabled on bottom right of display
    if (alarmOneOn) {
        display.setCursor(116, 56);
        display.print('1');
    }
    if (alarmTwoOn) {
        display.setCursor(122, 56);
        display.print('2');
    }
    display.display();
}

// --- BLYNK METHODS ---

// Alarm time inputs
BLYNK_WRITE(V0) {
    alarmOne = parseTimeInput((TimeInputParam) param);
    indicateUpdate();
}
BLYNK_WRITE(V1) {
    alarmTwo = parseTimeInput((TimeInputParam) param);
    indicateUpdate();
}

// Alarm on/off buttons
BLYNK_WRITE(V2) {
    // Clear all old alarms and sync alarm time inputs
    deleteEvent(alarm);
    Blynk.syncVirtual(V0, V1);

    switch (param.asInt()) {
        // off
        case 1: {
            indicateAlarmOn(false, false);
            break;
        }
        // alarm 1
        case 2: {
            setEvent(alarm, alarmOne);
            indicateAlarmOn(true, false);
            break;
        }
        // alarm 2
        case 3: {
            setEvent(alarm, alarmTwo);
            indicateAlarmOn(false, true);
            break;
        }
        // both
        case 4: {
            setEvent(alarm, alarmOne);
            setEvent(alarm, alarmTwo);
            indicateAlarmOn(true, true);
            break;
        }
    }

    indicateUpdate();
}

// Display Vcc
BLYNK_READ(V20) {
    Blynk.virtualWrite(V20, system_get_vdd33() / 1000.0);
}

// Beep button
BLYNK_WRITE(V98) {
    // Only trigger on push, not release
    if (param.asInt()) {
        beep();
        Blynk.syncVirtual(V2);
    }
}

// Reset button
BLYNK_WRITE(V99) {
    ESP.reset();
}

void setup() {
    if (settings::DEBUG) beep();

    // Set initial display settings
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // init display w/ I2C address
    display.display();          // clear splash screen
    display.clearDisplay();
    display.setTextSize(1);     // text settings
    display.setTextColor(WHITE, BLACK);
    display.setCursor(0, 0);

    // Display logo
    display.drawBitmap(40, 12, jLogoBitmap, 48, 48, WHITE);
    display.display();

    if (settings::DEBUG) {
        // Display voltage
        sprintf(buf, "Vcc: %.2fV", system_get_vdd33() / 1000.0);
        display.println(buf);
    }

    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings::WIFI_SSID, settings::WIFI_PASSWORD);

    // Display WiFi connecting
    display.setCursor(0, 0);
    display.println(String(F("Connecting to: ")) + settings::WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        display.print('.');
        display.display();
        delay(250);
    }

    // Init ezTime
    waitForSync();
    tz.setLocation(settings::TIMEZONE);
    tz.setDefault();
    setInterval(900);   // query NTP every 15 minutes

    // Init Blynk
    blynkWiFiClient.stop();
    blynkWiFiClient.connect(BLYNK_DEFAULT_DOMAIN, BLYNK_DEFAULT_PORT);
    Blynk.begin(blynkWiFiClient, settings::BLYNK_AUTH_TOKEN);
    Blynk.virtualWrite(V21, WiFi.SSID());   // push SSID to Blynk
    Blynk.syncVirtual(V2);  // init alarms

    // Display IP, Vcc, freq
    if (settings::DEBUG) {
        display.clearDisplay();
        display.setCursor(0, 48);
        display.println(F("Connected."));
        display.println(String("IP: ") + WiFi.localIP().toString());
    }

    display.display();  // display all of above

    delay(1000);

    display.clearDisplay(); display.display();  // clear splash stuff
}

void loop() {
    // Run ezTime events
    events();

    // Run Blynk events
    Blynk.run();

    // Display time
    if (secondChanged()) {
        // Print date
        display.setCursor(32, 0);
        display.setTextSize(1);
        display.println(tz.dateTime("D M j, Y"));

        // Print time
        display.setCursor(0, 24);
        display.setTextSize(2);
        display.print(tz.dateTime("h:i:s"));
        display.setTextSize(1);
        display.println(tz.dateTime(" A"));

        display.display();  // update display
    }
}
