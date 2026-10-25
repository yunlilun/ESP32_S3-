#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

//摄像头引脚定义
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

// 全局对象与变量
WebServer server(80);
Preferences prefs;
camera_config_t config;
sensor_t * s;

String apSSID = "ESP32-Cam";
String apPass = "12345678";
String staSSID = "";
String staPass = "";
int currentRes = 9;
int currentQual = 10;
int currentBri = 0;
int currentCon = 0;

// 网页代码
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>ESP32终端监视设备</title>
<style>
    body { background-color: #121212; color: #ffffff; font-family: Arial, sans-serif; margin: 0; padding: 20px; }
    .container { max-width: 1000px; margin: 0 auto; }
    .card { background-color: #1e1e1e; border: 1px solid #333; border-radius: 8px; padding: 15px; margin-bottom: 15px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    h3 { margin-top: 0; color: #00e5ff; }
    h4 { margin-top: 0; border-bottom: 1px solid #333; padding-bottom: 5px; }
    .row { display: flex; gap: 15px; flex-wrap: wrap; }
    .col { flex: 1; min-width: 300px; }
    button { padding: 8px 12px; background-color: #00e5ff; color: #000; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; margin-right: 5px; }
    button:hover { background-color: #00b3cc; }
    .btn-rec { background-color: #ff4444; color: #fff; }
    .btn-rec:hover { background-color: #cc0000; }
    input[type="text"], input[type="password"], select { padding: 6px; margin: 5px 0; background-color: #000; border: 1px solid #555; color: #fff; border-radius: 4px; width: 100%; box-sizing: border-box; }
    input[type="range"] { width: 100%; }
    img { width: 100%; border-radius: 4px; border: 1px solid #555; }
    #aiStat { color: #ffaa00; font-weight: bold; }
    #recStat { color: #ff4444; font-weight: bold; margin-left: 10px; }
</style>
</head>
<body>
<div class="container">
    <div class="card">
        <h3>ESP32 终端监视设备</h3>
        <img id="v" src="/stream" crossorigin="anonymous">
    </div>

    <div class="row">
        <div class="col">
            <div class="card">
                <h4>快捷操作</h4>
                <button onclick="snap()">截图下载</button>
                <button id="recBtn" class="btn-rec" onclick="toggleRec()">开始录像</button>
                <span id="recStat"></span>
            </div>
            <div class="card">
                <h4>硬件参数调整</h4>
                分辨率: <select id="res"><option value="8">VGA</option><option value="9">SVGA</option><option value="10">XGA</option></select>
                画质: <input type="range" id="qual" min="10" max="63" value="10">
                亮度: <input type="range" id="bri" min="-2" max="2" value="0">
                对比度: <input type="range" id="con" min="-2" max="2" value="0">
                <button onclick="saveCam()">应用参数</button>
            </div>
        </div>
        <div class="col">
            <div class="card">
                <h4>前端特效与 AI</h4>
                <select id="filter" onchange="applyFilter()">
                    <option value="none">无特效</option>
                    <option value="invert(1)">反色 (X光)</option>
                    <option value="grayscale(1)">黑白</option>
                    <option value="contrast(2) saturate(1.5)">高对比度</option>
                </select>
                <button onclick="startAI()">启动 AI 识别</button> <span id="aiStat">未启动</span>
            </div>
            <div class="card">
                <h4>网络配置 (AP/STA)</h4>
                热点名称: <input type="text" id="ap_ssid" placeholder="不填则保持原样">
                热点密码: <input type="password" id="ap_pass" placeholder="至少8位">
                路由名称: <input type="text" id="sta_ssid">
                路由密码: <input type="password" id="sta_pass">
                <button onclick="saveNet()">保存并重启</button>
            </div>
        </div>
    </div>
</div>

<script>
// 获取DOM
var v = document.getElementById('v');
var c = document.createElement('canvas');
var ctx = c.getContext('2d');
var model = null;
var aiOn = false;

// 录像相关变量
var rec = null;
var recChunks = [];
var isRecording = false;
var drawLoop = null;

// 滤镜应用
function applyFilter() {
    v.style.filter = document.getElementById('filter').value;
}

// 截图
function snap() {
    c.width = v.naturalWidth || 640;
    c.height = v.naturalHeight || 480;
    ctx.drawImage(v, 0, 0);
    var a = document.createElement('a');
    a.href = c.toDataURL('image/jpeg');
    a.download = 'snap_' + Date.now() + '.jpg';
    a.click();
}

// 录像开关
function toggleRec() {
    var btn = document.getElementById('recBtn');
    var stat = document.getElementById('recStat');
    
    if (!isRecording) {
        // 准备画布
        c.width = v.naturalWidth || 640;
        c.height = v.naturalHeight || 480;
        ctx.drawImage(v, 0, 0);
        
        // 录制画布的数据流
        var stream = c.captureStream(25); 
        rec = new MediaRecorder(stream, {mimeType: 'video/webm'});
        recChunks = [];
        
        rec.ondataavailable = function(e) { 
            if(e.data.size > 0) recChunks.push(e.data); 
        };
        
        rec.onstop = function() {
            if(recChunks.length === 0) {
                alert("录制失败，未获取到画面数据！");
                return;
            }
            var blob = new Blob(recChunks, {type: 'video/webm'});
            var a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            a.download = 'video_' + Date.now() + '.webm';
            a.click();
            stat.innerText = "";
        };
        
        // 强制不断向画布画图
        drawLoop = setInterval(function() {
            if(v.readyState === v.HAVE_ENOUGH_DATA) {
                ctx.drawImage(v, 0, 0, c.width, c.height);
            }
        }, 40);
        
        rec.start();
        isRecording = true;
        btn.innerText = "停止录像";
        stat.innerText = "正在录制中...";
    } else {
        // 停止录制
        if (drawLoop) clearInterval(drawLoop);
        rec.stop();
        isRecording = false;
        btn.innerText = "开始录像";
        stat.innerText = "正在保存文件...";
    }
}

// 保存参数
function saveCam() {
    var r = document.getElementById('res').value;
    var q = document.getElementById('qual').value;
    var b = document.getElementById('bri').value;
    var o = document.getElementById('con').value;
    fetch('/setcam?res='+r+'&qual='+q+'&bri='+b+'&con='+o).then(function(){
        v.src = "/stream?t=" + Date.now();
        alert("参数已应用。");
    });
}

// 保存网络配置
function saveNet() {
    var as = document.getElementById('ap_ssid').value;
    var ap = document.getElementById('ap_pass').value;
    var ss = document.getElementById('sta_ssid').value;
    var sp = document.getElementById('sta_pass').value;
    
    if(ap.length > 0 && ap.length < 8) {
        alert("热点密码必须至少8位！");
        return;
    }
    
    document.getElementById('aiStat').innerText = "正在保存并重启...";
    fetch('/setnet?as='+as+'&ap='+ap+'&ss='+ss+'&sp='+sp).catch(function(){});
    
    setTimeout(function(){
        alert("设备已重启！请重新搜索连接热点。");
    }, 2000);
}

// 加载AI库
function startAI() {
    if(aiOn) return;
    var stat = document.getElementById('aiStat');
    stat.innerText = "加载模型中...";
    
    var s1 = document.createElement('script');
    s1.src = "https://cdn.jsdmirror.com/npm/@tensorflow/tfjs@3.11.0/dist/tf.min.js";
    s1.onload = function() {
        var s2 = document.createElement('script');
        s2.src = "https://cdn.jsdmirror.com/npm/@tensorflow-models/coco-ssd@2.2.2/dist/coco-ssd.min.js";
        s2.onload = async function() { 
            model = await cocoSsd.load(); 
            aiOn = true;
            stat.innerText = "识别中...";
            aiLoop();
        };
        s2.onerror = function() { stat.innerText = "模型下载失败"; };
        document.head.appendChild(s2);
    };
    s1.onerror = function() { stat.innerText = "AI框架下载失败"; };
    document.head.appendChild(s1);
}

// AI循环
function aiLoop() {
    if(!aiOn) return;
    c.width = v.naturalWidth || 640;
    c.height = v.naturalHeight || 480;
    ctx.drawImage(v, 0, 0);
    
    model.detect(c).then(function(preds) {
        var count = preds.filter(function(p) { return p.class === 'person'; }).length;
        document.getElementById('aiStat').innerText = "在线AI: 人数 " + count;
    });
    setTimeout(aiLoop, 200);
}
</script>
</body>
</html>
)rawliteral";

// 网页路由处理
void handleRoot() { 
    server.send_P(200, "text/html", index_html); 
}

// 视频流推流
void handleStream() {
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println();
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) break;
        client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.println();
        esp_camera_fb_return(fb);
        if (!client.connected()) break;
        yield(); 
    }
}

// 处理参数
void handleSetCam() {
    if (server.hasArg("res")) currentRes = server.arg("res").toInt();
    if (server.hasArg("qual")) currentQual = server.arg("qual").toInt();
    if (server.hasArg("bri")) currentBri = server.arg("bri").toInt();
    if (server.hasArg("con")) currentCon = server.arg("con").toInt();
    
    prefs.putInt("res", currentRes);
    prefs.putInt("qual", currentQual);
    prefs.putInt("bri", currentBri);
    prefs.putInt("con", currentCon);
    
    esp_camera_deinit();
    config.frame_size = (framesize_t)currentRes;
    config.jpeg_quality = currentQual;
    esp_camera_init(&config);
    s = esp_camera_sensor_get();
    s->set_brightness(s, currentBri);
    s->set_contrast(s, currentCon);
    
    server.send(200, "text/plain", "OK");
}

// 处理配网
void handleSetNet() {
    if (server.hasArg("as") && server.arg("as") != "") { apSSID = server.arg("as"); prefs.putString("ap_ssid", apSSID); }
    if (server.hasArg("ap") && server.arg("ap") != "") { apPass = server.arg("ap"); prefs.putString("ap_pass", apPass); }
    if (server.hasArg("ss")) { staSSID = server.arg("ss"); prefs.putString("sta_ssid", staSSID); }
    if (server.hasArg("sp")) { staPass = server.arg("sp"); prefs.putString("sta_pass", staPass); }
    
    prefs.end();
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
}

void setup() {
    Serial.begin(115200);
    
    prefs.begin("cam", false);
    currentRes = prefs.getInt("res", 9);
    currentQual = prefs.getInt("qual", 10);
    currentBri = prefs.getInt("bri", 0);
    currentCon = prefs.getInt("con", 0);
    apSSID = prefs.getString("ap_ssid", "ESP32-Cam");
    apPass = prefs.getString("ap_pass", "12345678");
    staSSID = prefs.getString("sta_ssid", "");
    staPass = prefs.getString("sp", "");
    prefs.end();

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = (framesize_t)currentRes;
    config.jpeg_quality = currentQual;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_camera_init(&config);
    s = esp_camera_sensor_get();
    s->set_brightness(s, currentBri);
    s->set_contrast(s, currentCon);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSSID.c_str(), apPass.c_str());
    if(staSSID != "") WiFi.begin(staSSID.c_str(), staPass.c_str());

    server.on("/", handleRoot);
    server.on("/stream", handleStream);
    server.on("/setcam", handleSetCam);
    server.on("/setnet", handleSetNet);
    server.begin();
}

void loop() {
    server.handleClient();
}