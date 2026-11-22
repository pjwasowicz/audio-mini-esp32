#!/usr/bin/env bash
# Jednorazowe przygotowanie cspot: pobranie (jeśli brak) i łatki zgodności z ESP-IDF 5.5 / dzisiejszym protobuf.
set -euo pipefail
cd "$(dirname "$0")"
IDF="${IDF_PATH:-$HOME/esp/esp-idf}"

[ -d cspot/cspot ] || git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/feelfreelinux/cspot.git cspot

laduj() {   # katalog repozytorium, plik łatki
  if git -C "$1" apply --check "$2" 2>/dev/null; then git -C "$1" apply "$2"; echo "łatka $(basename "$2"): zastosowana"
  else echo "łatka $(basename "$2"): już zastosowana albo niepotrzebna"; fi
}
laduj cspot/cspot/bell "$PWD/patches/nanopb-protobuf7.patch"
laduj cspot/cspot "$PWD/patches/cspot-login-mac.patch"
laduj cspot/cspot "$PWD/patches/cspot-mercury-stabilnosc.patch"   # stos 16 KB, ping od startu, blokada map
laduj cspot/cspot/bell "$PWD/patches/bell-civetweb-threads.patch"
laduj cspot/cspot/bell "$PWD/patches/bell-bufor-audio.patch"   # bufor pełny w CentralAudioBuffer, zapis za tablicą w FadeEffect

# generator protobuf w cspot potrzebuje modułów Pythona w środowisku ESP-IDF
. "$IDF/export.sh" >/dev/null
SSL_CERT_FILE=/etc/ssl/cert.pem python -m pip install --quiet protobuf grpcio-tools "setuptools<81"
touch cspot/.przygotowany
