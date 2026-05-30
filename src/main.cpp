#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>
#include <BLESecurity.h> 
#include <nvs_flash.h>   
#include <esp_gap_ble_api.h>

const int buttonPin = 0; // ESP32-S3 板载 BOOT 按键

// 状态机标志位
bool isConnected = false; 
bool isEncrypted = false; // 核心标志：加密链路是否已建立！

BLEHIDDevice* hid = NULL;
BLECharacteristic* inputReport = NULL;      // 通道 1：普通键盘 (8字节)
BLECharacteristic* inputReportMedia = NULL; // 通道 2：多媒体 (2字节)

// ==========================================
// 1. 清空 Flash 中的配对记忆 (长按触发) - 修复版
// ==========================================
void clearAllBondedDevices() {
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
        
        // 修复 1：去掉了 _gap_，使用新版 API
        esp_ble_get_bond_device_list(&dev_num, dev_list);
        
        for (int i = 0; i < dev_num; i++) {
            // 修复 2：去掉了 _gap_，使用新版 API
            esp_ble_remove_bond_device(dev_list[i].bd_addr);
        }
        free(dev_list);
        Serial.println("🧨 已彻底清空 Flash 中的所有蓝牙配对记录(LTK)！请在手机端取消配对后重连。");
    } else {
        Serial.println("Flash 中目前没有配稳记录。");
    }
}

// ==========================================
// 2. 发送动作指令 (严格区分通道与字节长度)
// ==========================================
// 动作 A：方向下键 (走键盘通道，8字节)
void sendDownArrow() {
    if (!isEncrypted) return; // 加密未完成前，禁止发送
    uint8_t pressMsg[8] = {0x00, 0x00, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(pressMsg, sizeof(pressMsg));
    inputReport->notify();
    
    delay(80); 
    
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg));
    inputReport->notify();
}

// 动作 B：下一首 (走多媒体通道，2字节)
// 动作 B：发送空格键 (走普通键盘通道，8字节)
void sendSpace() {
    if (!isEncrypted) return; 
    
    // 0x2C 是空格键的 HID 码，放在第 3 个位置
    uint8_t pressMsg[8] = {0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    // ⚠️ 极其关键：因为是普通按键，必须使用 inputReport！绝不能用 inputReportMedia！
    inputReport->setValue(pressMsg, sizeof(pressMsg));
    inputReport->notify();
    
    delay(80); 
    
    // 释放信号全部置零
    uint8_t releaseMsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    inputReport->setValue(releaseMsg, sizeof(releaseMsg));
    inputReport->notify();

    delay(80); 
    
    // ⚠️ 极其关键：因为是普通按键，必须使用 inputReport！绝不能用 inputReportMedia！
    inputReport->setValue(pressMsg, sizeof(pressMsg));
    inputReport->notify();
    
    delay(80); 
    
    // 释放信号全部置零
    inputReport->setValue(releaseMsg, sizeof(releaseMsg));
    inputReport->notify();
}

// ==========================================
// 3. 蓝牙底层回调 (修复“断电失聪”的核心)
// ==========================================
// 安全握手监听
class MySecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() { return 123456; }
    void onPassKeyNotify(uint32_t pass_key) {}
    bool onConfirmPIN(uint32_t pass_key) { return true; }
    bool onSecurityRequest() { return true; }
    
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
        if (cmpl.success) {
            isEncrypted = true;
            Serial.println("🔒 蓝牙加密链路已重建！(按键现已完全生效)");
        } else {
            isEncrypted = false;
            Serial.println("❌ 蓝牙加密失败！请长按按键重置，并在手机端重新配对。");
        }
    }
};

// 物理连接监听
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        isConnected = true;
        Serial.println("🔗 物理链路已连接，正在强行唤醒 CCCD 通知描述符...");
        
        // 核心修复：强行打开键盘和多媒体通道的 CCCD (0x2902)
        if (inputReport != NULL) {
            BLEDescriptor* pDesc = inputReport->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
            if (pDesc) { uint8_t descVal[] = {0x01, 0x00}; pDesc->setValue(descVal, 2); }
        }
        if (inputReportMedia != NULL) {
            BLEDescriptor* pDesc = inputReportMedia->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
            if (pDesc) { uint8_t descVal[] = {0x01, 0x00}; pDesc->setValue(descVal, 2); }
        }
    }
    void onDisconnect(BLEServer* pServer) {
        isConnected = false;
        isEncrypted = false;
        Serial.println("❌ 主机已断开，重新开始广播...");
        pServer->startAdvertising(); 
    }
};

// ==========================================
// 4. 合并版描述符 (支持普通按键 + 多媒体按键)
// ==========================================
const uint8_t hidReportMap[] = {
    // --- 通道 1: 普通键盘 ---
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report Id (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) 
    0xC0,        // End Collection

    // --- 通道 2: 多媒体控制 ---
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x02,  //   Report Id (2)
    0x15, 0x00,  //   Logical minimum (0)
    0x26, 0xFF, 0x03, // Logical maximum (0x3FF)
    0x19, 0x00,  //   Usage Minimum (0)
    0x2A, 0xFF, 0x03, // Usage Maximum (0x3FF)
    0x75, 0x10,  //   Report Size (16) -> 2 bytes
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x00,  //   Input (Data,Array,Absolute)
    0xC0         // End Collection
};

// ==========================================
// 初始化与主循环
// ==========================================
void setup() {
    Serial.begin(115200);
    pinMode(buttonPin, INPUT_PULLUP);
    
    // 初始化 NVS，为了能存 LTK 密钥
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    BLEDevice::init("ESP32 Ultimate KB");
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks()); // 注册安全监听

    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    hid = new BLEHIDDevice(pServer);
    // 绑定描述符通道
    inputReport = hid->inputReport(1);       // 对应 Report ID 1
    inputReportMedia = hid->inputReport(2);  // 对应 Report ID 2

    hid->manufacturer()->setValue("Espressif");
    hid->pnp(0x02, 0x0E8D, 0x0300, 0x0100);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t*)hidReportMap, sizeof(hidReportMap));
    hid->startServices();

    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->setAppearance(HID_KEYBOARD); 
    pAdvertising->addServiceUUID(hid->hidService()->getUUID());
    pAdvertising->setMinPreferred(0x06); // 安卓防吞键参数
    pAdvertising->setMaxPreferred(0x12); 
    pAdvertising->start();

    // 核心安全级别：安全连接 + 绑定
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND); 
    pSecurity->setCapability(ESP_IO_CAP_NONE); 
    uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t respKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    pSecurity->setInitEncryptionKey(initKey);
    pSecurity->setRespEncryptionKey(respKey);
    uint32_t passkey = 123456;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));

    Serial.println("🚀 究极版蓝牙键盘启动完毕，等待连接...");
}

void loop() {
    // 按键逻辑：必须在连接且【加密成功】后才能触发
    if (isConnected) {
        int currentButtonState = digitalRead(buttonPin);
        static int lastButtonState = HIGH;
        static unsigned long lastDebounceTime = 0;
        
        static unsigned long pressStartTime = 0;
        static bool longPressHandled = false;

        static int clickCount = 0;             
        static unsigned long firstClickTime = 0; 
        const unsigned long doubleClickWindow = 250; 

        // 软件消抖
        if (currentButtonState != lastButtonState) {
            lastDebounceTime = millis();
        }

        if ((millis() - lastDebounceTime) > 30) {
            static int stableButtonState = HIGH;
            if (currentButtonState != stableButtonState) {
                stableButtonState = currentButtonState;
                
                if (stableButtonState == LOW) { 
                    pressStartTime = millis();
                    longPressHandled = false;
                } else {
                    if (pressStartTime > 0 && !longPressHandled) {
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

            // 长按 3 秒清空配对
            if (stableButtonState == LOW && pressStartTime > 0) {
                if ((millis() - pressStartTime > 3000) && !longPressHandled) {
                    clearAllBondedDevices();
                    longPressHandled = true; 
                }
            }
        }
        lastButtonState = currentButtonState;

        // 执行短按与双击（只在加密链路通畅时执行）
        // 执行短按与双击（只在加密链路通畅时执行）
        if (clickCount > 0 && pressStartTime == 0) {
            if (clickCount == 2) {
                // 修改这里：打印提示并调用 sendSpace
                Serial.println("👇 双击触发 -> 空格键 (Keyboard: 0x2C)");
                sendSpace();
                clickCount = 0; 
            } 
            else if (millis() - firstClickTime > doubleClickWindow) {
                Serial.println("👇 单击触发 -> 方向下键 (Keyboard: 0x51)");
                sendDownArrow();
                clickCount = 0; 
            }
        }
    } else {
        // 断线状态下依然允许长按重置
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