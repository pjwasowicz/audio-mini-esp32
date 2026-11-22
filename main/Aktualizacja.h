#pragma once
// Wgrywanie programu przez Wi-Fi: POST /api/aktualizacja (ciało = audioesp.bin, nagłówek X-Haslo, jeśli
// ustawione hasło). Program trafia do wolnego miejsca OTA, po zapisie restart z nowej wersji. Bez
// automatycznego powrotu: gdy nowa wersja nie działa, wgrywa się przez USB.
#include <memory>

namespace bell {
class BellHTTPServer;
}

namespace aktualizacja {

void rejestruj(std::shared_ptr<bell::BellHTTPServer> serwer);

}  // namespace aktualizacja
