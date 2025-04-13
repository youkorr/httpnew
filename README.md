```
# Exemple de configuration pour ESP-IDF 5.1.5 (simplifiée)
esphome:
  name: esp32-file-server
  friendly_name: ESP32 File Server
  platformio_options:
    platform: espressif32
    platform_version: "5.1.0"  # Correspond à ESP-IDF 5.1.5
    board: esp32-s3-box-3
    framework: espidf

esp32:
  board: esp32-s3-box-3
  framework:
    type: esp-idf
    version: 5.1.0  # Compatible avec ESP-IDF 5.1.5
  
  # Options importantes pour les gros fichiers
  flash_size: 16MB  
  psram: true       # Activer la PSRAM

# Configuration Wi-Fi
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  
  # Optionnel: Point d'accès pour un accès direct
  ap:
    ssid: "ESP32 Files"
    password: "fileserver123"
    
# Composant personnalisé
external_components:
  - source: local
    components: [ftp_http_proxy]

# Configuration FTP-HTTP Proxy simplifiée
ftp_http_proxy:
  id: file_proxy
  ftp_server: "192.168.1.10"  # Votre serveur FTP
  username: "ftpuser"         # Votre nom d'utilisateur FTP
  password: "ftppass"         # Votre mot de passe FTP
  local_port: 8080            # Port HTTP sur l'ESP

# Affichage des logs
logger:
  level: INFO
  logs:
    ftp_proxy: DEBUG

```
