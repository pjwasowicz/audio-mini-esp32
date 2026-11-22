#include "Konfiguracja.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "Dziennik.h"
#include "Sprzet.h"
#include "Sterownik.h"
#include "Ustawienia.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace konfiguracja {

static const char* TAG = "konfiguracja";
static constexpr const char* NAZWA_SIECI = "Audio mini config";
static constexpr int LIMIT_MS = 5 * 60 * 1000;   // bez zapisu: powrót do zwykłej pracy

static std::vector<std::string> sieci;            // znalezione przy starcie, od najsilniejszej

static void restart(const char* powod) {
  ESP_LOGW(TAG, "restart: %s", powod);
  dziennik::powod(powod);
  vTaskDelay(pdMS_TO_TICKS(300));
  sprzet::zamroz();
  esp_restart();
}

static std::string html_tekst(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '&': o += "&amp;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

// wartość pola formularza (application/x-www-form-urlencoded) z odkodowaniem %XX i '+'
static std::string pole(const std::string& cialo, const char* nazwa) {
  std::string klucz = std::string(nazwa) + "=";
  size_t p = 0;
  while ((p = cialo.find(klucz, p)) != std::string::npos) {
    if (p == 0 || cialo[p - 1] == '&') break;
    p += klucz.size();
  }
  if (p == std::string::npos) return "";
  p += klucz.size();
  size_t k = cialo.find('&', p);
  std::string v = cialo.substr(p, k == std::string::npos ? std::string::npos : k - p), o;
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i] == '+') o += ' ';
    else if (v[i] == '%' && i + 2 < v.size()) {
      o += (char)strtol(v.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else o += v[i];
  }
  return o;
}

static const char* NAGLOWEK =
    "<!doctype html><html lang=pl><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>Audio mini – Wi-Fi</title>"
    "<style>body{font-family:system-ui,sans-serif;background:#16181d;color:#e8e8ea;margin:0;padding:16px}"
    "main{max-width:420px;margin:auto}h1{font-size:20px}label{display:block;margin:14px 0 6px;color:#a9adb7}"
    "select,input,button{width:100%;box-sizing:border-box;font-size:17px;padding:12px;border-radius:10px;"
    "border:1px solid #3a3f4b;background:#22252c;color:#e8e8ea}button{margin-top:20px;background:#1db954;"
    "border:0;color:#000;font-weight:600}p{color:#a9adb7}</style></head><body><main>";

static esp_err_t strona(httpd_req_t* r) {
  std::string h = NAGLOWEK;
  h += "<h1>Audio mini – sieć Wi-Fi</h1><form method=post action=/zapisz>"
       "<label for=s>Sieć</label><select id=s name=ssid onchange=\"u.hidden=this.value!='*'\">";
  for (auto& s : sieci) h += "<option>" + html_tekst(s) + "</option>";
  // nazwa ukrytej sieci tylko po wybraniu „Inna” – wcześniej wolne pole łatwo było pomylić z hasłem
  h += "<option value='*'>Inna (ukryta) sieć…</option></select>"
       "<div id=u hidden><label for=i>Nazwa ukrytej sieci</label><input id=i name=inna autocomplete=off></div>"
       "<label for=p>Hasło</label><input id=p name=haslo type=password autocomplete=off>"
       "<button>Zapisz i połącz</button></form>"
       "<p>Po zapisaniu Audio mini uruchomi się ponownie i połączy z wybraną siecią.</p></main></body></html>";
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  return httpd_resp_send(r, h.data(), h.size());
}

static esp_err_t zapisz(httpd_req_t* r) {
  std::string cialo;
  if (r->content_len > 0 && r->content_len < 1024) {
    cialo.resize(r->content_len);
    int got = 0;
    while (got < (int)cialo.size()) {
      int n = httpd_req_recv(r, cialo.data() + got, cialo.size() - got);
      if (n <= 0) break;
      got += n;
    }
    cialo.resize(got);
  }
  std::string ssid = pole(cialo, "ssid");
  if (ssid == "*") ssid = pole(cialo, "inna");
  std::string haslo = pole(cialo, "haslo");
  std::string h = NAGLOWEK;
  if (ssid.empty() || ssid.size() > 32 || haslo.size() > 64) {
    h += "<h1>Brak nazwy sieci</h1><p><a href=/ style=color:#1db954>Wróć</a></p></main></body></html>";
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, h.data(), h.size());
  }
  ustawienia::zapisz_wifi(ssid, haslo);
  h += "<h1>Zapisano</h1><p>Audio mini łączy się z siecią „" + html_tekst(ssid) +
       "”. Możesz wrócić do swojej sieci Wi-Fi.</p></main></body></html>";
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_send(r, h.data(), h.size());
  xTaskCreate([](void*) {
    vTaskDelay(pdMS_TO_TICKS(1500));   // odpowiedź zdąży dojść do telefonu
    restart("zapisano sieć Wi-Fi");
  }, "konfig_restart", 4096, nullptr, 5, nullptr);
  return ESP_OK;
}

// każde zapytanie DNS -> 192.168.4.1: telefon wykrywa „stronę logowania” i otwiera konfigurację sam
static void dns(void*) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(s, (sockaddr*)&a, sizeof a);
  uint8_t b[512];
  while (true) {
    sockaddr_in od = {};
    socklen_t dl = sizeof od;
    int n = recvfrom(s, b, sizeof b - 16, 0, (sockaddr*)&od, &dl);
    if (n < 12) continue;
    b[2] = 0x81;                         // odpowiedź, rekurencja
    b[3] = 0x80;
    b[6] = 0; b[7] = 1;                  // 1 odpowiedź
    b[8] = b[9] = b[10] = b[11] = 0;
    static const uint8_t ODP[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
    memcpy(b + n, ODP, sizeof ODP);
    sendto(s, b, n + sizeof ODP, 0, (sockaddr*)&od, dl);
  }
}

void uruchom() {
  ESP_LOGW(TAG, "tryb konfiguracji Wi-Fi: sieć „%s”, strona http://192.168.4.1", NAZWA_SIECI);
  sterownik::tryb_konfiguracji("    WIFI: AUDIO MINI CONFIG  192.168.4.1    ");
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  wifi_config_t ap = {};
  strlcpy((char*)ap.ap.ssid, NAZWA_SIECI, sizeof ap.ap.ssid);
  ap.ap.ssid_len = strlen(NAZWA_SIECI);
  ap.ap.channel = 1;
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
  ESP_ERROR_CHECK(esp_wifi_start());

  // lista sieci raz, przed wystawieniem strony
  if (esp_wifi_scan_start(nullptr, true) == ESP_OK) {
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    std::vector<wifi_ap_record_t> rek(n);
    if (n && esp_wifi_scan_get_ap_records(&n, rek.data()) == ESP_OK) {
      std::sort(rek.begin(), rek.begin() + n, [](auto& x, auto& y) { return x.rssi > y.rssi; });
      for (int i = 0; i < n; i++) {
        std::string s = (const char*)rek[i].ssid;
        if (!s.empty() && std::find(sieci.begin(), sieci.end(), s) == sieci.end()) sieci.push_back(s);
      }
    }
  }
  ESP_LOGI(TAG, "znaleziono %u sieci", (unsigned)sieci.size());

  httpd_handle_t serwer = nullptr;
  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  hc.uri_match_fn = httpd_uri_match_wildcard;
  hc.stack_size = 8192;
  ESP_ERROR_CHECK(httpd_start(&serwer, &hc));
  httpd_uri_t z = {};
  z.uri = "/zapisz";
  z.method = HTTP_POST;
  z.handler = zapisz;
  httpd_register_uri_handler(serwer, &z);
  httpd_uri_t s = {};
  s.uri = "/*";                         // też /generate_204, /hotspot-detect.html itp. (wykrywanie portalu)
  s.method = HTTP_GET;
  s.handler = strona;
  httpd_register_uri_handler(serwer, &s);
  xTaskCreate(dns, "dns", 4096, nullptr, 4, nullptr);

  vTaskDelay(pdMS_TO_TICKS(LIMIT_MS));
  restart("koniec konfiguracji Wi-Fi bez zmian");
}

}  // namespace konfiguracja
