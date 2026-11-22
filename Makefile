# AudioESP – kompilacja i wgrywanie (ESP-IDF 5.5 w ~/esp/esp-idf).
#   make            kompilacja (za pierwszym razem pyta o Wi-Fi i przygotowuje cspot)
#   make flash      wgranie do ESP32 przez USB (PORT=/dev/cu.usbserial-XXXX, jeśli kilka portów)
#   make ota        wgranie przez Wi-Fi (ADRES=192.168.1.130; hasło: CONFIG_AUDIO_OTA_HASLO w menuconfig)
#   make monitor    komunikaty z ESP32 (wyjście: Ctrl+])
#   make wifi       zmiana sieci Wi-Fi / nazwy
#   make clean      usunięcie kompilacji
SHELL := /bin/bash
IDF_PATH ?= $(HOME)/esp/esp-idf
IDF := source $(IDF_PATH)/export.sh >/dev/null 2>&1 && idf.py $(if $(PORT),-p $(PORT))

.PHONY: all build flash ota monitor wifi clean
all: build

build: sdkconfig.local cspot/.przygotowany
	@# zmienione Wi-Fi/nazwa: wygeneruj sdkconfig od nowa z ustawień domyślnych
	@if [ sdkconfig.local -nt sdkconfig ] || [ sdkconfig.defaults -nt sdkconfig ]; then rm -f sdkconfig; fi
	$(IDF) build

flash: build
	$(IDF) flash

ADRES ?= 192.168.1.130
# hasło czytane z sdkconfig przez skrypt, nie wypisywane
ota: build
	@H=$$(sed -n 's/^CONFIG_AUDIO_OTA_HASLO="\(.*\)"$$/\1/p' sdkconfig); \
	echo "wgrywam przez Wi-Fi do $(ADRES)…"; \
	curl --fail-with-body -sS --max-time 180 -H "X-Haslo: $$H" -H "Content-Type: application/octet-stream" \
	  --data-binary @build/audioesp.bin http://$(ADRES):8080/api/aktualizacja; echo

monitor:
	$(IDF) monitor

sdkconfig.local:
	./wifi.sh

wifi:
	./wifi.sh

cspot/.przygotowany:
	./setup.sh

clean:
	rm -rf build sdkconfig
