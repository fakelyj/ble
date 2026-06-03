#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>
#include <BLESecurity.h> 
#include <nvs_flash.h>   
#include <esp_gap_ble_api.h>

// ==========================================
// 引脚定义与配置
// ==========================================
const int buttonPin = 15; // 主按键: 3.3V NC端 (平时HIGH，按下变LOW)
const int btnXPin = 1;    // 辅助键: 3.3V NO端 (平时LOW，按下变HIGH)

// ==========================================
// 全局变量声明
// ==========================================
bool isConnected = false; 
bool isEncrypted = false; 

BLEHIDDevice* hid = NULL;
BLECharacteristic* inputReport = NULL;      
BLECharacteristic* inputReportMedia = NULL; 

// ==========================================
// 功能函数：清空蓝牙配对
// ==========================================
void clearAllBondedDevices() {
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
        esp_ble_get_bond_device_list(&dev_num, dev_list);
        for (int i = 0; i < dev_num; i++) {
            esp_ble_remove_bond_device(dev_list[i].bd_addr);
        }
        free(dev_list);
        Serial.println("🧨 已清空配对记录！请在手机取消配对后重连。");
    } else {
        Serial.println("Flash 中无记录。");
    }
}

// ==========================================
// 发送指令函数库 (走键盘通道，8字节)
// ==========================================
void sendDownArrow() {
    if (!isEncrypted) return; 
    uint8_t pressMsg[8] = {0x00, 0x00, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(pressMsg, sizeof(pressMsg)); inputReport->notify();
    delay(80); 
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg)); inputReport->notify();
}

void sendUpArrow() {
    if (!isEncrypted) return; 
    uint8_t pressMsg[8] = {0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(pressMsg, sizeof(pressMsg)); inputReport->notify();
    delay(80); 
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg)); inputReport->notify();
}

// 新增：发送方向右键 (0x4F)
void sendRightArrow() {
    if (!isEncrypted) return; 
    uint8_t pressMsg[8] = {0x00, 0x00, 0x4F, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(pressMsg, sizeof(pressMsg)); inputReport->notify();
    delay(80); 
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg)); inputReport->notify();
}

void sendZ() {
    if (!isEncrypted) return; 
    uint8_t pressMsg[8] = {0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(pressMsg, sizeof(pressMsg)); inputReport->notify();
    delay(80); 
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg)); inputReport->notify();
}

void sendX() {
    if (!isEncrypted) return; 
    uint8_t pressMsg[8] = {0x00, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(pressMsg, sizeof(pressMsg)); inputReport->notify();
    delay(80); 
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg)); inputReport->notify();
}

// ==========================================
// 蓝牙底层回调
// ==========================================
class MySecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() { return 123456; }
    void onPassKeyNotify(uint32_t pass_key) {}
    bool onConfirmPIN(uint32_t pass_key) { return true; }
    bool onSecurityRequest() { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
        if (cmpl.success) {
            isEncrypted = true;
            Serial.println("🔒 加密链路已重建！");
        } else {
            isEncrypted = false;
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        isConnected = true;
        Serial.println("🔗 物理链路已连接");
        if (inputReport != NULL) {
            BLEDescriptor* pDesc = inputReport->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
            if (pDesc) { uint8_t descVal[] = {0x01, 0x00}; pDesc->setValue(descVal, 2); }
        }
    }
    void onDisconnect(BLEServer* pServer) {
        isConnected = false;
        isEncrypted = false;
        Serial.println("❌ 已断开");
        pServer->startAdvertising(); 
    }
};

// ==========================================
// HID 描述符 (键盘通道)
// ==========================================
const uint8_t hidReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0         
};

// ==========================================
// 初始化
// ==========================================
void setup() {
    Serial.begin(115200);
    
    pinMode(buttonPin, INPUT_PULLDOWN); 
    pinMode(btnXPin, INPUT_PULLDOWN);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    BLEDevice::init("ESP32 Ultimate KB");
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks()); 
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    hid = new BLEHIDDevice(pServer);
    inputReport = hid->inputReport(1); 
    hid->manufacturer()->setValue("Espressif");
    hid->pnp(0x02, 0x0E8D, 0x0300, 0x0100);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t*)hidReportMap, sizeof(hidReportMap));
    hid->startServices();

    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->setAppearance(HID_KEYBOARD); 
    pAdvertising->addServiceUUID(hid->hidService()->getUUID());
    pAdvertising->setMinPreferred(0x06); 
    pAdvertising->setMaxPreferred(0x12); 
    pAdvertising->start();

    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND); 
    pSecurity->setCapability(ESP_IO_CAP_NONE); 
    uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t respKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    pSecurity->setInitEncryptionKey(initKey);
    pSecurity->setRespEncryptionKey(respKey);
    uint32_t passkey = 123456;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));

    Serial.println("🚀 蓝牙启动...");
}

// ==========================================
// 主循环状态机
// ==========================================
void loop() {
    if (isConnected) {
        const unsigned long doubleClickWindow = 200; 
        const unsigned long debounceDelay = 20;      

        // --- 模块 1：主按键 (引脚 15, NC模式) ---
        int currentButtonState = digitalRead(buttonPin);
        static int lastButtonState = HIGH;
        static unsigned long lastDebounceTime = 0;
        
        static unsigned long pressStartTime = 0;
        static bool shortPressHandled = false; 
        static bool resetPressHandled = false; 

        static int clickCount = 0;             
        static unsigned long firstClickTime = 0; 

        if (currentButtonState != lastButtonState) lastDebounceTime = millis();

        if ((millis() - lastDebounceTime) > debounceDelay) {
            static int stableButtonState = HIGH;
            if (currentButtonState != stableButtonState) {
                stableButtonState = currentButtonState;
                
                if (stableButtonState == LOW) { 
                    pressStartTime = millis();
                    shortPressHandled = false;   
                    resetPressHandled = false; 
                } else {
                    if (pressStartTime > 0 && !shortPressHandled && !resetPressHandled) {
                        if (clickCount == 0) {
                            firstClickTime = millis();
                            clickCount = 1;
                        } else if (clickCount == 1 && (millis() - firstClickTime < doubleClickWindow)) {
                            clickCount = 2; 
                        }
                    }
                    pressStartTime = 0;
                }
            }

            if (stableButtonState == LOW && pressStartTime > 0) {
                unsigned long pressDuration = millis() - pressStartTime;
                
                if (pressDuration > 250 && !shortPressHandled) {
                    Serial.println("👆 主键长按 -> 方向上 (0x52)");
                    sendUpArrow();
                    shortPressHandled = true; 
                }
                
                if (pressDuration > 3000 && !resetPressHandled) {
                    clearAllBondedDevices();
                    resetPressHandled = true; 
                }
            }
        }
        lastButtonState = currentButtonState;

        if (clickCount > 0 && pressStartTime == 0) {
            if (clickCount == 2) {
                Serial.println("👇 主键双击 -> 字母 Z (0x1D)");
                sendZ();
                clickCount = 0; 
            } 
            else if (millis() - firstClickTime > doubleClickWindow) {
                Serial.println("👇 主键单击 -> 方向下 (0x51)");
                sendDownArrow();
                clickCount = 0; 
            }
        }

        // --- 模块 2：辅助按键 X (引脚 1, NO 模式 + 连发逻辑) ---
        int currentBtnXState = digitalRead(btnXPin);
        static int lastBtnXState = LOW;
        static unsigned long lastBtnXDebounceTime = 0;
        static int stableBtnXState = LOW;

        if (currentBtnXState != lastBtnXState) lastBtnXDebounceTime = millis();
        if ((millis() - lastBtnXDebounceTime) > debounceDelay) {
            stableBtnXState = currentBtnXState;
        }
        lastBtnXState = currentBtnXState;

        static unsigned long btnXPressStartTime = 0;
        static unsigned long lastAutoFireTime = 0;
        static bool xLongPressActive = false; // 记录是否触发了长按连发

        if (stableBtnXState == HIGH) {
            // 按下瞬间
            if (btnXPressStartTime == 0) {
                btnXPressStartTime = millis();
                xLongPressActive = false;
                lastAutoFireTime = millis(); // 初始化连发计时器
            } 
            // 持续按压中
            else {
                unsigned long heldTime = millis() - btnXPressStartTime;
                if (heldTime >= 250) { // 长按超过 250ms
                    xLongPressActive = true; 
                    // 每间隔 800ms 触发一次方向右键
                    if (millis() - lastAutoFireTime >= 800) {
                        Serial.println("👉 X键连发 -> 方向右 (0x4F)");
                        sendRightArrow();
                        lastAutoFireTime = millis(); // 重置下次触发时间
                    }
                }
            }
        } else {
            // 松开瞬间
            if (btnXPressStartTime > 0) {
                // 如果松开时没有触发过长按连发，说明是短按，发送一次 X
                if (!xLongPressActive) {
                    Serial.println("✖️ X键短按 -> 字母 X (0x1B)");
                    sendX();
                }
                // 彻底重置状态
                btnXPressStartTime = 0;
                xLongPressActive = false;
            }
        }

    } else {
        // 断网状态下的重置依然保留
        if (digitalRead(buttonPin) == LOW) {
            delay(3000);
            if (digitalRead(buttonPin) == LOW) {
                clearAllBondedDevices();
                while(digitalRead(buttonPin) == LOW) { delay(10); } 
            }
        }
    }
    delay(10); 
}