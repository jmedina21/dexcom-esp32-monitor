#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <time.h>

const char *ssid = "xxxx";     // Replace with your Wi-Fi SSID
const char *password = "xxxx"; // Replace with your Wi-Fi password

// Define ESP32 Pins for ILI9341
#define TFT_CS 5  // Chip Select
#define TFT_DC 2  // Data/Command
#define TFT_RST 4 // Reset

// Initialize Display
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// North America
const char *dexcomAuthenticateURL = "https://share2.dexcom.com/ShareWebServices/Services/General/AuthenticatePublisherAccount";
const char *dexcomLoginURL = "https://share2.dexcom.com/ShareWebServices/Services/General/LoginPublisherAccountById";
const char *dexcomDataURL = "https://share2.dexcom.com/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues";

// Other Regions
//  const char *dexcomAuthenticateURL = "https://shareous1.dexcom.com/ShareWebServices/Services/General/AuthenticatePublisherAccount";
//  const char *dexcomLoginURL = "https://shareous1.dexcom.com/ShareWebServices/Services/General/LoginPublisherAccountById";
//  const char *dexcomDataURL = "https://shareous1.dexcom.com/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues";

const char *dexcomUsername = "xxxx";
const char *dexcomPassword = "xxxx";

const char *applicationId = "d8665ade-9673-4e27-9ff6-92db4ce13d13"; // This is a constant for the Dexcom API

String accountId = "";
String sessionId = "";

float current_glucose_mgdl = 0;
float previous_glucose_mgdl = 0;
float glucose_diff = 0;

float glucose_mmol = 0;
String trend = "Stable";
String timestamp = "N/A";

unsigned long sessionStartMs = 0;
const unsigned long SESSION_REFRESH_MS = 23UL * 60UL * 60UL * 1000UL; // 23h

// Trend direction mapping
const char *DEXCOM_TREND_DIRECTIONS[] = {
    "None",          // 0 - Unconfirmed
    "DoubleUp",      // 1 - Rapidly rising
    "SingleUp",      // 2 - Rising
    "FortyFiveUp",   // 3 - Slowly rising
    "Flat",          // 4 - Stable
    "FortyFiveDown", // 5 - Slowly falling
    "SingleDown",    // 6 - Falling
    "DoubleDown",    // 7 - Rapidly falling
    "NotComputable", // 8 - Unconfirmed
    "RateOutOfRange" // 9 - Unconfirmed
};

// Trend arrows for display
const char *DEXCOM_TREND_ARROWS[] = {
    "",         // 0 - No data
    "\x18\x18", // 1 - DoubleUp (↑↑)
    "\x18",     // 2 - SingleUp (↑)
    "\x1E",     // 3 - FortyFiveUp (↗)
    "\x1A",     // 4 - Flat (→)
    "\x1F",     // 5 - FortyFiveDown (↘)
    "\x19",     // 6 - SingleDown (↓)
    "\x19\x19", // 7 - DoubleDown (↓↓)
    "?",        // 8 - NotComputable
    "-"         // 9 - RateOutOfRange
};

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Initialize Display
    SPI.begin(23, 19, 18, 5);
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(30, 30);
    tft.print("Glucose Monitor!");

    tft.setCursor(30, 120);
    tft.print("Loading Glucose Data...");

    delay(2000);

    if (authenticateToDexcom() && loginToDexcom())
    {
        fetchGlucoseData();
    }
    else
    {
        Serial.println("Initial Dexcom login failed");
    }
}

void loop()
{
    static unsigned long lastFetchTime = 0;
    static unsigned long lastWiFiCheckTime = 0;
    unsigned long now = millis();

    if (now - lastWiFiCheckTime >= 300000UL)
    {
        checkWiFiConnection();
        lastWiFiCheckTime = now;
    }
    if (now - lastFetchTime >= 300000UL)
    {
        fetchGlucoseData();
        lastFetchTime = now;
    }
}

bool authenticateToDexcom()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(dexcomAuthenticateURL);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"accountName\":\"" + String(dexcomUsername) + "\",\"password\":\"" + String(dexcomPassword) + "\",\"applicationId\":\"" + String(applicationId) + "\"}";
    int code = http.POST(payload);
    if (code == HTTP_CODE_OK)
    {
        accountId = http.getString();
        accountId.replace("\"", "");
        http.end();
        return true;
    }
    http.end();
    return false;
}

bool loginToDexcom()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(dexcomLoginURL);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"accountId\":\"" + accountId + "\",\"password\":\"" + String(dexcomPassword) + "\",\"applicationId\":\"" + String(applicationId) + "\"}";
    int code = http.POST(payload);
    if (code == HTTP_CODE_OK)
    {
        sessionId = http.getString();
        sessionId.replace("\"", "");
        sessionStartMs = millis();
        http.end();
        return true;
    }
    http.end();
    return false;
}

bool refreshSession()
{
    sessionId = "";
    // Some deployments require re-auth before login after long idle
    if (!authenticateToDexcom())
        return false;
    return loginToDexcom();
}

void fetchGlucoseData()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    // Proactive refresh before the 24h cutoff
    if (sessionId == "" || (millis() - sessionStartMs) >= SESSION_REFRESH_MS)
    {
        if (!refreshSession())
        {
            Serial.println("Session refresh failed");
            return;
        }
    }

    bool attemptedRefresh = false;

RETRY_FETCH:
    if (sessionId == "")
        return;

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(dexcomDataURL);
    http.addHeader("Content-Type", "application/json");

    String fetchPayload = "{\"sessionId\":\"" + sessionId + "\",\"minutes\":1440,\"maxCount\":4}";
    int httpCode = http.POST(fetchPayload);
    String payload = (httpCode > 0) ? http.getString() : "";
    http.end();

    bool bodyInvalid = payload.indexOf("SessionNotValid") != -1 || payload.indexOf("SessionId") != -1;

    if (httpCode == HTTP_CODE_OK && !bodyInvalid)
    {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, payload);
        if (!err && doc.size() >= 3)
        {
            int difIndex = checkDiff(doc) ? 2 : 1;
            previous_glucose_mgdl = doc[difIndex]["Value"];
            current_glucose_mgdl = doc[0]["Value"];
            glucose_mmol = current_glucose_mgdl / 18.0;
            trend = String(doc[0]["Trend"]);
            timestamp = formatTimestamp(doc[0]["DT"]);
            glucose_diff = current_glucose_mgdl - previous_glucose_mgdl;
            updateDisplay();
            return;
        }
        else
        {
            Serial.print("JSON parse error: ");
            Serial.println(err.c_str());
            return;
        }
    }
    else
    {
        // On auth/session errors, refresh once then retry
        if (!attemptedRefresh && (httpCode == 401 || httpCode == 403 || httpCode >= 500 || bodyInvalid))
        {
            Serial.printf("Session likely expired (code %d). Refreshing...\n", httpCode);
            attemptedRefresh = true;
            if (refreshSession())
                goto RETRY_FETCH;
            Serial.println("Refresh failed.");
        }
        else
        {
            Serial.printf("HTTP error: %d\n", httpCode);
        }
    }
}

bool checkDiff(DynamicJsonDocument &doc)
{
    if (doc.size() < 3)
        return false;

    time_t time0 = extractUnixTime(doc[0]["DT"]);
    time_t time1 = extractUnixTime(doc[1]["DT"]);
    Serial.println(time0);
    Serial.println(time1);
    Serial.println(abs(time0 - time1) < 300);

    return abs(time0 - time1) < 300;
}

time_t extractUnixTime(String rawTime)
{
    if (rawTime.startsWith("Date("))
    {
        int startPos = 5;
        int dashPos = rawTime.indexOf('-', startPos);
        if (dashPos == -1)
            dashPos = rawTime.indexOf('+', startPos);

        if (dashPos > startPos)
        {
            String timestampStr = rawTime.substring(startPos, dashPos);
            return strtoull(timestampStr.c_str(), NULL, 10) / 1000;
        }
    }
    return 0;
}

// Function to format the timestamp
String formatTimestamp(String rawTime)
{
    char result[6]; // Buffer for "HH:MM" plus null terminator

    if (rawTime.startsWith("Date("))
    {
        // Find the positions of important parts
        int startPos = 5; // After "Date("
        int dashPos = rawTime.indexOf('-', startPos);
        int endPos = rawTime.indexOf(')', dashPos);

        if (dashPos > startPos && endPos > dashPos)
        {
            // Extract timestamp (milliseconds since epoch)
            String timestampStr = rawTime.substring(startPos, dashPos);
            uint64_t timestamp = strtoull(timestampStr.c_str(), NULL, 10);

            // Extract timezone offset
            String tzOffsetStr = rawTime.substring(dashPos + 1, endPos);
            int tzOffset = tzOffsetStr.toInt();

            // Convert timezone from HHMM format to hours
            int tzHours = tzOffset / 100;
            int tzMinutes = tzOffset % 100;

            // Convert milliseconds to seconds
            time_t seconds = timestamp / 1000;

            // Apply timezone offset
            seconds -= (tzHours * 3600 + tzMinutes * 60);

            // Convert to tm structure
            struct tm timeInfo;
            gmtime_r(&seconds, &timeInfo);

            // Format as HH:MM
            sprintf(result, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);

            return String(result);
        }
    }

    return "N/A";
}

// Draw a 45-degree up-right arrow
void drawDiagonalUpArrow(int x, int y, int size, uint16_t color)
{
    // Arrow body
    tft.drawLine(x, y, x + size, y - size, color);
    // Arrow head
    tft.drawLine(x + size, y - size, x + size - (size / 3), y - size + (size / 3), color);
    tft.drawLine(x + size, y - size, x + size - (size / 3), y - size, color);
}

// Draw a 45-degree down-right arrow
void drawDiagonalDownArrow(int x, int y, int size, uint16_t color)
{
    // Arrow body
    tft.drawLine(x, y, x + size, y + size, color);
    // Arrow head
    tft.drawLine(x + size, y + size, x + size - (size / 3), y + size - (size / 3), color);
    tft.drawLine(x + size, y + size, x + size - (size / 3), y + size, color);
}

void updateDisplay()
{
    tft.fillScreen(ILI9341_BLACK);

    // Title
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 10);
    tft.print("Glucose Monitor");

    int colorBasedOnGlucose = ILI9341_GREEN;

    if (current_glucose_mgdl > 180)
    {
        colorBasedOnGlucose = ILI9341_RED;
    }
    else if (current_glucose_mgdl < 70)
    {
        colorBasedOnGlucose = ILI9341_BLUE;
    }
    else
    {
        colorBasedOnGlucose = ILI9341_GREEN;
    }

    // Glucose mg/dL
    tft.setTextColor(colorBasedOnGlucose);
    tft.setTextSize(4);
    tft.setCursor(40, 50);
    tft.printf("%.0f", current_glucose_mgdl);
    tft.setTextSize(2);
    tft.setCursor(160, 60);
    tft.print("mg/dL");

    // Glucose mmol/L
    tft.setTextSize(3);
    tft.setCursor(40, 100);
    tft.printf("%.1f", glucose_mmol);
    tft.setTextSize(2);
    tft.setCursor(160, 110);
    tft.print("mmol/L");

    // Glucose Change Indicator
    tft.setTextSize(2);
    tft.setCursor(115, 57);
    if (glucose_diff > 0)
    {
        tft.printf("+%.0f", glucose_diff);
    }
    else if (glucose_diff < 0)
    {
        tft.printf("%.0f", glucose_diff);
    }
    else
    {
        tft.setTextColor(ILI9341_WHITE);
        tft.print("+0");
    }

    // **Trend Arrow & Text**
    tft.setTextSize(4);
    tft.setCursor(40, 170);

    int trendIndex = -1;
    for (int i = 0; i < 10; i++)
    {
        if (trend == DEXCOM_TREND_DIRECTIONS[i])
        {
            trendIndex = i;
            break;
        }
    }

    // Handle display based on trend index
    if (trendIndex != -1)
    {
        // For FortyFiveUp (3) and FortyFiveDown (5), use custom arrows
        if (trendIndex == 3)
        { // FortyFiveUp
            trend = "Increasing";
            drawDiagonalUpArrow(40, 180, 20, colorBasedOnGlucose);
        }
        else if (trendIndex == 5)
        { // FortyFiveDown
            trend = "Decreasing";
            drawDiagonalDownArrow(40, 170, 20, colorBasedOnGlucose);
        }
        else
        {
            // For all other trends, use the character arrows
            tft.setCursor(40, 170);
            tft.print(DEXCOM_TREND_ARROWS[trendIndex]);
        }
    }
    else
    {
        // Unknown trend
        tft.setCursor(40, 170);
        tft.print("?");
    }

    // Trend Text
    tft.setTextSize(2);
    tft.setCursor(100, 180);
    tft.print(trend);

    // Timestamp
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(40, 210);
    tft.print("Updated: ");
    tft.print(timestamp);
}

void displayWiFiStatus(bool status)
{
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(40, 60);

    if (status)
    {
        tft.print("WiFi Connected");
    }
    else
    {
        tft.setTextColor(ILI9341_RED);
        tft.print("WiFi Down...");
        tft.setCursor(40, 90);
        tft.print("Reconnecting...");
    }
}

void checkWiFiConnection()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi connection is down, reconnecting...");
        displayWiFiStatus(false); // Show on TFT screen

        WiFi.disconnect();
        WiFi.reconnect();

        unsigned long startAttemptTime = millis();

        // Try reconnecting for 30 seconds
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000)
        {
            delay(1000);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("\nReconnected to WiFi");
            displayWiFiStatus(true); // Show success on TFT
            delay(2000);             // Keep message visible before updating
            updateDisplay();         // Refresh glucose data display
        }
        else
        {
            Serial.println("\nWiFi reconnection failed");
        }
    }
}
