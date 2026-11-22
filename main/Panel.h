#pragma once
// Panel WWW (pilot) – ta sama strona i te same adresy API co na Pi (AudioMini/pi/webgui), na serwerze
// HTTP cspot (port 8080, obok logowania Spotify /spotify_info).
#include <memory>

namespace bell {
class BellHTTPServer;
}

namespace panel {
void rejestruj(std::shared_ptr<bell::BellHTTPServer> serwer);
}
