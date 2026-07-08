#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <time.h>
#include <WiFiManager.h>
#include <Preferences.h>

// Preferences for storing credentials
Preferences preferences;

// Credential storage (loaded from Preferences)
String storedDexcomUsername = "";
String storedDexcomPassword = "";

// Reset button pin (hold during boot to reset config)
#define RESET_CONFIG_PIN 0  // GPIO0 - BOOT button on most ESP32 boards

// Define ESP32 Pins for ILI9341
#define TFT_CS 5  // Chip Select
#define TFT_DC 2  // Data/Command
#define TFT_RST 4 // Reset

// Initialize Display
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Dexcom region (loaded from Preferences): "us", "ous" (worldwide outside US), or "jp" (Japan)
String storedDexcomRegion = "us";

// Dexcom Share API endpoints - derived at runtime from the selected region
String dexcomAuthenticateURL;
String dexcomLoginURL;
String dexcomDataURL;

// Build the API URLs for the currently selected region
void updateDexcomURLs()
{
    String host;
    if (storedDexcomRegion == "ous")
        host = "shareous1.dexcom.com"; // Outside US (worldwide)
    else if (storedDexcomRegion == "jp")
        host = "share.dexcom.jp"; // Japan
    else
        host = "share2.dexcom.com"; // United States (default)

    String base = "https://" + host + "/ShareWebServices/Services/";
    dexcomAuthenticateURL = base + "General/AuthenticatePublisherAccount";
    dexcomLoginURL = base + "General/LoginPublisherAccountById";
    dexcomDataURL = base + "Publisher/ReadPublisherLatestGlucoseValues";
}

// Dexcom credentials are now stored in Preferences and loaded at runtime

const char *applicationId = "d8665ade-9673-4e27-9ff6-92db4ce13d13"; // This is a constant for the Dexcom API

String accountId = "";
String sessionId = "";

float current_glucose_mgdl = 0;
float previous_glucose_mgdl = 0;
float glucose_diff = 0;
bool validDiff = false;

float glucose_mmol = 0;
String trend = "Stable";
String timestamp = "N/A";

unsigned long sessionStartMs = 0;
const unsigned long SESSION_REFRESH_MS = 23UL * 60UL * 60UL * 1000UL; // 23h

// NTP time sync
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;     // Use UTC for comparing with Dexcom timestamps
const int daylightOffset_sec = 0; // No DST adjustment needed for UTC
bool ntpSynced = false;

// Adaptive polling - uses NTP time for accurate scheduling
time_t lastReadingTimestamp = 0;                   // Unix timestamp of last reading
const int READING_INTERVAL_SEC = 300;              // Dexcom readings are 5 min apart
const int FETCH_BUFFER_SEC = 15;                   // Buffer after expected reading time
const unsigned long MIN_FETCH_INTERVAL_MS = 30000; // Minimum 30s between API calls

// Historical readings for graph (10 hours = 120 readings at 5-min intervals)
const int GRAPH_POINTS = 120;
float graphReadings[GRAPH_POINTS];
time_t graphTimestamps[GRAPH_POINTS];
int graphCount = 0; // Number of valid readings stored

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

// Load saved credentials from Preferences
void loadCredentials()
{
    preferences.begin("dexcom", true); // Read-only mode
    storedDexcomUsername = preferences.getString("username", "");
    storedDexcomPassword = preferences.getString("password", "");
    storedDexcomRegion = preferences.getString("region", "us");
    preferences.end();

    updateDexcomURLs();

    Serial.println("Loaded credentials from storage");
    Serial.printf("Username: %s\n", storedDexcomUsername.length() > 0 ? "(set)" : "(empty)");
    Serial.printf("Region: %s\n", storedDexcomRegion.c_str());
}

// Save credentials to Preferences
void saveCredentials(const char *username, const char *password, const char *region)
{
    // Default to US if the region is missing or unrecognized
    String regionStr = String(region);
    if (regionStr != "ous" && regionStr != "jp")
        regionStr = "us";

    preferences.begin("dexcom", false); // Read-write mode
    preferences.putString("username", username);
    preferences.putString("password", password);
    preferences.putString("region", regionStr);
    preferences.end();

    storedDexcomUsername = String(username);
    storedDexcomPassword = String(password);
    storedDexcomRegion = regionStr;

    updateDexcomURLs();

    Serial.println("Credentials saved to storage");
    Serial.printf("Region: %s\n", storedDexcomRegion.c_str());
}

// Clear all saved credentials and WiFi config
void clearCredentials()
{
    preferences.begin("dexcom", false);
    preferences.clear();
    preferences.end();

    storedDexcomUsername = "";
    storedDexcomPassword = "";
    storedDexcomRegion = "us";

    updateDexcomURLs();

    Serial.println("Credentials cleared from storage");
}

// Display configuration portal status on TFT
void displayConfigPortalStatus(const char *ssid)
{
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.print("WiFi Setup Mode");

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 60);
    tft.print("Connect to WiFi network:");

    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(2);
    tft.setCursor(20, 80);
    tft.print(ssid);

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 120);
    tft.print("Then open browser to:");
    tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(20, 135);
    tft.print("http://192.168.4.1");

    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(20, 170);
    tft.print("Enter WiFi and Dexcom credentials");
    tft.setCursor(20, 185);
    tft.print("to configure the monitor.");

    tft.setTextColor(ILI9341_RED);
    tft.setCursor(20, 210);
    tft.print("Hold BOOT button at startup to reset");
}

void setup()
{
    Serial.begin(115200);

    // Initialize reset button pin
    pinMode(RESET_CONFIG_PIN, INPUT_PULLUP);

    // Initialize Display first so we can show status
    SPI.begin(23, 19, 18, 5);
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(30, 30);
    tft.print("Glucose Monitor!");

    // Check if reset button is held during boot
    bool resetConfig = (digitalRead(RESET_CONFIG_PIN) == LOW);
    if (resetConfig)
    {
        Serial.println("Reset button held - clearing credentials");
        tft.setCursor(30, 60);
        tft.setTextColor(ILI9341_RED);
        tft.print("Resetting config...");
        clearCredentials();
        delay(1000);
    }

    // Load saved Dexcom credentials
    loadCredentials();

    // Create WiFiManager instance
    WiFiManager wm;

    // Set connection timeout (seconds) - don't wait forever
    wm.setConnectTimeout(30);

    // Custom parameters for Dexcom credentials
    WiFiManagerParameter dexcom_user("dexcom_user", "Dexcom Username", storedDexcomUsername.c_str(), 64);
    WiFiManagerParameter dexcom_pass("dexcom_pass", "Dexcom Password", storedDexcomPassword.c_str(), 64);

    // Region selector: a dropdown (UI only) that writes into a hidden field which
    // WiFiManager actually captures via getValue(). Pre-selects the saved region on load.
    static const char REGION_SELECT_HTML[] =
        "<br/><label for='region_ui'>Dexcom Region</label><br/>"
        "<select id='region_ui' onchange=\"document.getElementById('dexcom_region').value=this.value;\">"
        "<option value='us'>United States</option>"
        "<option value='ous'>Outside US / Worldwide</option>"
        "<option value='jp'>Japan</option>"
        "</select>"
        "<script>window.addEventListener('load',function(){"
        "var h=document.getElementById('dexcom_region');var s=document.getElementById('region_ui');"
        "if(h&&s){s.value=(h.value||'us');}});</script>";

    // Hidden field holds the actual value WiFiManager submits and captures
    WiFiManagerParameter dexcom_region("dexcom_region", "", storedDexcomRegion.c_str(), 5, " type='hidden'");
    WiFiManagerParameter region_select(REGION_SELECT_HTML);

    wm.addParameter(&dexcom_user);
    wm.addParameter(&dexcom_pass);
    wm.addParameter(&dexcom_region);
    wm.addParameter(&region_select);

    // Set callback for when config is saved
    wm.setSaveParamsCallback([&dexcom_user, &dexcom_pass, &dexcom_region]()
                             { saveCredentials(dexcom_user.getValue(), dexcom_pass.getValue(), dexcom_region.getValue()); });

    // Set custom AP name
    const char *apName = "DexcomMonitor-Setup";

    // Set timeout for config portal (2 minutes)
    wm.setConfigPortalTimeout(120);

    // Display portal info on TFT when portal starts
    wm.setAPCallback([](WiFiManager *wm)
                     {
                         // Re-init display in case of SPI issues
                         tft.begin();
                         tft.setRotation(3);
                         displayConfigPortalStatus(wm->getConfigPortalSSID().c_str());
                     });

    bool connected;
    if (resetConfig || storedDexcomUsername.length() == 0)
    {
        // Show setup instructions on welcome screen
        tft.fillScreen(ILI9341_BLACK);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(35, 5);
        tft.print("Welcome to");
        tft.setCursor(15, 28);
        tft.print("Glucose Monitor!");

        tft.setTextSize(2);
        tft.setCursor(10, 65);
        tft.print("Connect to WiFi:");

        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(2);
        tft.setCursor(10, 90);
        tft.print(apName);

        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(10, 125);
        tft.print("Open browser to:");

        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(3);
        tft.setCursor(10, 150);
        tft.print("192.168.4.1");

        tft.setTextColor(ILI9341_LIGHTGREY);
        tft.setTextSize(1);
        tft.setCursor(10, 195);
        tft.print("Enter WiFi and Dexcom credentials");
        tft.setCursor(10, 210);
        tft.print("to get started.");

        // Force config portal if reset or no credentials
        connected = wm.startConfigPortal(apName);
    }
    else
    {
        tft.setCursor(30, 60);
        tft.print("Connecting to WiFi...");

        // Try auto-connect, fall back to config portal if it fails
        connected = wm.autoConnect(apName);
    }

    // Re-initialize display immediately after WiFiManager (can cause SPI issues)
    tft.begin();
    tft.setRotation(3);

    if (!connected)
    {
        Serial.println("WiFi connection failed - reopening config portal");

        tft.fillScreen(ILI9341_BLACK);
        tft.setTextColor(ILI9341_RED);
        tft.setTextSize(2);
        tft.setCursor(20, 20);
        tft.print("WiFi Failed!");

        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(20, 55);
        tft.print("Check WiFi password");

        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1);
        tft.setCursor(20, 100);
        tft.print("Restarting setup in 5 seconds...");
        tft.setCursor(20, 120);
        tft.print("Connect to:");
        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(2);
        tft.setCursor(20, 135);
        tft.print(apName);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1);
        tft.setCursor(20, 165);
        tft.print("Then go to:");
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(2);
        tft.setCursor(20, 180);
        tft.print("192.168.4.1");

        delay(5000);
        clearCredentials();
        ESP.restart();
    }

    Serial.println("Connected to WiFi");

    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.setCursor(30, 30);
    tft.print("WiFi Connected!");

    // Sync time with NTP server
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(30, 60);
    tft.print("Syncing time...");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.print("Syncing NTP time");
    time_t now = 0;
    int ntpRetries = 0;
    while (now < 1000000000 && ntpRetries < 10)
    {
        delay(500);
        Serial.print(".");
        time(&now);
        ntpRetries++;
    }
    if (now >= 1000000000)
    {
        ntpSynced = true;
        Serial.println(" synced!");
        Serial.print("Current UTC time: ");
        Serial.println(now);
    }
    else
    {
        Serial.println(" failed! Using fallback polling.");
    }

    tft.setCursor(30, 90);
    tft.print("Loading Glucose Data...");

    delay(1000);

    if (authenticateToDexcom() && loginToDexcom())
    {
        fetchGlucoseData();
    }
    else
    {
        Serial.println("Dexcom login failed - reopening config portal");
        tft.fillScreen(ILI9341_BLACK);
        tft.setTextColor(ILI9341_RED);
        tft.setTextSize(2);
        tft.setCursor(20, 20);
        tft.print("Dexcom Failed!");

        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(20, 55);
        tft.print("Check Dexcom credentials");

        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1);
        tft.setCursor(20, 100);
        tft.print("Restarting setup in 5 seconds...");
        tft.setCursor(20, 120);
        tft.print("Connect to:");
        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(2);
        tft.setCursor(20, 135);
        tft.print("DexcomMonitor-Setup");
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1);
        tft.setCursor(20, 165);
        tft.print("Then go to:");
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(2);
        tft.setCursor(20, 180);
        tft.print("192.168.4.1");

        delay(5000);
        clearCredentials();
        ESP.restart();
    }
}

void loop()
{
    static unsigned long lastFetchTime = 0;
    static unsigned long lastWiFiCheckTime = 0;
    unsigned long nowMs = millis();

    if (nowMs - lastWiFiCheckTime >= 300000UL)
    {
        checkWiFiConnection();
        lastWiFiCheckTime = nowMs;
    }

    // Enforce minimum interval between API calls
    if (nowMs - lastFetchTime < MIN_FETCH_INTERVAL_MS)
        return;

    // Adaptive polling using NTP time
    bool shouldFetch = false;

    if (lastReadingTimestamp == 0)
    {
        // First run or no valid reading yet - fetch immediately
        shouldFetch = true;
    }
    else if (ntpSynced)
    {
        // Use accurate NTP time for polling
        time_t currentTime;
        time(&currentTime);

        // Next reading expected at lastReadingTimestamp + 5 minutes
        time_t nextReadingExpected = lastReadingTimestamp + READING_INTERVAL_SEC;

        // Fetch when current time >= next expected + buffer
        shouldFetch = (currentTime >= nextReadingExpected + FETCH_BUFFER_SEC);
    }
    else
    {
        // Fallback: fetch every 5 min 15 sec using millis
        static unsigned long lastSuccessfulFetchMs = 0;
        if (lastSuccessfulFetchMs == 0)
            lastSuccessfulFetchMs = nowMs;

        unsigned long elapsedMs = nowMs - lastSuccessfulFetchMs;
        shouldFetch = (elapsedMs >= (READING_INTERVAL_SEC + FETCH_BUFFER_SEC) * 1000UL);

        if (shouldFetch)
            lastSuccessfulFetchMs = nowMs;
    }

    if (shouldFetch)
    {
        fetchGlucoseData();
        lastFetchTime = nowMs;
    }
}

bool authenticateToDexcom()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;
    if (storedDexcomUsername.length() == 0 || storedDexcomPassword.length() == 0)
    {
        Serial.println("Dexcom credentials not configured");
        return false;
    }
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(dexcomAuthenticateURL);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"accountName\":\"" + storedDexcomUsername + "\",\"password\":\"" + storedDexcomPassword + "\",\"applicationId\":\"" + String(applicationId) + "\"}";
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
    String payload = "{\"accountId\":\"" + accountId + "\",\"password\":\"" + storedDexcomPassword + "\",\"applicationId\":\"" + String(applicationId) + "\"}";
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

    String fetchPayload = "{\"sessionId\":\"" + sessionId + "\",\"minutes\":360,\"maxCount\":120}";
    int httpCode = http.POST(fetchPayload);
    String payload = (httpCode > 0) ? http.getString() : "";
    http.end();

    bool bodyInvalid = payload.indexOf("SessionNotValid") != -1 || payload.indexOf("SessionId") != -1;

    if (httpCode == HTTP_CODE_OK && !bodyInvalid)
    {
        DynamicJsonDocument doc(24576); // Increased for 120 readings
        DeserializationError err = deserializeJson(doc, payload);
        if (!err && doc.size() >= 2)
        {
            current_glucose_mgdl = doc[0]["Value"];
            glucose_mmol = current_glucose_mgdl / 18.0;
            trend = String(doc[0]["Trend"]);
            timestamp = formatTimestamp(doc[0]["DT"]);
            time_t newReadingTimestamp = extractUnixTime(doc[0]["DT"]);

            // Update timestamp if this is a new reading
            if (newReadingTimestamp != lastReadingTimestamp)
            {
                lastReadingTimestamp = newReadingTimestamp;
            }

            // Store historical readings for graph (newest first in API, we store oldest first)
            graphCount = min((int)doc.size(), GRAPH_POINTS);
            for (int i = 0; i < graphCount; i++)
            {
                // Reverse order: API index 0 (newest) goes to our array end
                int srcIdx = graphCount - 1 - i;
                graphReadings[i] = doc[srcIdx]["Value"];
                graphTimestamps[i] = extractUnixTime(doc[srcIdx]["DT"]);
            }

            // Find best previous reading and calculate normalized difference
            time_t timeDiffSeconds = 0;
            int prevIndex = findBestPreviousReading(doc, timeDiffSeconds);

            if (prevIndex > 0 && timeDiffSeconds > 0)
            {
                previous_glucose_mgdl = doc[prevIndex]["Value"];
                float rawDiff = current_glucose_mgdl - previous_glucose_mgdl;

                // Normalize to per-5-minute rate if time diff is between 3-10 minutes
                if (timeDiffSeconds >= 180 && timeDiffSeconds <= 600)
                {
                    glucose_diff = rawDiff * 300.0 / timeDiffSeconds;
                }
                else
                {
                    // Use raw difference for very short intervals
                    glucose_diff = rawDiff;
                }
                validDiff = true;
            }
            else
            {
                // No valid previous reading found
                glucose_diff = 0;
                validDiff = false;
            }

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

int findBestPreviousReading(DynamicJsonDocument &doc, time_t &timeDiffSeconds)
{
    if (doc.size() < 2)
        return -1;

    time_t currentTime = extractUnixTime(doc[0]["DT"]);
    const time_t TARGET_DIFF = 300; // 5 minutes in seconds
    const time_t MAX_AGE = 900;     // 15 minutes - readings older than this are stale

    int bestIndex = -1;
    time_t bestDiff = MAX_AGE + 1;

    // Check readings 1 through min(5, doc.size()-1) to handle duplicate readings
    int maxIndex = min((int)doc.size() - 1, 5);
    for (int i = 1; i <= maxIndex; i++)
    {
        time_t readingTime = extractUnixTime(doc[i]["DT"]);
        time_t diff = currentTime - readingTime;

        // Skip if reading is in the future or too old
        if (diff <= 0 || diff > MAX_AGE)
            continue;

        // Find reading closest to 5 minutes ago
        time_t distanceFromTarget = abs(diff - TARGET_DIFF);
        if (distanceFromTarget < bestDiff)
        {
            bestDiff = distanceFromTarget;
            bestIndex = i;
            timeDiffSeconds = diff;
        }
    }

    return bestIndex;
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

// Draw a 45-degree up-right arrow (↗) using filled triangle for arrowhead
void drawDiagonalUpArrow(int x, int y, int size, uint16_t color)
{
    // Arrowhead tip is at top-right
    int tipX = x + size;
    int tipY = y - size;

    // Filled triangle arrowhead pointing up-right
    // Tip at (tipX, tipY), base perpendicular to the arrow direction
    int headSize = size / 2;
    tft.fillTriangle(
        tipX, tipY,            // Tip
        tipX - headSize, tipY, // Base left
        tipX, tipY + headSize, // Base bottom
        color);

    // Arrow shaft (thicker line using multiple parallel lines)
    int shaftLen = size - headSize / 2;
    for (int i = -1; i <= 1; i++)
    {
        tft.drawLine(x, y + i, x + shaftLen, y - shaftLen + i, color);
    }
}

// Draw a 45-degree down-right arrow (↘) using filled triangle for arrowhead
void drawDiagonalDownArrow(int x, int y, int size, uint16_t color)
{
    // Arrowhead tip is at bottom-right
    int tipX = x + size;
    int tipY = y + size;

    // Filled triangle arrowhead pointing down-right
    int headSize = size / 2;
    tft.fillTriangle(
        tipX, tipY,            // Tip
        tipX - headSize, tipY, // Base left
        tipX, tipY - headSize, // Base top
        color);

    // Arrow shaft (thicker line using multiple parallel lines)
    int shaftLen = size - headSize / 2;
    for (int i = -1; i <= 1; i++)
    {
        tft.drawLine(x, y + i, x + shaftLen, y + shaftLen + i, color);
    }
}

void drawGraph()
{
    if (graphCount < 2)
        return;

    // Graph dimensions
    const int graphX = 25;  // Left margin (space for Y labels)
    const int graphY = 125; // Top of graph area
    const int graphW = 290; // Width
    const int graphH = 100; // Height
    const float minGlucose = 40.0;
    const float maxGlucose = 300.0;
    const float glucoseRange = maxGlucose - minGlucose;

    // Draw border
    tft.drawRect(graphX, graphY, graphW, graphH, ILI9341_DARKGREY);

    // Draw target range lines (70 and 180 mg/dL)
    int y70 = graphY + graphH - (int)((70.0 - minGlucose) / glucoseRange * graphH);
    int y180 = graphY + graphH - (int)((180.0 - minGlucose) / glucoseRange * graphH);

    // Dashed lines for target range
    for (int x = graphX; x < graphX + graphW; x += 6)
    {
        tft.drawPixel(x, y70, ILI9341_BLUE);
        tft.drawPixel(x + 1, y70, ILI9341_BLUE);
        tft.drawPixel(x, y180, ILI9341_RED);
        tft.drawPixel(x + 1, y180, ILI9341_RED);
    }

    // Y-axis labels
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(2, y180 - 3);
    tft.print("180");
    tft.setCursor(8, y70 - 3);
    tft.print("70");

    // Plot glucose line using actual timestamps for x-position
    // This handles gaps (missed readings) and duplicates correctly
    time_t oldestTime = graphTimestamps[0];
    time_t newestTime = graphTimestamps[graphCount - 1];
    time_t timeRange = newestTime - oldestTime;

    // Avoid division by zero if all timestamps are the same
    if (timeRange <= 0)
        timeRange = 1;

    for (int i = 1; i < graphCount; i++)
    {
        float val1 = constrain(graphReadings[i - 1], minGlucose, maxGlucose);
        float val2 = constrain(graphReadings[i], minGlucose, maxGlucose);

        // Calculate x-position based on actual timestamp
        int x1 = graphX + (int)(((graphTimestamps[i - 1] - oldestTime) * (long)graphW) / timeRange);
        int x2 = graphX + (int)(((graphTimestamps[i] - oldestTime) * (long)graphW) / timeRange);
        int y1 = graphY + graphH - (int)((val1 - minGlucose) / glucoseRange * graphH);
        int y2 = graphY + graphH - (int)((val2 - minGlucose) / glucoseRange * graphH);

        // Color based on glucose level
        uint16_t lineColor = ILI9341_GREEN;
        if (val2 > 180)
            lineColor = ILI9341_RED;
        else if (val2 < 70)
            lineColor = ILI9341_BLUE;

        tft.drawLine(x1, y1, x2, y2, lineColor);
    }

    // Time labels (start and end) - dynamic based on actual data range
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(graphX, graphY + graphH + 3);
    int hoursAgo = (timeRange + 1800) / 3600; // Round to nearest hour
    if (hoursAgo < 1)
        hoursAgo = 1;
    tft.printf("-%dh", hoursAgo);
    tft.setCursor(graphX + graphW - 20, graphY + graphH + 3);
    tft.print("now");
}

void updateDisplay()
{
    tft.fillScreen(ILI9341_BLACK);

    int colorBasedOnGlucose = ILI9341_GREEN;
    if (current_glucose_mgdl > 180)
        colorBasedOnGlucose = ILI9341_RED;
    else if (current_glucose_mgdl < 70)
        colorBasedOnGlucose = ILI9341_BLUE;

    // Row 1: Glucose mg/dL (large) + diff + unit
    tft.setTextColor(colorBasedOnGlucose);
    tft.setTextSize(4);
    tft.setCursor(10, 8);
    tft.printf("%.0f", current_glucose_mgdl);

    // Glucose Change Indicator (next to mg/dL value)
    tft.setTextSize(2);
    tft.setCursor(95, 15);
    if (!validDiff)
    {
        tft.setTextColor(ILI9341_WHITE);
        tft.print("--");
    }
    else if (glucose_diff > 0)
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

    tft.setTextColor(colorBasedOnGlucose);
    tft.setTextSize(2);
    tft.setCursor(140, 15);
    tft.print("mg/dL");

    // Row 2: mmol/L + Trend arrow + trend text
    tft.setTextColor(colorBasedOnGlucose);
    tft.setTextSize(3);
    tft.setCursor(10, 45);
    tft.printf("%.1f", glucose_mmol);

    tft.setTextSize(1);
    tft.setCursor(75, 55);
    tft.print("mmol/L");

    // Trend Arrow (smaller, inline)
    int trendIndex = -1;
    for (int i = 0; i < 10; i++)
    {
        if (trend == DEXCOM_TREND_DIRECTIONS[i])
        {
            trendIndex = i;
            break;
        }
    }

    tft.setTextSize(3);
    tft.setCursor(130, 45);
    if (trendIndex != -1)
    {
        if (trendIndex == 3)
        { // FortyFiveUp
            trend = "Increasing";
            drawDiagonalUpArrow(130, 60, 15, colorBasedOnGlucose);
        }
        else if (trendIndex == 5)
        { // FortyFiveDown
            trend = "Decreasing";
            drawDiagonalDownArrow(130, 50, 15, colorBasedOnGlucose);
        }
        else
        {
            tft.print(DEXCOM_TREND_ARROWS[trendIndex]);
        }
    }
    else
    {
        tft.print("?");
    }

    // Trend Text
    tft.setTextSize(2);
    tft.setCursor(170, 50);
    tft.print(trend);

    // Row 3: Timestamp
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.print("Updated: ");
    tft.print(timestamp);

    // Separator line
    tft.drawLine(0, 95, 320, 95, ILI9341_DARKGREY);

    // Row 4: Graph title - dynamic based on actual data range
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 100);
    if (graphCount >= 2)
    {
        time_t range = graphTimestamps[graphCount - 1] - graphTimestamps[0];
        int hours = (range + 1800) / 3600; // Round to nearest hour
        if (hours < 1)
            hours = 1;
        tft.printf("Last %d hours", hours);
    }
    else
    {
        tft.print("Last -- hours");
    }

    // Draw the glucose graph
    drawGraph();
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
