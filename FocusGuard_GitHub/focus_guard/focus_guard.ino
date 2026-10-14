/*
 * 讀書專注小幫手 — 第二週原型（AMB82-MINI）
 *
 * 以第一週的 focus_check 為基礎（改自 Realtek 範例 NeuralNetwork/ObjectDetectionCallback），
 * 第二週新增：
 *   1. 狀態機：手機連續出現 3 秒才判定「分心」，手機消失超過 2 秒才結束，同一次不重複計數；
 *      人消失超過 3 秒判定「離座」並暫停計時；累計讀書時間、分心時間與分心次數
 *   2. 板上網頁：瀏覽器開 http://<板子IP>/ 可看即時狀態、統計與分心紀錄，並可按「歸零」
 *
 * LED：LED_G（綠）= 讀書中，LED_B（藍）= 分心中。序列埠保留第一週的逐幀 F,... 紀錄，
 * 另外輸出 EVENT,... 事件紀錄。Wi-Fi 帳密放在 secrets.h（不上傳）。
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

constexpr float MODEL_MIN_CONFIDENCE = 0.30f;
constexpr float MODEL_NMS = 0.30f;
constexpr int CONFIRM_SCORE = 50;    // ≥ 50 才算確定
constexpr int UNSURE_SCORE = 30;     // 30～49 視為不確定，不會觸發也不會延長分心

// 狀態機參數
constexpr unsigned long PHONE_CONFIRM_MS = 3000;    // 手機連續出現多久才算分心
constexpr unsigned long PHONE_GRACE_MS = 2000;      // 手機消失多久才算結束（短暫遮擋不算）
constexpr unsigned long AWAY_MS = 3000;             // 人消失多久才算離座
constexpr unsigned long MAX_FRAME_GAP_MS = 1000;    // 累計時間時，單一幀最多算 1 秒

constexpr uint8_t PIN_DISTRACT_LED = LED_B;
constexpr uint8_t PIN_STUDY_LED = LED_G;
constexpr unsigned long HEARTBEAT_MS = 1000;
constexpr unsigned long WIFI_CHECK_MS = 5000;
constexpr bool LOG_EVERY_FRAME = true;    // F,時間,person,phone,幀狀態（給測試腳本統計）

// 單一幀的辨識結果（第一週的四種狀態）
enum FrameState { NO_TARGET, PERSON, UNSURE, PHONE };
const char* const FRAME_NAMES[] = {"NO_TARGET", "PERSON", "UNSURE", "PHONE"};

// 第二週的使用者狀態
enum UserState { AWAY, STUDYING, PHONE_PENDING, DISTRACTED };
const char* const USER_NAMES[] = {"AWAY", "STUDYING", "PHONE_PENDING", "DISTRACTED"};
const char* const OSD_USER_NAMES[] = {"AWAY", "STUDYING", "PHONE ?", "DISTRACTED"};

struct DistractEvent {
    unsigned long startMs;
    unsigned long durationMs;
};
constexpr int MAX_EVENTS = 20;

VideoSetting config(VIDEO_HD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNObjectDetection ObjDet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);
WiFiServer webServer(80);

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

// ---- 狀態機資料（只在辨識回呼中修改）----
UserState userState = AWAY;
unsigned long lastFrameMs = 0;
unsigned long personLastSeenMs = 0;
unsigned long phoneFirstSeenMs = 0;    // 0 = 目前沒有候選的手機
unsigned long phoneLastSeenMs = 0;
unsigned long distractStartMs = 0;
unsigned long studyMs = 0;
unsigned long distractMs = 0;    // 已結束的分心累計；進行中的那一次另外加
int distractCount = 0;
DistractEvent events[MAX_EVENTS];
int eventCount = 0;    // 累計事件數，最近 MAX_EVENTS 筆保留在 events（環狀）
int lastPersonScore = 0;
int lastPhoneScore = 0;
volatile bool resetRequested = false;

// 網頁用的狀態 JSON：回呼寫入非使用中的那一份再切換，loop() 讀取使用中的那一份
char statusJson[2][2048];
volatile int statusIndex = 0;

unsigned long lastHeartbeat = 0;
unsigned long frameCount = 0;

const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>讀書專注小幫手</title>
<style>
body{margin:0;font-family:"Microsoft JhengHei",sans-serif;background:#f4f6fa;color:#1d2433}
main{max-width:560px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 12px}
#state{border-radius:12px;padding:18px;text-align:center;color:#fff;font-size:26px;font-weight:bold}
#sub{text-align:center;color:#5b6475;margin:6px 0 14px;font-size:14px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.card{background:#fff;border-radius:10px;padding:10px;text-align:center}
.card b{display:block;font-size:22px;margin-top:4px}
table{width:100%;border-collapse:collapse;background:#fff;border-radius:10px;margin-top:14px;font-size:14px}
th,td{padding:7px 10px;border-bottom:1px solid #e3e7ee;text-align:left}
button{margin-top:14px;padding:8px 16px;border:0;border-radius:8px;background:#2a5db0;color:#fff;font-size:15px}
#err{color:#b3261e;text-align:center;font-size:14px;min-height:18px}
</style></head><body><main>
<h1>讀書專注小幫手</h1>
<div id="state">連線中…</div>
<div id="sub"></div>
<div class="grid">
<div class="card">讀書時間<b id="study">-</b></div>
<div class="card">分心次數<b id="count">-</b></div>
<div class="card">分心時間<b id="distract">-</b></div>
</div>
<table><thead><tr><th>#</th><th>開始（開機後）</th><th>持續</th></tr></thead><tbody id="events"></tbody></table>
<button onclick="resetAll()">歸零</button>
<div id="err"></div>
</main><script>
const LABEL={AWAY:["離座","#7b8494"],STUDYING:["讀書中","#2e8b57"],PHONE_PENDING:["偵測到手機…","#c98a00"],DISTRACTED:["分心中","#c0392b"]};
function t(s){s=Math.floor(s);const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return(h?h+":":"")+String(m).padStart(2,"0")+":"+String(x).padStart(2,"0")}
async function poll(){
 try{
  const r=await fetch("/status",{cache:"no-store"});const d=await r.json();
  const [name,color]=LABEL[d.state]||[d.state,"#333"];
  const st=document.getElementById("state");st.textContent=name;st.style.background=color;
  let sub="人 "+d.person+" 分　手機 "+d.phone+" 分";
  if(d.state==="PHONE_PENDING")sub+="　再 "+(d.pending_left_ms/1000).toFixed(1)+" 秒判定分心";
  document.getElementById("sub").textContent=sub;
  document.getElementById("study").textContent=t(d.study_s);
  document.getElementById("count").textContent=d.count;
  document.getElementById("distract").textContent=t(d.distract_s);
  document.getElementById("events").innerHTML=d.events.map(e=>"<tr><td>"+e.n+"</td><td>"+t(e.start_s)+"</td><td>"+e.dur_s.toFixed(1)+" 秒</td></tr>").join("")||"<tr><td colspan=3>尚無分心紀錄</td></tr>";
  document.getElementById("err").textContent="";
 }catch(e){document.getElementById("err").textContent="與開發板連線中斷，重試中…"}
}
async function resetAll(){if(confirm("確定要把統計歸零？")){await fetch("/reset",{method:"POST"});poll()}}
poll();setInterval(poll,1000);
</script></body></html>)HTML";

int beginWiFi()
{
    return (pass[0] == '\0') ? WiFi.begin(ssid) : WiFi.begin(ssid, pass);
}

bool connectWiFi()
{
#ifdef WIFI_STATIC_IP
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

void printUrls()
{
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("RTSP: rtsp://");
        Serial.print(WiFi.localIP());
        Serial.print(":");
        Serial.println(rtsp.getPort());
        Serial.print("WEB: http://");
        Serial.print(WiFi.localIP());
        Serial.println("/");
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_DISTRACT_LED, OUTPUT);
    pinMode(PIN_STUDY_LED, OUTPUT);
    digitalWrite(PIN_DISTRACT_LED, LOW);
    digitalWrite(PIN_STUDY_LED, LOW);
    snprintf(statusJson[0], sizeof(statusJson[0]), "{\"state\":\"AWAY\",\"person\":0,\"phone\":0,\"study_s\":0,\"distract_s\":0,\"count\":0,\"pending_left_ms\":0,\"events\":[]}");

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

    webServer.begin();
    printUrls();

    Serial.println("==== Focus Guard week-2 prototype ====");
    Serial.println("LED_G(D24)=studying, LED_B(D23)=distracted (phone >= 50 for 3 s)");
}

// ---------------- 網頁 ----------------

void sendResponse(WiFiClient& client, const char* contentType, const char* body)
{
    client.print("HTTP/1.1 200 OK\r\nContent-Type: ");
    client.print(contentType);
    client.print("\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
    client.print(strlen(body));
    client.print("\r\n\r\n");
    client.print(body);
}

void handleWebClient(WiFiClient& client)
{
    String requestLine = "";
    String line = "";
    const unsigned long start = millis();
    bool headersDone = false;
    while (client.connected() && millis() - start < 1500 && !headersDone) {
        if (!client.available()) {
            delay(1);
            continue;
        }
        const char c = client.read();
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            line += c;
            continue;
        }
        if (requestLine.length() == 0) {
            requestLine = line;
        }
        headersDone = (line.length() == 0);
        line = "";
    }

    if (requestLine.startsWith("GET /status")) {
        char body[sizeof(statusJson[0])];
        strncpy(body, statusJson[statusIndex], sizeof(body));
        body[sizeof(body) - 1] = '\0';
        sendResponse(client, "application/json; charset=utf-8", body);
    } else if (requestLine.startsWith("POST /reset")) {
        resetRequested = true;    // 交給辨識回呼處理，避免兩邊同時改統計資料
        sendResponse(client, "application/json; charset=utf-8", "{\"ok\":true}");
        Serial.println("EVENT,RESET_REQUESTED");
    } else if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /index")) {
        sendResponse(client, "text/html; charset=utf-8", INDEX_HTML);
    } else {
        client.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    }
    delay(1);
    client.stop();
}

void loop()
{
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck >= WIFI_CHECK_MS) {
        lastWiFiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wi-Fi not connected, retrying...");
            if (beginWiFi() == WL_CONNECTED) {
                printUrls();
            }
        }
    }

    WiFiClient client = webServer.available();
    if (client) {
        handleWebClient(client);
    } else {
        delay(20);
    }
}

// ---------------- 辨識與狀態機 ----------------

FrameState classifyFrame(int personScore, int phoneScore)
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

void resetStats()
{
    studyMs = 0;
    distractMs = 0;
    distractCount = 0;
    eventCount = 0;
    phoneFirstSeenMs = 0;
    if (userState == DISTRACTED || userState == PHONE_PENDING) {
        userState = STUDYING;
    }
    Serial.println("EVENT,RESET");
}

// 一次分心的長度 = 手機第一次出現到最後一次出現（含 3 秒確認期，不含結束時的寬限期）
unsigned long currentDistractMs()
{
    return userState == DISTRACTED ? phoneLastSeenMs - distractStartMs : 0;
}

void endDistraction(unsigned long now)
{
    const unsigned long duration = phoneLastSeenMs - distractStartMs;
    distractMs += duration;
    events[eventCount % MAX_EVENTS] = {distractStartMs, duration};
    eventCount++;
    printf("EVENT,DISTRACT_END,%lu,%d,%lu\r\n", now, distractCount, duration);
}

void updateUserState(unsigned long now, int personScore, int phoneScore)
{
    const unsigned long dt = (lastFrameMs == 0) ? 0 : std::min(now - lastFrameMs, MAX_FRAME_GAP_MS);
    lastFrameMs = now;

    // 讀書時間以「上一幀的狀態」累計這段 dt；分心時間在事件結束時計算
    if (userState == STUDYING || userState == PHONE_PENDING) {
        studyMs += dt;
    }

    const bool personSeen = personScore >= CONFIRM_SCORE;
    const bool phoneSeen = phoneScore >= CONFIRM_SCORE;
    if (personSeen) {
        personLastSeenMs = now;
    }
    if (phoneSeen) {
        if (phoneFirstSeenMs == 0 || now - phoneLastSeenMs > PHONE_GRACE_MS) {
            phoneFirstSeenMs = now;    // 新的一段手機出現（中間斷超過寬限期就重新起算）
        }
        phoneLastSeenMs = now;
    }
    const bool phoneActive = phoneFirstSeenMs != 0 && now - phoneLastSeenMs <= PHONE_GRACE_MS;
    if (!phoneActive) {
        phoneFirstSeenMs = 0;
    }

    UserState next;
    if (userState == DISTRACTED) {
        next = phoneActive ? DISTRACTED : STUDYING;
    } else if (phoneActive && now - phoneFirstSeenMs >= PHONE_CONFIRM_MS) {
        next = DISTRACTED;
    } else if (phoneActive) {
        next = PHONE_PENDING;
    } else {
        next = STUDYING;
    }
    // 沒在分心、也沒看到手機時，人消失超過 AWAY_MS 就算離座
    if (next == STUDYING && now - personLastSeenMs > AWAY_MS) {
        next = AWAY;
    }

    if (next != userState) {
        if (next == DISTRACTED) {
            distractCount++;
            distractStartMs = phoneFirstSeenMs;
            // 確認期那 3 秒原本算成讀書時間，改算到這次分心
            studyMs -= std::min(now - phoneFirstSeenMs, studyMs);
            printf("EVENT,DISTRACT_START,%lu,%d\r\n", now, distractCount);
        }
        if (userState == DISTRACTED) {
            endDistraction(now);
        }
        printf("EVENT,STATE,%lu,%s,%s\r\n", now, USER_NAMES[userState], USER_NAMES[next]);
        userState = next;
    }
}

void publishStatus(unsigned long now)
{
    const int target = 1 - statusIndex;
    char* out = statusJson[target];
    const size_t size = sizeof(statusJson[target]);
    const unsigned long pendingLeft = (userState == PHONE_PENDING) ? PHONE_CONFIRM_MS - std::min(now - phoneFirstSeenMs, PHONE_CONFIRM_MS) : 0;
    int len = snprintf(out, size,
                       "{\"state\":\"%s\",\"person\":%d,\"phone\":%d,\"study_s\":%lu,\"distract_s\":%lu,\"count\":%d,\"pending_left_ms\":%lu,\"uptime_s\":%lu,\"events\":[",
                       USER_NAMES[userState], lastPersonScore, lastPhoneScore, studyMs / 1000, (distractMs + currentDistractMs()) / 1000, distractCount, pendingLeft, now / 1000);
    // 最新的在最上面
    const int shown = std::min(eventCount, MAX_EVENTS);
    for (int i = 0; i < shown && len < (int)size - 64; i++) {
        const int n = eventCount - i;    // 第幾次分心
        const DistractEvent& e = events[(n - 1) % MAX_EVENTS];
        len += snprintf(out + len, size - len, "%s{\"n\":%d,\"start_s\":%lu,\"dur_s\":%lu.%lu}", i ? "," : "", n, e.startMs / 1000, e.durationMs / 1000, (e.durationMs % 1000) / 100);
    }
    snprintf(out + len, size - len, "]}");
    statusIndex = target;
}

void drawOsd(std::vector<ObjectDetectionResult>& results, int count)
{
    const uint16_t im_h = config.height();
    const uint16_t im_w = config.width();
    OSD.createBitmap(CHANNEL);
    for (int i = 0; i < count; i++) {
        ObjectDetectionResult item = results[i];
        const int type = item.type();
        if (type != CLASS_PERSON && type != CLASS_PHONE) {
            continue;
        }
        const int xmin = (int)(item.xMin() * im_w);
        const int xmax = (int)(item.xMax() * im_w);
        const int ymin = (int)(item.yMin() * im_h);
        const int ymax = (int)(item.yMax() * im_h);
        const uint32_t color = (type == CLASS_PHONE) ? OSD_COLOR_RED : OSD_COLOR_GREEN;
        OSD.drawRect(CHANNEL, xmin, ymin, xmax, ymax, 3, color);
        char label[24];
        snprintf(label, sizeof(label), "%s %d", itemList[type].objectName, item.score());
        OSD.drawText(CHANNEL, xmin, ymin - OSD.getTextHeight(CHANNEL), label, OSD_COLOR_CYAN);
    }
    char status[64];
    snprintf(status, sizeof(status), "%s  COUNT %d", OSD_USER_NAMES[userState], distractCount);
    OSD.drawText(CHANNEL, 20, 20, status, OSD_COLOR_YELLOW);
    OSD.update(CHANNEL);
}

void ODPostProcess(std::vector<ObjectDetectionResult> results)
{
    const int count = ObjDet.getResultCount();
    int personScore = 0;
    int phoneScore = 0;
    for (int i = 0; i < count; i++) {
        const int type = results[i].type();
        const int score = results[i].score();
        if (type == CLASS_PERSON && score > personScore) {
            personScore = score;
        }
        if (type == CLASS_PHONE && score > phoneScore) {
            phoneScore = score;
        }
    }
    lastPersonScore = personScore;
    lastPhoneScore = phoneScore;

    const unsigned long now = millis();
    if (resetRequested) {
        resetRequested = false;
        resetStats();
    }
    updateUserState(now, personScore, phoneScore);

    digitalWrite(PIN_DISTRACT_LED, userState == DISTRACTED ? HIGH : LOW);
    digitalWrite(PIN_STUDY_LED, (userState == STUDYING || userState == PHONE_PENDING) ? HIGH : LOW);

    publishStatus(now);
    drawOsd(results, count);

    const FrameState frame = classifyFrame(personScore, phoneScore);
    frameCount++;
    if (LOG_EVERY_FRAME) {
        printf("F,%lu,%d,%d,%s\r\n", now, personScore, phoneScore, FRAME_NAMES[frame]);
    }
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
        printf("[%lu ms] state=%s person=%d phone=%d frames/s=%lu count=%d study=%lus distract=%lus\r\n", now, USER_NAMES[userState], personScore, phoneScore, frameCount,
               distractCount, studyMs / 1000, (distractMs + currentDistractMs()) / 1000);
        frameCount = 0;
        lastHeartbeat = now;
    }
}
