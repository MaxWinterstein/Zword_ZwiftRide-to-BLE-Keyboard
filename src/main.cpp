#include <Arduino.h>
#include <BLEDevice.h>
#include <BleKeyboard.h>

// =============================================================================
// VERSION AND BUILD INFO
// =============================================================================

#define FIRMWARE_VERSION "1.2.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// =============================================================================
// CONFIGURATION
// =============================================================================

// General Options
bool enableHapticFeedback = false; // Vibration when Zwift Ride button is pressed
const char* ZWIFT_RIDE_BLUETOOTH_NAME ="Zwift Ride";

// BLE Keyboard initialization
BleKeyboard bleKeyboard("Zword", "Derguitarjo", 100);

// Zwift Ride Bluetooth UUIDs
const char* ZWIFT_CUSTOM_SERVICE_UUIDS[] = {
    "0000FC82-0000-1000-8000-00805F9B34FB", // Older ones
    "00000001-19CA-4651-86E5-FA29DCDD09D1"  // Newer ones
};
const char* ZWIFT_ASYNC_CHARACTERISTIC_UUID = "00000002-19CA-4651-86E5-FA29DCDD09D1"; // Notify
const char* ZWIFT_SYNC_RX_CHARACTERISTIC_UUID = "00000003-19CA-4651-86E5-FA29DCDD09D1"; // Write Without Response
const char* ZWIFT_SYNC_TX_CHARACTERISTIC_UUID = "00000004-19CA-4651-86E5-FA29DCDD09D1"; // Indicate, Read
const char* ZWIFT_Unknown_CHARACTERISTIC_UUID = "00000006-19CA-4651-86E5-FA29DCDD09D1"; // Indicate, Read, Write, Write Without Response

// Global variable to store which service UUID was actually found
const char* detectedServiceUUID = nullptr;

// =============================================================================
// BUTTON CONFIGURATION
// =============================================================================

// Action types
enum class ActionType {
    KEY_PRESS,
    MEDIA_KEY
};

// Media key constants
enum MediaKey {
    VOLUME_UP = 1,
    VOLUME_DOWN = 2,
    PREV_TRACK = 3,
    NEXT_TRACK = 4,
    PLAY_PAUSE = 5
};

// Game key constants (MyWhoosh mappings)
enum GameKey {
    PAUSE = ' ',        // Space = Pause
    HIDE_UI = 'u',      // Hide/unhide UI
    SHIFT_DOWN = 'i',   // Shift down
    SHIFT_UP = 'k',     // Shift up
    PEACE = '1',        // Peace emote
    HELLO = '2',        // Hello emote
    POWER = '3',        // Power emote
    DAB = '4',          // Dab emote
    AGAIN = '5',        // Again emote
    BATTERY_LOW = '6',  // Battery low emote
    THUMBS_UP = '7'     // Thumbs up emote
};

struct ButtonConfig {
    uint8_t dataIndex;      // Which pData array index (2, 3, or 4)
    uint8_t code;           // Button code value
    ActionType type;        // Key press or media key
    const char* controller; // "Left" or "Right"
    const char* button;     // Button name
    const char* function;   // What it does
    union {
        GameKey gameKey;
        MediaKey mediaKey;
    } action;
};

// =============================================================================
// COMPLETE BUTTON MAPPING TABLE
// =============================================================================
// Format: {dataIndex, code, type, controller, button, function, action}

const ButtonConfig BUTTON_MAPPINGS[] = {
    // LEFT CONTROLLER - D-PAD (Media Keys)
    {2, 0xFD, ActionType::MEDIA_KEY,  "Left", "D-Pad Up",    "Volume Up",       {.mediaKey = VOLUME_UP}},
    {2, 0xF7, ActionType::MEDIA_KEY,  "Left", "D-Pad Down",  "Volume Down",     {.mediaKey = VOLUME_DOWN}},
    {2, 0xFE, ActionType::MEDIA_KEY,  "Left", "D-Pad Left",  "Previous Track",  {.mediaKey = PREV_TRACK}},
    {2, 0xFB, ActionType::MEDIA_KEY,  "Left", "D-Pad Right", "Next Track",      {.mediaKey = NEXT_TRACK}},

    // LEFT CONTROLLER - POWER & SIDE BUTTONS
    {3, 0xEF, ActionType::MEDIA_KEY,  "Left", "Power",       "Play/Pause",      {.mediaKey = PLAY_PAUSE}},
    {3, 0xFD, ActionType::KEY_PRESS,  "Left", "Side Upper",  "Shift Down",      {.gameKey = SHIFT_DOWN}},
    {3, 0xFB, ActionType::KEY_PRESS,  "Left", "Side Middle", "Shift Down",      {.gameKey = SHIFT_DOWN}},
    {3, 0xF7, ActionType::KEY_PRESS,  "Left", "Side Lower",  "Shift Down",      {.gameKey = SHIFT_DOWN}},

    // RIGHT CONTROLLER - ACTION BUTTONS
    {2, 0xEF, ActionType::KEY_PRESS,  "Right", "A Button",   "Hello Emote",     {.gameKey = HELLO}},
    {2, 0xBF, ActionType::KEY_PRESS,  "Right", "Y Button",   "Hide/Show UI",    {.gameKey = HIDE_UI}},
    {2, 0xDF, ActionType::KEY_PRESS,  "Right", "B Button",   "Battery Low",     {.gameKey = BATTERY_LOW}},
    {3, 0xFE, ActionType::KEY_PRESS,  "Right", "Z Button",   "Thumbs Up",       {.gameKey = THUMBS_UP}},

    // RIGHT CONTROLLER - POWER & SIDE BUTTONS
    {4, 0xFD, ActionType::KEY_PRESS,  "Right", "Power",      "Pause/Resume",    {.gameKey = PAUSE}},
    {3, 0xDF, ActionType::KEY_PRESS,  "Right", "Side Upper", "Shift Up",        {.gameKey = SHIFT_UP}},
    {3, 0xBF, ActionType::KEY_PRESS,  "Right", "Side Middle","Shift Up",        {.gameKey = SHIFT_UP}},
    {4, 0xFE, ActionType::KEY_PRESS,  "Right", "Side Lower", "Shift Up",        {.gameKey = SHIFT_UP}},
};

const size_t BUTTON_COUNT = sizeof(BUTTON_MAPPINGS) / sizeof(ButtonConfig);

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

// BLE Objects
BLEAdvertisedDevice* myDevice;
BLERemoteCharacteristic* pNotifyCharacteristic;
BLERemoteCharacteristic* pRxCharacteristic;
BLERemoteCharacteristic* pTxCharacteristic;

// Connection state helpers
bool doConnect = false;
bool connected = false;
bool subscribed = false;
bool sentMessage = false;

// Vibration control
volatile bool shouldVibrate = false;

// Button state tracking
uint8_t prevData2 = 0xFF;
uint8_t prevData3 = 0xFF;
uint8_t prevData4 = 0xFF;

String notification = "";

// Statistics for logging
unsigned long startTime;
unsigned int buttonPressCount = 0;
unsigned int connectionAttempts = 0;

// =============================================================================
// LOGGING FUNCTIONS
// =============================================================================

/**
 * Print startup banner with version and build information
 */
void printStartupBanner() {
    Serial.println("===============================================");
    Serial.println("          ZWORD - Zwift Ride to BLE          ");
    Serial.println("===============================================");
    Serial.print("Firmware Version: ");
    Serial.println(FIRMWARE_VERSION);
    Serial.print("Build Date: ");
    Serial.print(BUILD_DATE);
    Serial.print(" ");
    Serial.println(BUILD_TIME);
    Serial.print("ESP32 Chip ID: ");
    Serial.println((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("Free Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    Serial.print("CPU Frequency: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");
    Serial.println("===============================================");
    Serial.println();
}

/**
 * Print complete button configuration table
 */
void printButtonConfiguration() {
    Serial.println("Button Configuration:");
    Serial.println("┌────────────────────────────────────────────────────────────────┐");
    Serial.println("│ Controller │    Button    │      Function      │      Type      │");
    Serial.println("├────────────────────────────────────────────────────────────────┤");
    
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        const ButtonConfig& btn = BUTTON_MAPPINGS[i];
        
        Serial.print("│ ");
        Serial.printf("%-10s", btn.controller);
        Serial.print(" │ ");
        Serial.printf("%-12s", btn.button);
        Serial.print(" │ ");
        Serial.printf("%-18s", btn.function);
        Serial.print(" │ ");
        
        if (btn.type == ActionType::MEDIA_KEY) {
            Serial.printf("%-14s", "Media Key");
        } else {
            Serial.print("Game Key (");
            Serial.print((char)btn.action.gameKey);
            Serial.print(")    ");
        }
        Serial.println(" │");
    }
    
    Serial.println("└────────────────────────────────────────────────────────────────┘");
    Serial.print("Total Buttons Configured: ");
    Serial.println(BUTTON_COUNT);
    Serial.println();
}

/**
 * Print configuration information
 */
void printConfiguration() {
    Serial.println("Configuration:");
    Serial.print("  Haptic Feedback: ");
    Serial.println(enableHapticFeedback ? "Enabled" : "Disabled");
    Serial.println();
    printButtonConfiguration();
}

/**
 * Print runtime statistics
 */
void printStats() {
    unsigned long uptime = (millis() - startTime) / 1000;
    Serial.println("Runtime Statistics:");
    Serial.print("  Uptime: ");
    Serial.print(uptime);
    Serial.println(" seconds");
    Serial.print("  Button Presses: ");
    Serial.println(buttonPressCount);
    Serial.print("  Connection Attempts: ");
    Serial.println(connectionAttempts);
    Serial.print("  Free Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    Serial.println();
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Find button configuration for given data index and code
 */
const ButtonConfig* findButtonConfig(uint8_t dataIndex, uint8_t code) {
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        if (BUTTON_MAPPINGS[i].dataIndex == dataIndex && BUTTON_MAPPINGS[i].code == code) {
            return &BUTTON_MAPPINGS[i];
        }
    }
    return nullptr;
}

/**
 * Execute the action for a button configuration
 */
void executeButtonAction(const ButtonConfig* config) {
    if (!config) return;

    buttonPressCount++;
    shouldVibrate = true;

    Serial.print("[");
    Serial.print(millis());
    Serial.print("ms] ");
    Serial.print(config->controller);
    Serial.print(" ");
    Serial.print(config->button);
    Serial.print(" -> ");
    Serial.print(config->function);
    Serial.print(" -> ");

    switch (config->type) {
        case ActionType::KEY_PRESS:
            bleKeyboard.write((char)config->action.gameKey);
            Serial.print("Key '");
            Serial.print((char)config->action.gameKey);
            Serial.println("'");
            break;
            
        case ActionType::MEDIA_KEY:
            switch (config->action.mediaKey) {
                case VOLUME_UP:
                    bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
                    Serial.println("KEY_MEDIA_VOLUME_UP");
                    break;
                case VOLUME_DOWN:
                    bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
                    Serial.println("KEY_MEDIA_VOLUME_DOWN");
                    break;
                case PREV_TRACK:
                    bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
                    Serial.println("KEY_MEDIA_PREVIOUS_TRACK");
                    break;
                case NEXT_TRACK:
                    bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
                    Serial.println("KEY_MEDIA_NEXT_TRACK");
                    break;
                case PLAY_PAUSE:
                    bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
                    Serial.println("KEY_MEDIA_PLAY_PAUSE");
                    break;
                default:
                    Serial.println("Unknown media key");
                    break;
            }
            break;
    }
}

/**
 * Send vibration command to the controller
 */
void vibrate() {
    if (pRxCharacteristic && pRxCharacteristic->canWriteNoResponse()) {
        // Base vibration pattern
        uint8_t basePattern[] = {0x12, 0x12, 0x08, 0x0A, 0x06, 0x08, 0x02, 0x10, 0x00, 0x18};
        const size_t baseLen = sizeof(basePattern);

        // Extended pattern with 0x20 appended
        uint8_t fullPattern[baseLen + 1];
        memcpy(fullPattern, basePattern, baseLen);
        fullPattern[baseLen] = 0x20;

        // Send the pattern
        pRxCharacteristic->writeValue(fullPattern, sizeof(fullPattern), false);
        Serial.println("[VIBRATE] Haptic feedback sent");
    } else {
        Serial.println("[ERROR] RX characteristic not writable or not available.");
    }
}

// =============================================================================
// BLE CALLBACK CLASSES
// =============================================================================

/**
 * Callback for BLE device scanning
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        Serial.print("[SCAN] Found device: ");
        Serial.print(advertisedDevice.getName().c_str());
        Serial.print(" (");
        Serial.print(advertisedDevice.getAddress().toString().c_str());
        Serial.print(") RSSI: ");
        Serial.print(advertisedDevice.getRSSI());
        Serial.println(" dBm");
        
        // BLE scan logic using the array
        for (int i = 0; i < sizeof(ZWIFT_CUSTOM_SERVICE_UUIDS) / sizeof(ZWIFT_CUSTOM_SERVICE_UUIDS[0]); ++i) {
            if (advertisedDevice.haveName() &&
                advertisedDevice.getName() == ZWIFT_RIDE_BLUETOOTH_NAME &&
                advertisedDevice.isAdvertisingService(BLEUUID(ZWIFT_CUSTOM_SERVICE_UUIDS[i]))) {

                Serial.print("[SCAN] ✓ Found Zwift Ride controller with service UUID_");
                Serial.print(i + 1);
                Serial.println("!");
                Serial.print("[SCAN] ✓ Service UUID: ");
                Serial.println(ZWIFT_CUSTOM_SERVICE_UUIDS[i]);

                detectedServiceUUID = ZWIFT_CUSTOM_SERVICE_UUIDS[i];
                BLEDevice::getScan()->stop();
                myDevice = new BLEAdvertisedDevice(advertisedDevice);
                doConnect = true;
                break;
            }
        }
        
        // Print the advertised service UUID for debugging
        if (advertisedDevice.haveServiceUUID()) {
            Serial.print("[SCAN] Advertised service: ");
            Serial.println(advertisedDevice.getServiceUUID().toString().c_str());
        }
        
    }
};

/**
 * Callback for BLE notifications from the controller
 */
static void notifyCallback(BLERemoteCharacteristic* pRemoteCharacteristic, 
                          uint8_t* pData, size_t length, bool isNotify) {
    
    // Handle button releases and send corresponding keystrokes
    // Logic triggers on button release (prevents multiple detections)
    
    // Handle pData[2] button releases
    if (prevData2 != 0xFF && pData[2] == 0xFF) {
        const ButtonConfig* config = findButtonConfig(2, prevData2);
        executeButtonAction(config);
    }

    // Handle pData[3] button releases
    if (prevData3 != 0xFF && pData[3] == 0xFF) {
        const ButtonConfig* config = findButtonConfig(3, prevData3);
        executeButtonAction(config);
    }

    // Handle pData[4] button releases
    if (prevData4 != 0xFF && pData[4] == 0xFF) {
        const ButtonConfig* config = findButtonConfig(4, prevData4);
        executeButtonAction(config);
    }

    // Save current states for next comparison
    prevData2 = pData[2];
    prevData3 = pData[3];
    prevData4 = pData[4];
}

// =============================================================================
// CONNECTION FUNCTIONS
// =============================================================================

/**
 * Connect to Zwift Ride controller and perform handshake
 */
bool connectAndHandshakeZwiftRide() {
    connectionAttempts++;
    Serial.print("[CONNECT] Attempt #");
    Serial.print(connectionAttempts);
    Serial.println(" - Starting BLE Client...");
    
    if (!detectedServiceUUID) {
        Serial.println("[ERROR] No Zwift service UUID detected!");
        return false;
    }
    
    Serial.print("[CONNECT] Using detected service UUID: ");
    Serial.println(detectedServiceUUID);
    
    BLEClient* pClient = BLEDevice::createClient();
    Serial.println("[CONNECT] Created BLE client");

    Serial.print("[CONNECT] Connecting to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    if (!pClient->connect(myDevice)) {
        Serial.println("[ERROR] Failed to connect to device");
        return false;
    }

    Serial.println("[CONNECT] ✓ Connected to device");

    // Connect to Controller Service using the detected UUID
    BLERemoteService* pService = pClient->getService(BLEUUID(detectedServiceUUID));
    if (pService == nullptr) {
        Serial.print("[ERROR] Zwift service not found with UUID: ");
        Serial.println(detectedServiceUUID);
        pClient->disconnect();
        return false;
    }

    Serial.print("[CONNECT] ✓ Found Zwift service with UUID: ");
    Serial.println(detectedServiceUUID);

    // Send handshake message to RX Characteristic
    Serial.println("[HANDSHAKE] Sending handshake...");
    pRxCharacteristic = pService->getCharacteristic(BLEUUID(ZWIFT_SYNC_RX_CHARACTERISTIC_UUID));
    if (pRxCharacteristic == nullptr) {
        Serial.println("[ERROR] RX characteristic not found");
        pClient->disconnect();
        return false;
    }

    String handshakeMessage = "RideOn";
    pRxCharacteristic->writeValue(handshakeMessage.c_str(), handshakeMessage.length());
    Serial.println("[HANDSHAKE] ✓ Sent 'RideOn' message");

    // Subscribe to the Async Characteristic for notifications
    pNotifyCharacteristic = pService->getCharacteristic(BLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID));
    if (pNotifyCharacteristic == nullptr) {
        Serial.println("[ERROR] Notify characteristic not found");
        pClient->disconnect();
        return false;
    }

    if (pNotifyCharacteristic->canNotify()) {
        pNotifyCharacteristic->registerForNotify(notifyCallback);
        Serial.println("[CONNECT] ✓ Subscribed to notifications");
        subscribed = true;
    }

    connected = true;
    Serial.println("[CONNECT] ✓ Connection established successfully!");
    return true;
}

/**
 * Start BLE scanning for Zwift Ride controller
 */
void startScanning() {
    Serial.println("[SCAN] Starting BLE scan...");
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setInterval(1349);
    pScan->setWindow(449);
    pScan->setActiveScan(true);
    pScan->start(15, false);
}

// =============================================================================
// MAIN ARDUINO FUNCTIONS
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000); // Give serial monitor time to connect
    
    startTime = millis();
    
    // Print startup information
    printStartupBanner();
    printConfiguration();

    Serial.println("[INIT] Initializing BLE Device...");
    BLEDevice::init("Zword");
    
    Serial.println("[INIT] Starting BLE Keyboard...");
    bleKeyboard.begin();

    // Start initial scan
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setInterval(1349);
    pScan->setWindow(449);
    pScan->setActiveScan(true);
    
    Serial.println("[INIT] Starting initial 30-second scan...");
    pScan->start(30, false);
}

void loop() {
    static unsigned long lastStatsTime = 0;
    
    // Print stats every 5 minutes
    if (millis() - lastStatsTime > 300000) {
        printStats();
        lastStatsTime = millis();
    }
    
    // Handle vibration requests
    if (shouldVibrate && enableHapticFeedback) {
        vibrate();
        shouldVibrate = false;
    }

    // Handle connection requests
    if (doConnect) {
        if (connectAndHandshakeZwiftRide()) {
            Serial.println("[SUCCESS] Connected to Zwift Ride controller");
            delay(500);
            vibrate(); // Initial connection vibration
            delay(2000);
            
            if (bleKeyboard.isConnected() && subscribed) {
                Serial.println("[SUCCESS] BLE Keyboard also connected - ready to go!");
                vibrate(); // Double vibration to confirm full setup
                delay(500);
                vibrate();
            }
            sentMessage = true;
            doConnect = false;
        } else {
            Serial.println("[RETRY] Connection failed, restarting scan in 5 seconds...");
            delay(5000);
            startScanning();
        }
    }

    delay(100);
}