#include <Arduino.h>
#include <BLEDevice.h>
#include <BleKeyboard.h>

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
#define KEY_MEDIA       '<'   // Media key flag

struct ButtonMapping {
    uint8_t code;  // From pData[x]
    char key;      // Mapped key
};

// Button mapping arrays
const ButtonMapping pData2Mappings[] = {
    {0xFD, KEY_MEDIA},        // Left Controller Up
    {0xF7, KEY_MEDIA},        // Left Controller Down
    {0xFE, KEY_MEDIA},        // Left Controller Left
    {0xFB, KEY_MEDIA},        // Left Controller Right
    {0xEF, KEY_HELLO},        // Right Controller A Button
    {0xBF, KEY_HIDE_UI},      // Right Controller Y Button
    {0xDF, KEY_BATTERY_LOW},  // Right Controller B Button
};

const ButtonMapping pData3Mappings[] = {
    {0xEF, KEY_MEDIA},        // Left Controller Power
    {0xFD, KEY_SHIFT_DOWN},   // Left Controller Side Upper Button
    {0xFB, KEY_SHIFT_DOWN},   // Left Controller Side Middle Button
    {0xF7, KEY_SHIFT_DOWN},   // Left Controller Side Lower Button
    {0xFE, KEY_THUMBS_UP},    // Right Controller Z Button
    {0xDF, KEY_SHIFT_UP},     // Right Controller Side Upper Button
    {0xBF, KEY_SHIFT_UP},     // Right Controller Side Middle Button
};

const ButtonMapping pData4Mappings[] = {
    {0xFD, KEY_PAUSE},        // Right Controller Power Button
    {0xFE, KEY_SHIFT_UP},     // Right Controller Side Lower Button
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

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Find the mapped key for a given button code
 */
char getMappedKey(const ButtonMapping* mappings, size_t size, uint8_t code) {
    for (size_t i = 0; i < size; ++i) {
        if (mappings[i].code == code) {
            return mappings[i].key;
        }
    }
    return 0;  // No mapping found
}

/**
 * Handle media key events (volume, track control, play/pause)
 */
void mediaKeyHandler(uint8_t position, uint8_t key) {
    Serial.println("MediaKeyEvent");
    
    switch (position) {
        case 2:
            switch (key) {
                case 0xFD: 
                    bleKeyboard.write(KEY_MEDIA_VOLUME_UP);     
                    Serial.println("Released UP");          
                    break;
                case 0xF7: 
                    bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);   
                    Serial.println("Released DOWN");        
                    break;
                case 0xFE: 
                    bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
                    Serial.println("Released LEFT");        
                    break;
                case 0xFB: 
                    bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);    
                    Serial.println("Released RIGHT");       
                    break;
            }
            shouldVibrate = true;
            break;
            
        case 3:
            switch(key) {
                case 0xEF: 
                    bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);     
                    Serial.println("Released LEFT Power");    
                    break;
            }
            shouldVibrate = true;
            break;
            
        case 4:
            // No media keys currently mapped for position 4
            shouldVibrate = true;
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
    } else {
        Serial.println("RX characteristic not writable or not available.");
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
        Serial.println("Scanning...");
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(BLEUUID(ZWIFT_CUSTOM_SERVICE_UUID))) {
            Serial.println("Found Zwift Ride");
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
        char mappedKey = getMappedKey(pData2Mappings, sizeof(pData2Mappings)/sizeof(ButtonMapping), prevData2);
        if (mappedKey) {
            if (mappedKey == KEY_MEDIA) {
                mediaKeyHandler(2, prevData2);
            } else {
                bleKeyboard.write(mappedKey);
                Serial.println(mappedKey);
                shouldVibrate = true;
            }
        }
    }

    // Handle pData[3] button releases
    if (prevData3 != 0xFF && pData[3] == 0xFF) {
        char mappedKey = getMappedKey(pData3Mappings, sizeof(pData3Mappings)/sizeof(ButtonMapping), prevData3);
        if (mappedKey) {
            if (mappedKey == KEY_MEDIA) {
                mediaKeyHandler(3, prevData3);
            } else {
                bleKeyboard.write(mappedKey);
                Serial.println(mappedKey);
                shouldVibrate = true;
            }
        }
    }

    // Handle pData[4] button releases
    if (prevData4 != 0xFF && pData[4] == 0xFF) {
        char mappedKey = getMappedKey(pData4Mappings, sizeof(pData4Mappings)/sizeof(ButtonMapping), prevData4);
        if (mappedKey) {
            if (mappedKey == KEY_MEDIA) {
                mediaKeyHandler(4, prevData4);
            } else {
                bleKeyboard.write(mappedKey);
                Serial.println(mappedKey);
                shouldVibrate = true;
            }
        }
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
    Serial.println("Starting the BLE Client...");
    BLEClient* pClient = BLEDevice::createClient();
    Serial.println("Created BLE client");

    if (!pClient->connect(myDevice)) {
        Serial.println("Failed to connect");
        return false;
    }

    Serial.println("Connected to Service");

    // Connect to Controller Service
    BLERemoteService* pService = pClient->getService(BLEUUID(ZWIFT_CUSTOM_SERVICE_UUID));
    if (pService == nullptr) {
        Serial.println("Service not found");
        pClient->disconnect();
        return false;
    }

    // Send handshake message to RX Characteristic
    Serial.println("Sending Handshake...");
    pRxCharacteristic = pService->getCharacteristic(BLEUUID(ZWIFT_SYNC_RX_CHARACTERISTIC_UUID));
    if (pRxCharacteristic == nullptr) {
        Serial.println("RX characteristic not found");
        pClient->disconnect();
        return false;
    }

    String handshakeMessage = "RideOn";
    pRxCharacteristic->writeValue(handshakeMessage.c_str(), handshakeMessage.length());

    // Subscribe to the Async Characteristic for notifications
    pNotifyCharacteristic = pService->getCharacteristic(BLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID));
    if (pNotifyCharacteristic == nullptr) {
        Serial.println("Notify characteristic not found");
        pClient->disconnect();
        return false;
    }

    if (pNotifyCharacteristic->canNotify()) {
        pNotifyCharacteristic->registerForNotify(notifyCallback);
        Serial.println("Subscribed to notifications");
        subscribed = true;
    }

    connected = true;
    return true;
}

/**
 * Start BLE scanning for Zwift Ride controller
 */
void startScanning() {
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
    Serial.println("Starting BLE Client + HID Keyboard");

    BLEDevice::init("Zword");
    bleKeyboard.begin();

    // Start initial scan
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setInterval(1349);
    pScan->setWindow(449);
    pScan->setActiveScan(true);
    pScan->start(30, false);
}

void loop() {
    // Handle vibration requests
    if (shouldVibrate && enableHapticFeedback) {
        vibrate();
        shouldVibrate = false;
    }

    // Handle connection requests
    if (doConnect) {
        if (connectAndHandshakeZwiftRide()) {
            Serial.println("Connected to Controller device");
            delay(500);
            vibrate(); // Initial connection vibration
            delay(2000);
            
            if (bleKeyboard.isConnected() && subscribed) {
                vibrate(); // Double vibration to confirm full setup
                delay(500);
                vibrate();
            }
            sentMessage = true;
            doConnect = false;
        } else {
            Serial.println("Connection failed, restarting scan...");
            startScanning();
        }
    }

    delay(100);
}