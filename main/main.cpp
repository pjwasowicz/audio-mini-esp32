// AudioESP – „Audio mini” na ESP32-DevKitC-VIE: Spotify Connect (cspot) + PT2258 + przekaźniki wejść + wyświetlacz 14-seg + gałka
// + panel WWW. Odpowiednik programów z Raspberry Pi (AudioMini/pi).
//
// Spotify: urządzenie ogłasza się w sieci (zeroconf, _spotify-connect._tcp) – każdy telefon w sieci może się
// połączyć ze swoim kontem Premium. Dane logowania (token od Spotify, nie hasło) są zapisywane we flash,
// więc po włączeniu ESP32 łączy się samo i „Audio mini” od razu jest na liście urządzeń. Połączenie z innego
// konta zapisuje nowe dane i restartuje program (2–3 s), jak przełączenie konta w go-librespot.
#include <MDNSService.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <CSpotContext.h>
#include <LoginBlob.h>
#include <SpircHandler.h>

#include "BellHTTPServer.h"
#include "BellUtils.h"
#include "Logger.h"
#include "civetweb.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "mdns.h"
#include "sdkconfig.h"

#include "Aktualizacja.h"
#include "Dziennik.h"
#include "I2SAudioSink.h"
#include "Konfiguracja.h"
#include "Panel.h"
#include "SpotifyPlayer.h"
#include "Sprzet.h"
#include "Sterownik.h"
#include "Ustawienia.h"

static const char* TAG = "audioesp";

extern "C" {
void app_main(void);
}

static int64_t teraz_ms() { return esp_timer_get_time() / 1000; }

// Ciche wznowienie: restart programu z zamrożonymi przekaźnikami; po starcie PT2258 dostaje te same ustawienia
// bez czyszczenia rejestrów, a Spotify łączy się zapamiętanym kontem – w głośnikach nic nie stuka.
static void wznow(const char* powod) {
  ESP_LOGW(TAG, "wznowienie: %s", powod);
  dziennik::powod(powod);
  sterownik::stan_sieci("wznawiam...");
  vTaskDelay(pdMS_TO_TICKS(300));
  sprzet::zamroz();
  esp_restart();
}

// ── Wi-Fi ────────────────────────────────────────────────────────────────────────────────────────────
// Po zerwaniu (restart routera, zakłócenia) ponawianie bez końca, z przerwą 1 s, 2 s, 4 s … do 30 s.
static EventGroupHandle_t wifiEvents;
static constexpr int WIFI_CONNECTED_BIT = BIT0;
static esp_timer_handle_t wifiPonow = nullptr;
static int wifiPrzerwa_ms = 1000;
static std::atomic<int64_t> wifiZerwaneOd{0};     // 0 = połączone
static std::atomic<bool> wifiPoAwarii{false};     // po powrocie sieci trzeba odnowić sesję Spotify

static void wifiEventHandler(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(wifiEvents, WIFI_CONNECTED_BIT);
    if (!wifiZerwaneOd) wifiZerwaneOd = teraz_ms();
    auto* d = (wifi_event_sta_disconnected_t*)data;
    ESP_LOGW(TAG, "Wi-Fi rozłączone: powód %d (%s), punkt %02x:%02x:%02x:%02x:%02x:%02x, sygnał %d dBm; ponowię za %d ms",
             d->reason, esp_err_to_name(d->reason), d->bssid[0], d->bssid[1], d->bssid[2], d->bssid[3], d->bssid[4],
             d->bssid[5], d->rssi, wifiPrzerwa_ms);
    sterownik::stan_sieci("brak Wi-Fi...");
    if (d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT || d->reason == WIFI_REASON_MIC_FAILURE)
      sterownik::blad_wifi("BAD WIFI PASSWORD");
    else if (d->reason == WIFI_REASON_NO_AP_FOUND)
      sterownik::blad_wifi("NO NETWORK");
    else
      sterownik::blad_wifi("NO WIFI");
    esp_timer_start_once(wifiPonow, (uint64_t)wifiPrzerwa_ms * 1000);
    wifiPrzerwa_ms = std::min(wifiPrzerwa_ms * 2, 30000);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(wifiEvents, WIFI_CONNECTED_BIT);
    wifiPrzerwa_ms = 1000;
    if (wifiZerwaneOd && teraz_ms() - wifiZerwaneOd > 5000) wifiPoAwarii = true;   // dłuższa przerwa
    wifiZerwaneOd = 0;
    sterownik::blad_wifi("");
    sterownik::stan_sieci("czekam na Spotify");
  }
}

static void wifiConnect() {
  wifiEvents = xEventGroupCreate();
  esp_timer_create_args_t t = {};
  t.callback = [](void*) { esp_wifi_connect(); };
  t.name = "wifi_ponow";
  ESP_ERROR_CHECK(esp_timer_create(&t, &wifiPonow));
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t* sta = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(sta, CONFIG_AUDIO_HOSTNAME);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr));

  // sieć z trybu konfiguracji (menu „CONF”) ma pierwszeństwo przed ustawieniami z kompilacji
  std::string ssid, haslo;
  if (!ustawienia::wifi(ssid, haslo)) {
    ssid = CONFIG_AUDIO_WIFI_SSID;
    haslo = CONFIG_AUDIO_WIFI_PASSWORD;
  }
  wifi_config_t wifiCfg = {};
  strlcpy((char*)wifiCfg.sta.ssid, ssid.c_str(), sizeof(wifiCfg.sta.ssid));
  strlcpy((char*)wifiCfg.sta.password, haslo.c_str(), sizeof(wifiCfg.sta.password));
  ESP_LOGI(TAG, "Wi-Fi: sieć %s", ssid.c_str());
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiCfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_ps(WIFI_PS_NONE);          // bez oszczędzania energii – stabilny strumień (lekcja z RT5370 na Pi)

  sterownik::stan_sieci("łączę z Wi-Fi...");
  xEventGroupWaitBits(wifiEvents, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

// zegar z internetu, strefa Polski (CET/CEST)
static void czas() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&cfg);
}

// ── Spotify ──────────────────────────────────────────────────────────────────────────────────────────
static std::atomic<bool> sesjaAktywna = false;
static std::atomic<int64_t> ostatniPakiet{0};
static constexpr int64_t CISZA_MS = 30000;       // tyle po przekroczeniu limitu pingów cspot (2 min + 5 s)

// Zapis do flash z wątku serwera WWW (stos w PSRAM) kończy się wywrotką – zapis robi to zadanie
// (stos w pamięci wewnętrznej), potem restart.
static std::string loginDoZapisu;

static void restartZa(int ms) {
  xTaskCreate(
      [](void* a) {
        vTaskDelay(pdMS_TO_TICKS((int)(intptr_t)a));
        ustawienia::zapisz_login(loginDoZapisu);
        wznow("logowanie z innego konta");
      },
      "restart", 4096, (void*)(intptr_t)ms, 5, nullptr);
}

// Polecenia do Spotify (pauza, utwór, głośność) z gałki, pilota i WWW: cspot nie jest odporny na wywołania
// z kilku wątków (MercurySession bez blokad), więc wykonuje je dopiero wątek cspot między pakietami.
static std::mutex poleceniaMtx;
static std::vector<std::function<void()>> polecenia;

static void doSpotify(std::function<void()> f) {
  std::lock_guard<std::mutex> l(poleceniaMtx);
  if (polecenia.size() < 16) polecenia.push_back(std::move(f));
}

static void wykonajPolecenia() {
  std::vector<std::function<void()>> teraz;
  {
    std::lock_guard<std::mutex> l(poleceniaMtx);
    teraz.swap(polecenia);
  }
  for (auto& f : teraz) f();
}

// Strażnik sesji: połączenie TCP po restarcie routera potrafi „wisieć” bez błędu – wtedy brak pingów od
// serwera; po dłuższej przerwie Wi-Fi też łączymy od nowa (dopiero gdy sieć wróciła).
static void straznik(void*) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (!sesjaAktywna || wifiZerwaneOd) continue;
    if (wifiPoAwarii) wznow("Wi-Fi wróciło po przerwie");
    if (teraz_ms() - ostatniPakiet > CISZA_MS) wznow("brak sygnału od serwera Spotify");
  }
}

class CSpotTask {
 public:
  static void entry(void*) {
    CSpotTask().runTask();
    vTaskDelete(nullptr);
  }

  void runTask() {
    mdns_init();
    mdns_hostname_set(CONFIG_AUDIO_HOSTNAME);
    mdns_instance_name_set(CONFIG_AUDIO_DEVICE_NAME);
    std::atomic<bool> gotBlob = false;
    auto blob = std::make_shared<cspot::LoginBlob>(CONFIG_AUDIO_DEVICE_NAME);

    std::shared_ptr<bell::BellHTTPServer> server = std::make_shared<bell::BellHTTPServer>(8080);
    panel::rejestruj(server);
    aktualizacja::rejestruj(server);   // wgrywanie programu przez Wi-Fi (make ota)
    mdns_service_add(CONFIG_AUDIO_DEVICE_NAME, "_http", "_tcp", 8080, nullptr, 0);

    server->registerGet("/spotify_info", [server, blob](struct mg_connection*) {
      return server->makeJsonResponse(blob->buildZeroconfInfo());
    });
    server->registerPost("/spotify_info", [server, blob, &gotBlob](struct mg_connection* conn) {
      nlohmann::json obj;
      obj["status"] = 101;
      obj["spotifyError"] = 0;
      obj["statusString"] = "ERROR-OK";

      std::string body;
      auto requestInfo = mg_get_request_info(conn);
      if (requestInfo->content_length > 0) {
        size_t total = requestInfo->content_length;
        body.resize(total);
        size_t got = 0;
        while (got < total) {
          int n = mg_read(conn, body.data() + got, total - got);
          if (n <= 0) break;
          got += n;
        }
        body.resize(got);

        mg_header hd[16];
        int num = mg_split_form_urlencoded(body.data(), hd, 16);
        std::map<std::string, std::string> queryMap;
        for (int i = 0; i < num; i++) queryMap[hd[i].name] = hd[i].value;

        try {
          // logowanie z telefonu (dowolne konto): odszyfrowanie tym samym obiektem, którego klucz
          // publiczny poszedł w GET /spotify_info; bez sesji zapis po udanym logowaniu w runTask
          blob->loadZeroconfQuery(queryMap);
          BELL_LOG(info, "cspot", "Logowanie z telefonu: %s", blob->getUserName().c_str());
          if (sesjaAktywna) {
            loginDoZapisu = blob->toJson();  // przełączenie konta: zapis i restart po odpowiedzi do telefonu
            restartZa(800);
          } else {
            gotBlob = true;
          }
        } catch (const std::exception& e) {
          BELL_LOG(error, "cspot", "Logowanie nieudane: %s", e.what());
          obj["status"] = 303;
          obj["statusString"] = "ERROR-INVALID-ARGUMENTS";
        }
      }
      return server->makeJsonResponse(obj.dump());
    });
    bell::MDNSService::registerService(blob->getDeviceName(), "_spotify-connect", "_tcp", "", 8080,
                                       {{"VERSION", "1.0"}, {"CPath", "/spotify_info"}, {"Stack", "SP"}});

    // zapamiętane logowanie: od razu, bez czekania na telefon
    std::string zapisany = ustawienia::login();
    if (!zapisany.empty()) {
      try {
        blob->loadJson(zapisany);
        gotBlob = true;
        BELL_LOG(info, "cspot", "Zapamiętane logowanie: %s", blob->getUserName().c_str());
      } catch (const std::exception& e) {
        BELL_LOG(error, "cspot", "Zapisane dane logowania nieczytelne: %s", e.what());
      }
    }

    while (!gotBlob) {
      BELL_SLEEP_MS(1000);
    }
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
      // płytka bez PSRAM (np. WROOM-32): cspot nie zmieści się w pamięci – panel i Wi-Fi działają dalej
      BELL_LOG(error, "cspot", "Brak PSRAM - Spotify wyłączony (potrzebny moduł WROVER)");
      sterownik::stan_sieci("brak PSRAM");
      while (true) BELL_SLEEP_MS(60000);
    }

    auto ctx = cspot::Context::createFromBlob(blob);
    bool connected = false;
    for (int attempt = 0; attempt < 5 && !connected; attempt++) {
      try {
        ctx->session->connectWithRandomAp();
        connected = true;
      } catch (const std::exception& e) {
        BELL_LOG(error, "cspot", "Brak połączenia z serwerem Spotify (%d): %s", attempt, e.what());
        BELL_SLEEP_MS(2000);
      }
    }
    if (!connected) {
      sterownik::stan_sieci("brak Spotify...");
      BELL_SLEEP_MS(10000);
      wznow("brak połączenia z serwerem Spotify");
    }
    try {
      ctx->config.authData = ctx->session->authenticate(blob);
    } catch (const std::exception& e) {
      BELL_LOG(error, "cspot", "Logowanie odrzucone: %s", e.what());
    }
    if (ctx->config.authData.empty()) {
      // zapisane dane nieważne (np. zmiana hasła) – czekamy na wybór urządzenia w telefonie
      ustawienia::zapisz_login("");
      sterownik::stan_sieci("wybierz w Spotify");
      BELL_SLEEP_MS(3000);
      wznow("zapisane logowanie nieważne");
    }
    // token wielokrotnego użytku od Spotify zastępuje dane z telefonu
    blob->authType = 1;                   // AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS
    blob->authData = ctx->config.authData;
    ustawienia::zapisz_login(blob->toJson());

    auto handler = std::make_shared<cspot::SpircHandler>(ctx);
    ctx->session->startTask();
    auto sink = std::make_unique<I2SAudioSink>(CONFIG_PIN_I2S_BCK, CONFIG_PIN_I2S_LRCK, CONFIG_PIN_I2S_DIN);
    auto player = std::make_shared<SpotifyPlayer>(std::move(sink), handler);
    ostatniPakiet = teraz_ms();
    wifiPoAwarii = false;
    sesjaAktywna = true;
    xTaskCreate(straznik, "straznik", 3072, nullptr, 4, nullptr);

    sterownik::Spotify s;
    s.pauza = [handler](bool p) { doSpotify([handler, p] { handler->setPause(p); }); };
    s.nastepny = [handler]() { doSpotify([handler] { handler->nextSong(); }); };
    s.poprzedni = [handler]() { doSpotify([handler] { handler->previousSong(); }); };
    s.glosnosc = [handler](int v) { doSpotify([handler, v] { handler->setRemoteVolume(v); }); };
    sterownik::spotify_polaczony(s);
    sterownik::stan_sieci("Spotify gotowy");
    BELL_LOG(info, "cspot", "Spotify połączony jako %s", blob->getUserName().c_str());

    try {
      while (true) {
        ctx->session->handlePacket();     // czeka na pakiet najwyżej 200 ms
        if (!ctx->session->triggerTimeout()) ostatniPakiet = teraz_ms();   // serwer pinguje co 2 min
        wykonajPolecenia();
      }
    } catch (const std::exception& e) {
      BELL_LOG(error, "cspot", "Sesja przerwana: %s", e.what());
    }
    // zerwane połączenie z serwerem: restart, po starcie zapamiętane logowanie łączy od nowa
    // poczekaj na Wi-Fi (jeśli właśnie zniknęło), potem połącz od nowa
    xEventGroupWaitBits(wifiEvents, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    BELL_SLEEP_MS(2000);
    wznow("sesja Spotify przerwana");
  }
};

void app_main(void) {
  ustawienia::init();
  dziennik::start();
  sterownik::init();                      // przekaźniki, PT2258, wyświetlacz i gałka działają też bez sieci
  sterownik::na_konfiguracje([] {
    ustawienia::ustaw_tryb_konfig();
    wznow("konfiguracja Wi-Fi z menu");
  });
  if (ustawienia::tryb_konfig()) konfiguracja::uruchom();   // nie wraca: restart po zapisie albo 5 min

  wifiConnect();
  ESP_LOGI(TAG, "Wi-Fi połączone");
  czas();

  bell::setDefaultLogger();
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
    xTaskCreatePinnedToCoreWithCaps(CSpotTask::entry, "cspot", 32 * 1024, nullptr, 5, nullptr, 1,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  } else {
    ESP_LOGW(TAG, "brak PSRAM: tylko Wi-Fi, panel WWW i ogłaszanie w sieci");
    xTaskCreatePinnedToCore(CSpotTask::entry, "cspot", 16 * 1024, nullptr, 5, nullptr, 1);
  }
  vTaskSuspend(nullptr);
}
