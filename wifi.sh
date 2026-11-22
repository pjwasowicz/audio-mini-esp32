#!/usr/bin/env bash
# Zapisuje ustawienia Wi-Fi do sdkconfig.local (zostaje tylko na tym komputerze).
set -euo pipefail
cd "$(dirname "$0")"
read -rp "Sieć Wi-Fi (SSID): " SSID
read -rsp "Hasło Wi-Fi: " PASS; echo
read -rp "Nazwa w Spotify [Audio mini]: " NAZWA
# w plikach sdkconfig znaki \ i " w napisach muszą być poprzedzone ukośnikiem
kconfig() { local s="${1//\\/\\\\}"; printf '%s' "${s//\"/\\\"}"; }
cat > sdkconfig.local <<KONIEC
CONFIG_AUDIO_WIFI_SSID="$(kconfig "$SSID")"
CONFIG_AUDIO_WIFI_PASSWORD="$(kconfig "$PASS")"
CONFIG_AUDIO_DEVICE_NAME="$(kconfig "${NAZWA:-Audio mini}")"
KONIEC
chmod 600 sdkconfig.local
echo "Zapisano sdkconfig.local"
