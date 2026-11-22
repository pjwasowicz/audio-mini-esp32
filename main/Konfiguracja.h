#pragma once
// Tryb konfiguracji Wi-Fi (z menu gałki „CONF”): otwarta sieć „Audio mini config”, strona pod 192.168.4.1
// (telefon otwiera ją sam – DNS odpowiada na każde pytanie adresem ESP) z listą znalezionych sieci i hasłem.
// Po zapisaniu restart i łączenie z nową siecią; bez zmian przez 5 minut – restart do zwykłej pracy.

namespace konfiguracja {

void uruchom();   // zamiast zwykłego Wi-Fi i Spotify; nie wraca

}  // namespace konfiguracja
