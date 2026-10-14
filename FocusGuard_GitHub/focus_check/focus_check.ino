/*
 * 讀書專注小幫手 — 第一週基本辨識驗證（AMB82-MINI）
 *
 * 改自 Realtek 範例 NeuralNetwork/ObjectDetectionCallback（YOLOv4-tiny，COCO 80 類）。
 * 自行修改的部分：
 *   1. 只看 person（0）與 cell phone（67）兩類，其餘類別不畫框、不判斷
 *   2. 依信心值把每一幀分成四種狀態：NO_TARGET / PERSON / UNSURE / PHONE
 *   3. 狀態帶動 LED：LED_G（D24，綠）= 有人，LED_B（D23，藍）= 確定有手機
 *   4. 序列埠在狀態改變時、每秒一次、以及每一幀（F,...）輸出，方便錄影與統計
 *   5. RTSP 畫面左上角疊上目前狀態文字；串流降為 720p、1 Mbps，Wi-Fi 斷線會自動重連
 *
 * Wi-Fi 帳密放在 secrets.h（不上傳）。連不上 Wi-Fi 時仍會辨識並控制 LED，並在 loop() 持續重連。
 */

#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNObjectDetection.h"
#include "VideoStreamOverlay.h"
#include "ObjectClassList.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID "Network_SSID"
#define WIFI_PASS "Password"
#endif

#define CHANNEL   0
#define CHANNELNN 3

#define NNWIDTH  576
#define NNHEIGHT 320

constexpr int CLASS_PERSON = 0;
constexpr int CLASS_PHONE = 67;

// 模型最低輸出門檻設為 0.30，讓 30～49 分的「不確定」結果也能被看到
constexpr float MODEL_MIN_CONFIDENCE = 0.30f;
constexpr float MODEL_NMS = 0.30f;
constexpr int CONFIRM_SCORE = 50;    // ≥ 50 才算確定
constexpr int UNSURE_SCORE = 30;     // 30～49 視為不確定，不觸發 LED

constexpr uint8_t PIN_PHONE_LED = LED_B;
constexpr uint8_t PIN_PERSON_LED = LED_G;
constexpr unsigned long HEARTBEAT_MS = 1000;
constexpr unsigned long WIFI_CHECK_MS = 5000;
// 每一幀輸出一行 F,時間,person,phone,狀態，給 scripts/placement_test.ps1 統計用
constexpr bool LOG_EVERY_FRAME = true;

enum FrameState { NO_TARGET, PERSON, UNSURE, PHONE };
const char* const STATE_NAMES[] = {"NO_TARGET", "PERSON", "UNSURE", "PHONE"};
// OSD 字型沒有底線，畫面上改用空格
const char* const OSD_STATE_NAMES[] = {"NO TARGET", "PERSON", "UNSURE", "PHONE"};

// 串流用 720p、1 Mbps，在擁擠的 Wi-Fi 上比較順；辨識另外走 576x320 的 RGB 通道，不受影響
VideoSetting config(VIDEO_HD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNObjectDetection ObjDet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

FrameState lastState = NO_TARGET;
unsigned long lastHeartbeat = 0;
unsigned long frameCount = 0;

int beginWiFi()
{
    // 密碼留空代表開放式網路（例如校園 guest 網路）
    return (pass[0] == '\0') ? WiFi.begin(ssid) : WiFi.begin(ssid, pass);
}

bool connectWiFi()
{
#ifdef WIFI_STATIC_IP
    // 部分手機熱點（例如 iPhone）的 DHCP 不回應開發板，改用固定 IP
    WiFi.config(IPAddress(WIFI_STATIC_IP), IPAddress(WIFI_GATEWAY), IPAddress(WIFI_GATEWAY), IPAddress(WIFI_SUBNET));
#endif
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.print("Connecting to Wi-Fi: ");
        Serial.println(ssid);
        if (beginWiFi() == WL_CONNECTED) {
            return true;
        }
        delay(2000);
    }
    Serial.println("Wi-Fi not connected yet, will keep retrying in loop() (detection + LED keep running)");
    return false;
}

void printRtspUrl()
{
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("RTSP: rtsp://");
        Serial.print(WiFi.localIP());
        Serial.print(":");
        Serial.println(rtsp.getPort());
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_PHONE_LED, OUTPUT);
    pinMode(PIN_PERSON_LED, OUTPUT);
    digitalWrite(PIN_PHONE_LED, LOW);
    digitalWrite(PIN_PERSON_LED, LOW);

    // 開機連不上也照樣啟動 RTSP 與 OSD，之後由 loop() 持續重連，連上後畫面就能看
    connectWiFi();

    config.setBitrate(1 * 1024 * 1024);
    Camera.configVideoChannel(CHANNEL, config);
    Camera.configVideoChannel(CHANNELNN, configNN);
    Camera.videoInit();

    rtsp.configVideo(config);
    rtsp.begin();

    ObjDet.configVideo(configNN);
    ObjDet.configThreshold(MODEL_MIN_CONFIDENCE, MODEL_NMS);
    ObjDet.setResultCallback(ODPostProcess);
    ObjDet.modelSelect(OBJECT_DETECTION, DEFAULT_YOLOV4TINY, NA_MODEL, NA_MODEL);
    ObjDet.begin();

    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }
    Camera.channelBegin(CHANNEL);

    videoStreamerNN.registerInput(Camera.getStream(CHANNELNN));
    videoStreamerNN.setStackSize();
    videoStreamerNN.setTaskPriority();
    videoStreamerNN.registerOutput(ObjDet);
    if (videoStreamerNN.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }
    Camera.channelBegin(CHANNELNN);

    OSD.configVideo(CHANNEL, config);
    OSD.begin();
    printRtspUrl();

    Serial.println("==== Focus Guard week-1 check ====");
    Serial.println("LED_G(D24)=person, LED_B(D23)=phone (score >= 50)");
    Serial.println("states: NO_TARGET / PERSON / UNSURE(30-49) / PHONE");
}

void loop()
{
    // 辨識與 LED 都在 ODPostProcess 中處理；這裡只負責 Wi-Fi 斷線重連，讓 RTSP 畫面能恢復
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck >= WIFI_CHECK_MS) {
        lastWiFiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wi-Fi not connected, retrying...");
            if (beginWiFi() == WL_CONNECTED) {
                printRtspUrl();
            }
        }
    }
    delay(100);
}

FrameState classify(int personScore, int phoneScore)
{
    if (phoneScore >= CONFIRM_SCORE) {
        return PHONE;
    }
    if (phoneScore >= UNSURE_SCORE) {
        return UNSURE;
    }
    if (personScore >= CONFIRM_SCORE) {
        return PERSON;
    }
    return NO_TARGET;
}

void ODPostProcess(std::vector<ObjectDetectionResult> results)
{
    const uint16_t im_h = config.height();
    const uint16_t im_w = config.width();
    const int count = ObjDet.getResultCount();
    int personScore = 0;
    int phoneScore = 0;

    OSD.createBitmap(CHANNEL);

    for (int i = 0; i < count; i++) {
        ObjectDetectionResult item = results[i];
        const int type = item.type();
        if (type != CLASS_PERSON && type != CLASS_PHONE) {
            continue;
        }

        const int score = item.score();
        if (type == CLASS_PERSON && score > personScore) {
            personScore = score;
        }
        if (type == CLASS_PHONE && score > phoneScore) {
            phoneScore = score;
        }

        const int xmin = (int)(item.xMin() * im_w);
        const int xmax = (int)(item.xMax() * im_w);
        const int ymin = (int)(item.yMin() * im_h);
        const int ymax = (int)(item.yMax() * im_h);
        const uint32_t color = (type == CLASS_PHONE) ? OSD_COLOR_RED : OSD_COLOR_GREEN;
        OSD.drawRect(CHANNEL, xmin, ymin, xmax, ymax, 3, color);
        char label[24];
        snprintf(label, sizeof(label), "%s %d", itemList[type].objectName, score);
        OSD.drawText(CHANNEL, xmin, ymin - OSD.getTextHeight(CHANNEL), label, OSD_COLOR_CYAN);
    }

    const FrameState state = classify(personScore, phoneScore);
    digitalWrite(PIN_PHONE_LED, state == PHONE ? HIGH : LOW);
    digitalWrite(PIN_PERSON_LED, personScore >= CONFIRM_SCORE ? HIGH : LOW);

    char status[48];
    snprintf(status, sizeof(status), "STATE %s  P%d  PH%d", OSD_STATE_NAMES[state], personScore, phoneScore);
    OSD.drawText(CHANNEL, 20, 20, status, OSD_COLOR_YELLOW);
    OSD.update(CHANNEL);

    frameCount++;
    const unsigned long now = millis();
    if (LOG_EVERY_FRAME) {
        printf("F,%lu,%d,%d,%s\r\n", now, personScore, phoneScore, STATE_NAMES[state]);
    }
    if (state != lastState) {
        printf("[%lu ms] %s -> %s (person=%d phone=%d)\r\n", now, STATE_NAMES[lastState], STATE_NAMES[state], personScore, phoneScore);
        lastState = state;
    }
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
        printf("[%lu ms] state=%s person=%d phone=%d frames/s=%lu\r\n", now, STATE_NAMES[state], personScore, phoneScore, frameCount);
        frameCount = 0;
        lastHeartbeat = now;
    }
}
