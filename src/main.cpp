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

// BLE Keyboard initialization
BleKeyboard bleKeyboard("Zword", "Derguitarjo", 100);

// Zwift Ride Bluetooth UUIDs
const char* ZWIFT_CUSTOM_SERVICE_UUID = "0000FC82-0000-1000-8000-00805F9B34FB";
const char* ZWIFT_ASYNC_CHARACTERISTIC_UUID = "00000002-19CA-4651-86E5-FA29DCDD09D1"; // Notify
const char* ZWIFT_SYNC_RX_CHARACTERISTIC_UUID = "00000003-19CA-4651-86E5-FA29DCDD09D1"; // Write Without Response
const char* ZWIFT_SYNC_TX_CHARACTERISTIC_UUID = "00000004-19CA-4651-86E5-FA29DCDD09D1"; // Indicate, Read
const char* ZWIFT_Unknown_CHARACTERISTIC_UUID = "00000006-19CA-4651-86E5-FA29DCDD09D1"; // Indicate, Read, Write, Write Without Response

// =============================================================================
// BUTTON MAPPINGS
// =============================================================================

// Media key constants for better readability
#define MEDIA_VOLUME_UP     1
#define MEDIA_VOLUME_DOWN   2
#define MEDIA_PREV_TRACK    3
#define MEDIA_NEXT_TRACK    4
#define MEDIA_PLAY_PAUSE    5

// Mywoosh key mappings
#define KEY_PAUSE       ' '   // Space = Pause
#define KEY_HIDE_UI     'u'   // Hide/unhide UI
#define KEY_SHIFT_DOWN  'i'   // Shift down
#define KEY_SHIFT_UP    'k'   // Shift up
#define KEY_PEACE       '1'   // Peace emote
#define KEY_HELLO       '2'   // Hello emote
#define KEY_POWER       '3'   // Power emote
#define KEY_DAB         '4'   // Dab emote
#define KEY_AGAIN       '5'   // Again emote
#define KEY_BATTERY_LOW '6'   // Battery low emote
#define KEY_THUMBS_UP   '7'   // Thumbs up emote

enum class ActionType {
    KEY_PRESS,
    MEDIA_KEY
};

struct ButtonMapping {
    uint8_t code;
    ActionType type;
    const char* description;  // For logging
    union {
        char key;           // For regular key presses
        uint8_t mediaKey;   // For media keys
    } action;
};

// Button mapping arrays with cleaner media key handling
const ButtonMapping pData2Mappings[] = {
    {0xFD, ActionType::MEDIA_KEY, "Left Up (Volume Up)", {.mediaKey = MEDIA_VOLUME_UP}},
    {0xF7, ActionType::MEDIA_KEY, "Left Down (Volume Down)", {.mediaKey = MEDIA_VOLUME_DOWN}},
    {0xFE, ActionType::MEDIA_KEY, "Left Left (Previous Track)", {.mediaKey = MEDIA_PREV_TRACK}},
    {0xFB, ActionType::MEDIA_KEY, "Left Right (Next Track)", {.mediaKey = MEDIA_NEXT_TRACK}},
    {0xEF, ActionType::KEY_PRESS, "Right A (Hello)", {.key = KEY_HELLO}},
    {0xBF, ActionType::KEY_PRESS, "Right Y (Hide UI)", {.key = KEY_HIDE_UI}},
    {0xDF, ActionType::KEY_PRESS, "Right B (Battery Low)", {.key = KEY_BATTERY_LOW}},
};

const ButtonMapping pData3Mappings[] = {
    {0xEF, ActionType::MEDIA_KEY, "Left Power (Play/Pause)", {.mediaKey = MEDIA_PLAY_PAUSE}},
    {0xFD, ActionType::KEY_PRESS, "Left Side Upper (Shift Down)", {.key = KEY_SHIFT_DOWN}},
    {0xFB, ActionType::KEY_PRESS, "Left Side Middle (Shift Down)", {.key = KEY_SHIFT_DOWN}},
    {0xF7, ActionType::KEY_PRESS, "Left Side Lower (Shift Down)", {.key = KEY_SHIFT_DOWN}},
    {0xFE, ActionType::KEY_PRESS, "Right Z (Thumbs Up)", {.key = KEY_THUMBS_UP}},
    {0xDF, ActionType::KEY_PRESS, "Right Side Upper (Shift Up)", {.key = KEY_SHIFT_UP}},
    {0xBF, ActionType::KEY_PRESS, "Right Side Middle (Shift Up)", {.key = KEY_SHIFT_UP}},
};

const ButtonMapping pData4Mappings[] = {
    {0xFD, ActionType::KEY_PRESS, "Right Power (Pause)", {.key = KEY_PAUSE}},
    {0xFE, ActionType::KEY_PRESS, "Right Side Lower (Shift Up)", {.key = KEY_SHIFT_UP}},
};

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
 * Print configuration information
 */
void printConfiguration() {
    Serial.println("Configuration:");
    Serial.print("  Haptic Feedback: ");
    Serial.println(enableHapticFeedback ? "Enabled" : "Disabled");
    Serial.print("  Button Mappings: ");
    Serial.print(sizeof(pData2Mappings)/sizeof(ButtonMapping) + 
                 sizeof(pData3Mappings)/sizeof(ButtonMapping) + 
                 sizeof(pData4Mappings)/sizeof(ButtonMapping));
    Serial.println(" total");
    Serial.println();
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
 * Find the button mapping for a given button code
 */
const ButtonMapping* findButtonMapping(const ButtonMapping* mappings, size_t size, uint8_t code) {
    for (size_t i = 0; i < size; ++i) {
        if (mappings[i].code == code) {
            return &mappings[i];
        }
    }
    return nullptr;  // No mapping found
}

/**
 * Execute the action for a button mapping
 */
void executeButtonAction(const ButtonMapping* mapping) {
    if (!mapping) return;

    buttonPressCount++;
    shouldVibrate = true;

    Serial.print("[");
    Serial.print(millis());
    Serial.print("ms] Button: ");
    Serial.print(mapping->description);
    Serial.print(" -> ");

    switch (mapping->type) {
        case ActionType::KEY_PRESS:
            bleKeyboard.write(mapping->action.key);
            Serial.print("Key '");
            Serial.print(mapping->action.key);
            Serial.println("'");
            break;
            
        case ActionType::MEDIA_KEY:
            switch (mapping->action.mediaKey) {
                case MEDIA_VOLUME_UP:
                    bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
                    Serial.println("Media: Volume Up");
                    break;
                case MEDIA_VOLUME_DOWN:
                    bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
                    Serial.println("Media: Volume Down");
                    break;
                case MEDIA_PREV_TRACK:
                    bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
                    Serial.println("Media: Previous Track");
                    break;
                case MEDIA_NEXT_TRACK:
                    bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
                    Serial.println("Media: Next Track");
                    break;
                case MEDIA_PLAY_PAUSE:
                    bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
                    Serial.println("Media: Play/Pause");
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
        Serial.println(")");
        
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(BLEUUID(ZWIFT_CUSTOM_SERVICE_UUID))) {
            Serial.println("[SCAN] ✓ Found Zwift Ride controller!");
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
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
        const ButtonMapping* mapping = findButtonMapping(pData2Mappings, 
                                                       sizeof(pData2Mappings)/sizeof(ButtonMapping), 
                                                       prevData2);
        executeButtonAction(mapping);
    }

    // Handle pData[3] button releases
    if (prevData3 != 0xFF && pData[3] == 0xFF) {
        const ButtonMapping* mapping = findButtonMapping(pData3Mappings, 
                                                       sizeof(pData3Mappings)/sizeof(ButtonMapping), 
                                                       prevData3);
        executeButtonAction(mapping);
    }

    // Handle pData[4] button releases
    if (prevData4 != 0xFF && pData[4] == 0xFF) {
        const ButtonMapping* mapping = findButtonMapping(pData4Mappings, 
                                                       sizeof(pData4Mappings)/sizeof(ButtonMapping), 
                                                       prevData4);
        executeButtonAction(mapping);
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
    
    BLEClient* pClient = BLEDevice::createClient();
    Serial.println("[CONNECT] Created BLE client");

    Serial.print("[CONNECT] Connecting to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    if (!pClient->connect(myDevice)) {
        Serial.println("[ERROR] Failed to connect to device");
        return false;
    }

    Serial.println("[CONNECT] ✓ Connected to device");

    // Connect to Controller Service
    BLERemoteService* pService = pClient->getService(BLEUUID(ZWIFT_CUSTOM_SERVICE_UUID));
    if (pService == nullptr) {
        Serial.println("[ERROR] Zwift service not found");
        pClient->disconnect();
        return false;
    }

    Serial.println("[CONNECT] ✓ Found Zwift service");

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