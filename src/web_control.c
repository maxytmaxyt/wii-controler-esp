#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "wiimote_bt.h"

#define WEB_AP_SSID "WiiRemote-ESP32"
#define WEB_AP_PASSWORD "wiiremote"
#define WEB_BUTTON_MASK 0x9f1fU

static const char *TAG = "web_control";
static httpd_handle_t s_http_server;

static const char s_index_html[] =
    "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,"
    "maximum-scale=1,user-scalable=no\"><meta name=\"theme-color\" "
    "content=\"#10131c\"><title>Wii Remote</title><style>"
    "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:"
    "radial-gradient(ellipse at 50% 0,#26344b,#10131c 65%);color:#f4f6fb;"
    "font:16px system-ui,sans-serif;display:flex;justify-content:center}"
    "main{width:min(100%,560px);padding:28px 20px 36px}h1{font-size:28px;"
    "margin:0 0 6px}.sub{color:#aab4c5;margin:0 0 20px}.status{display:flex;"
    "align-items:center;gap:10px;padding:13px 15px;border:1px solid #30394a;"
    "border-radius:14px;background:#1a202c;margin-bottom:26px}.dot{width:11px;"
    "height:11px;border-radius:50%;background:#e5a84b}.dot.on{background:"
    "#50d890;box-shadow:0 0 12px #50d890}.layout{display:grid;grid-template-"
    "columns:1fr 1fr;gap:26px;align-items:center}.pad{display:grid;grid-"
    "template-columns:repeat(3,68px);grid-template-rows:repeat(3,68px);"
    "gap:7px;justify-content:center}.actions{display:grid;grid-template-"
    "columns:repeat(2,76px);gap:14px;justify-content:center}.key{border:1px "
    "solid #3a4659;background:linear-gradient(145deg,#2c3545,#1e2532);"
    "color:#f4f6fb;border-radius:18px;font-size:19px;font-weight:700;"
    "box-shadow:0 5px 0 #111722;touch-action:none;user-select:none;"
    "-webkit-user-select:none;transition:transform .06s,background .06s}"
    ".key:active,.key.down{transform:translateY(4px);box-shadow:0 1px 0 "
    "#111722;background:#42618a}.empty{visibility:hidden}.round{width:76px;"
    "height:76px;border-radius:50%;font-size:22px}.wide{grid-column:span 2;"
    "width:100%;height:52px;border-radius:16px;font-size:16px}.section{"
    "color:#aab4c5;font-size:13px;text-align:center;margin:0 0 13px}"
    ".extras{display:flex;gap:10px;justify-content:center;margin-top:30px}"
    ".extras .key{min-width:68px;height:48px;font-size:15px}.hint{color:"
    "#8390a4;font-size:13px;text-align:center;line-height:1.55;margin-top:24px}"
    ".connect{width:100%;height:50px;border-radius:14px;font-size:15px;"
    "margin:-12px 0 24px}.connect:disabled{opacity:.65}"
    "@media(max-width:390px){.pad{grid-template-columns:repeat(3,58px);"
    "grid-template-rows:repeat(3,58px)}.layout{gap:10px}.actions{gap:10px}"
    ".round{width:68px;height:68px}}"
    "</style></head><body><main><h1>Wii Remote</h1><p class=\"sub\">"
    "Steuerung direkt im Browser</p><div class=\"status\"><span id=\"dot\" "
    "class=\"dot\"></span><span id=\"state\">Bluetooth wird geprueft ...</span>"
    "</div><button id=\"connect\" class=\"key connect\">Verbinden / Pairing "
    "starten</button><div class=\"layout\"><section><p class=\"section\">"
    "Steuerkreuz</p>"
    "<div class=\"pad\"><span class=\"empty\"></span><button class=\"key\" "
    "data-bit=\"8\">▲</button><span class=\"empty\"></span><button class=\"key\" "
    "data-bit=\"1\">◀</button><span class=\"empty\"></span><button class=\"key\" "
    "data-bit=\"2\">▶</button><span class=\"empty\"></span><button class=\"key\" "
    "data-bit=\"4\">▼</button><span class=\"empty\"></span></div></section>"
    "<section><p class=\"section\">Aktion</p><div class=\"actions\">"
    "<button class=\"key round\" data-bit=\"512\">1</button>"
    "<button class=\"key round\" data-bit=\"2048\">A</button>"
    "<button class=\"key round\" data-bit=\"256\">2</button>"
    "<button class=\"key round\" data-bit=\"1024\">B</button></div></section>"
    "</div><div class=\"extras\"><button class=\"key\" data-bit=\"16\">＋</button>"
    "<button class=\"key\" data-bit=\"4096\">−</button>"
    "<button class=\"key\" data-bit=\"32768\">Home</button></div>"
    "<p class=\"hint\">Tasten gedrueckt halten. Tastatur: Pfeile, A, B, 1, 2, "
    "Plus, Minus und H fuer Home.<br>Mit der Wii koppeln, bevor du steuerst.</p>"
    "</main><script>"
    "const held=new Set(),buttons=[...document.querySelectorAll('[data-bit]')];"
    "let chain=Promise.resolve();"
    "function mask(){let n=0;for(const b of held)n|=b;return n}"
    "function update(){for(const b of buttons)b.classList.toggle('down',"
    "held.has(Number(b.dataset.bit)));const n=mask();chain=chain.then(()=>"
    "fetch('/api/buttons',{method:'POST',headers:{'Content-Type':'text/plain'},"
    "body:String(n)})).then(async r=>{if(!r.ok){const t=await r.text();"
    "throw Error(t||r.statusText)}}).catch(e=>{if(e.message.includes('Wii not connected'))"
    "showState('Wii nicht verbunden',false)})}"
    "buttons.forEach(b=>{const bit=Number(b.dataset.bit);"
    "b.addEventListener('pointerdown',e=>{e.preventDefault();b.setPointerCapture"
    "(e.pointerId);held.add(bit);update()});"
    "for(const ev of ['pointerup','pointercancel','lostpointercapture'])"
    "b.addEventListener(ev,()=>{if(held.delete(bit))update()})});"
    "const keys={ArrowUp:8,ArrowDown:4,ArrowLeft:1,ArrowRight:2,"
    "a:2048,b:1024,'1':512,'2':256,'+':16,'-':4096,h:32768};"
    "window.addEventListener('keydown',e=>{const bit=keys[e.key];if(bit){"
    "e.preventDefault();if(!held.has(bit)){held.add(bit);update()}}});"
    "window.addEventListener('keyup',e=>{const bit=keys[e.key];"
    "if(bit&&held.delete(bit)){e.preventDefault();update()}});"
    "window.addEventListener('blur',()=>{if(held.size){held.clear();update()}});"
    "function showState(s,on){document.getElementById('state').textContent=s;"
    "document.getElementById('dot').classList.toggle('on',on)}"
    "async function poll(){try{const r=await fetch('/api/status',{cache:'no-store'});"
    "if(!r.ok)throw Error();const s=await r.json();showState("
    "s.connected?'Wii verbunden':'Wii nicht verbunden ('+s.state+')',s.connected)}"
    "catch(e){showState('Verbindung zum ESP32 unterbrochen',false)}}"
    "const connectButton=document.getElementById('connect');"
    "connectButton.addEventListener('click',async()=>{connectButton.disabled=true;"
    "connectButton.textContent='Verbindungsversuch laeuft ...';try{const r=await "
    "fetch('/api/connect',{method:'POST'});if(!r.ok){const t=await r.text();"
    "throw Error(t||r.statusText)}await poll()}catch(e){showState("
    "'Verbindung konnte nicht gestartet werden: '+e.message,false)}finally{"
    "setTimeout(()=>{connectButton.disabled=false;connectButton.textContent="
    "'Verbinden / Pairing starten'},1500)}});"
    "poll();setInterval(poll,1500);</script></body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static const char *const states[] = {
        "IDLE", "DISCOVERABLE", "CONNECTING", "AUTHENTICATING",
        "CONNECTED", "RECONNECTING",
    };
    wiimote_state_t state = wiimote_bt_get_state();
    const char *name = (unsigned)state < sizeof(states) / sizeof(states[0])
                           ? states[state]
                           : "UNKNOWN";
    char response[80];
    int length = snprintf(response, sizeof(response),
                          "{\"connected\":%s,\"state\":\"%s\"}",
                          state == WIIMOTE_STATE_CONNECTED ? "true" : "false",
                          name);
    if (length < 0 || (size_t)length >= sizeof(response)) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, length);
}

static esp_err_t connect_handler(httpd_req_t *req)
{
    esp_err_t ret = wiimote_bt_connect_or_pair();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wii connection/pairing: %s",
                 esp_err_to_name(ret));
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Bluetooth pairing could not be started",
                               HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "Connection attempt started",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t buttons_handler(httpd_req_t *req)
{
    char body[6];
    if (req->content_len < 1 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Expected button mask from 0 to 65535");
        return ESP_OK;
    }

    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int result = httpd_req_recv(req, body + received,
                                    req->content_len - received);
        if (result <= 0) {
            if (result == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += result;
    }
    body[received] = '\0';

    unsigned long mask = 0;
    for (size_t i = 0; i < received; i++) {
        if (body[i] < '0' || body[i] > '9') {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Button mask must be a decimal number");
            return ESP_OK;
        }
        mask = mask * 10 + (unsigned long)(body[i] - '0');
        if (mask > UINT16_MAX) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Button mask is out of range");
            return ESP_OK;
        }
    }
    if ((mask & ~WEB_BUTTON_MASK) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Button mask contains unsupported buttons");
        return ESP_OK;
    }

    esp_err_t ret = wiimote_bt_send_buttons((uint8_t)mask,
                                             (uint8_t)(mask >> 8));
    if (ret == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "Wii not connected", HTTPD_RESP_USE_STRLEN);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send button report: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Button report failed", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t web_control_start(void)
{
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Network interface init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Default event loop init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (esp_netif_create_default_wifi_ap() == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi access point interface");
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_init);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WEB_AP_SSID,
            .ssid_len = sizeof(WEB_AP_SSID) - 1,
            .channel = 1,
            .password = WEB_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_start();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi access point start failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 4;
    ret = httpd_start(&s_http_server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const httpd_uri_t routes[] = {
        {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
        },
        {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler,
        },
        {
            .uri = "/api/buttons",
            .method = HTTP_POST,
            .handler = buttons_handler,
        },
        {
            .uri = "/api/connect",
            .method = HTTP_POST,
            .handler = connect_handler,
        },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ret = httpd_register_uri_handler(s_http_server, &routes[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HTTP route registration failed: %s",
                     esp_err_to_name(ret));
            httpd_stop(s_http_server);
            s_http_server = NULL;
            return ret;
        }
    }

    ESP_LOGI(TAG, "Wi-Fi access point started: %s", WEB_AP_SSID);
    ESP_LOGI(TAG, "Web controller: http://192.168.4.1/");
    return ESP_OK;
}
