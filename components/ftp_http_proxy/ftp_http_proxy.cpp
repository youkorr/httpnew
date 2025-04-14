#include "ftp_http_proxy.h"
#include "esphome/core/log.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <string>
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_wifi.h"

static const char *TAG = "ftp_proxy";

// Interface HTML pour le navigateur de fichiers (inclus comme chaîne)
static const char* HTML_INDEX = R"=====(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 File Browser</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }
        h1 { color: #333; }
        .file-list { list-style: none; padding: 0; }
        .file-item { padding: 10px; border-bottom: 1px solid #eee; display: flex; justify-content: space-between; align-items: center; }
        .file-name { flex-grow: 1; }
        .file-actions { display: flex; gap: 10px; }
        .btn { padding: 6px 12px; border-radius: 4px; cursor: pointer; text-decoration: none; font-size: 14px; }
        .download-btn { background: #4CAF50; color: white; border: none; }
        .share-btn { background: #2196F3; color: white; border: none; }
        .toggle-btn { background: #FF9800; color: white; border: none; }
        .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); align-items: center; justify-content: center; }
        .modal-content { background: white; padding: 20px; border-radius: 5px; width: 90%; max-width: 500px; }
        .close-btn { float: right; cursor: pointer; font-size: 20px; }
        .share-link { padding: 10px; background: #f5f5f5; border-radius: 4px; word-break: break-all; margin: 10px 0; }
        .copy-btn { background: #673AB7; color: white; border: none; padding: 5px 10px; cursor: pointer; border-radius: 4px; }
        .success-msg { color: green; display: none; }
        .shareable-badge { display: inline-block; background: #4CAF50; color: white; font-size: 10px; padding: 3px 6px; border-radius: 3px; margin-left: 5px; }
    </style>
</head>
<body>
    <h1>ESP32 File Browser</h1>
    
    <ul class="file-list">
        <!-- Files will be loaded here -->
    </ul>
    
    <div id="shareModal" class="modal">
        <div class="modal-content">
            <span class="close-btn">&times;</span>
            <h2>Partage de fichier</h2>
            <p>Lien de partage (valide pour <span id="expiryHours">24</span> heures):</p>
            <div class="share-link" id="shareLink"></div>
            <button class="copy-btn" onclick="copyShareLink()">Copier</button>
            <span class="success-msg" id="copySuccess">Lien copié!</span>
        </div>
    </div>
    
    <script>
        // Charger la liste des fichiers
        function loadFiles() {
            fetch('/api/files')
                .then(response => response.json())
                .then(files => {
                    const fileList = document.querySelector('.file-list');
                    fileList.innerHTML = '';
                    
                    files.forEach(file => {
                        const li = document.createElement('li');
                        li.className = 'file-item';
                        
                        const nameDiv = document.createElement('div');
                        nameDiv.className = 'file-name';
                        nameDiv.textContent = file.name;
                        
                        if (file.shareable) {
                            const badge = document.createElement('span');
                            badge.className = 'shareable-badge';
                            badge.textContent = 'Partageable';
                            nameDiv.appendChild(badge);
                        }
                        
                        const actionsDiv = document.createElement('div');
                        actionsDiv.className = 'file-actions';
                        
                        const downloadBtn = document.createElement('a');
                        downloadBtn.className = 'btn download-btn';
                        downloadBtn.textContent = 'Télécharger';
                        downloadBtn.href = '/' + file.path;
                        actionsDiv.appendChild(downloadBtn);
                        
                        // Bouton de partage uniquement pour les fichiers
                        if (file.type === 'file') {
                            const toggleBtn = document.createElement('button');
                            toggleBtn.className = 'btn toggle-btn';
                            toggleBtn.textContent = file.shareable ? 'Ne pas partager' : 'Rendre partageable';
                            toggleBtn.onclick = () => toggleShareable(file.path, !file.shareable);
                            actionsDiv.appendChild(toggleBtn);
                            
                            if (file.shareable) {
                                const shareBtn = document.createElement('button');
                                shareBtn.className = 'btn share-btn';
                                shareBtn.textContent = 'Partager';
                                shareBtn.onclick = () => createShareLink(file.path);
                                actionsDiv.appendChild(shareBtn);
                            }
                        }
                        
                        li.appendChild(nameDiv);
                        li.appendChild(actionsDiv);
                        fileList.appendChild(li);
                    });
                })
                .catch(error => console.error('Erreur lors du chargement des fichiers:', error));
        }
        
        // Activer/Désactiver le partage d'un fichier
        function toggleShareable(path, shareable) {
            fetch('/api/toggle-shareable', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: path, shareable: shareable })
            })
            .then(response => {
                if (response.ok) {
                    loadFiles(); // Recharger la liste des fichiers
                } else {
                    console.error('Erreur lors du changement de statut de partage');
                }
            })
            .catch(error => console.error('Erreur:', error));
        }
        
        // Créer un lien de partage
        function createShareLink(path) {
            fetch('/api/share', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: path, expiry: 24 })
            })
            .then(response => response.json())
            .then(data => {
                const shareLink = document.getElementById('shareLink');
                shareLink.textContent = window.location.origin + data.link;
                
                document.getElementById('expiryHours').textContent = data.expiry;
                
                const modal = document.getElementById('shareModal');
                modal.style.display = 'flex';
            })
            .catch(error => console.error('Erreur lors de la création du lien:', error));
        }
        
        // Copier le lien de partage
        function copyShareLink() {
            const shareLink = document.getElementById('shareLink').textContent;
            navigator.clipboard.writeText(shareLink).then(() => {
                const copySuccess = document.getElementById('copySuccess');
                copySuccess.style.display = 'inline';
                setTimeout(() => { copySuccess.style.display = 'none'; }, 2000);
            });
        }
        
        // Fermer la modal
        document.querySelector('.close-btn').onclick = function() {
            document.getElementById('shareModal').style.display = 'none';
        }
        
        // Fermer la modal si on clique en dehors
        window.onclick = function(event) {
            const modal = document.getElementById('shareModal');
            if (event.target == modal) {
                modal.style.display = 'none';
            }
        }
        
        // Charger les fichiers au démarrage
        document.addEventListener('DOMContentLoaded', loadFiles);
    </script>
</body>
</html>
)=====";

namespace esphome {
namespace ftp_http_proxy {

void FTPHTTPProxy::setup() {
  ESP_LOGI(TAG, "Initialisation du proxy FTP/HTTP avec ESP-IDF 5.1.5");

  // Ne pas essayer de réinitialiser le watchdog, utiliser celui déjà configuré
  // Planifier le démarrage du serveur HTTP après un délai pour que le WiFi et LWIP soient prêts
  delayed_setup_ = true;
}

void FTPHTTPProxy::loop() {
  // Premier passage dans loop: initialiser le serveur HTTP
  if (delayed_setup_) {
    static uint8_t startup_counter = 0;
    startup_counter++;
    
    // Attendre 5 passages dans loop pour s'assurer que tout est prêt
    if (startup_counter >= 5) {
      delayed_setup_ = false;
      this->setup_http_server();
    }
    return;
  }

  // Nettoyage des liens de partage expirés
  int64_t now = esp_timer_get_time() / 1000000; // Temps en secondes
  active_shares_.erase(
    std::remove_if(
      active_shares_.begin(), 
      active_shares_.end(),
      [now](const ShareLink& link) { return link.expiry < now; }
    ),
    active_shares_.end()
  );
}

bool FTPHTTPProxy::is_shareable(const std::string &path) {
  // Rechercher le fichier dans notre liste
  for (const auto &file : ftp_files_) {
    if (file.path == path) {
      return file.shareable;
    }
  }
  return false;
}

void FTPHTTPProxy::create_share_link(const std::string &path, int expiry_hours) {
  // Vérifier si le fichier est partageable
  if (!is_shareable(path)) {
    ESP_LOGW(TAG, "Tentative de partage d'un fichier non partageable: %s", path.c_str());
    return;
  }
  
  // Générer un token aléatoire
  uint32_t random_value = esp_random();
  char token[16];
  snprintf(token, sizeof(token), "%08x", random_value);
  
  // Créer le lien de partage avec expiration
  ShareLink share;
  share.path = path;
  share.token = token;
  share.expiry = (esp_timer_get_time() / 1000000) + (expiry_hours * 3600);
  
  // Ajouter à la liste des partages actifs
  active_shares_.push_back(share);
  
  ESP_LOGI(TAG, "Lien de partage créé pour %s: token=%s, expire dans %d heures", 
           path.c_str(), token, expiry_hours);
}

bool FTPHTTPProxy::connect_to_ftp(int& sock, const char* server, const char* username, const char* password) {
  struct hostent *ftp_host = gethostbyname(server);
  if (!ftp_host) {
    ESP_LOGE(TAG, "Échec de la résolution DNS");
    return false;
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "Échec de création du socket : %d", errno);
    return false;
  }

  // Configuration du socket pour être plus robuste
  int flag = 1;
  setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
  
  // Augmenter la taille du buffer de réception
  int rcvbuf = 16384;
  setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  // Timeout pour les opérations socket
  struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(21);
  server_addr.sin_addr.s_addr = *((unsigned long *)ftp_host->h_addr);

  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
    ESP_LOGE(TAG, "Échec de connexion FTP : %d", errno);
    close(sock);
    sock = -1;
    return false;
  }

  char buffer[512];
  int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes_received <= 0 || !strstr(buffer, "220 ")) {
    ESP_LOGE(TAG, "Message de bienvenue FTP non reçu");
    close(sock);
    sock = -1;
    return false;
  }
  buffer[bytes_received] = '\0';

  // Authentification
  snprintf(buffer, sizeof(buffer), "USER %s\r\n", username);
  send(sock, buffer, strlen(buffer), 0);
  bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  buffer[bytes_received] = '\0';

  snprintf(buffer, sizeof(buffer), "PASS %s\r\n", password);
  send(sock, buffer, strlen(buffer), 0);
  bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  buffer[bytes_received] = '\0';
  if (!strstr(buffer, "230 ")) {
    ESP_LOGE(TAG, "Authentification FTP échouée: %s", buffer);
    close(sock);
    sock = -1;
    return false;
  }

  // Mode binaire
  send(sock, "TYPE I\r\n", 8, 0);
  bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  buffer[bytes_received] = '\0';

  return true;
}

/* Cette fonction exécute le transfert de fichier dans une tâche séparée */
// Correction dans la fonction file_transfer_task

void FTPHTTPProxy::file_transfer_task(void* param) {
  FileTransferContext* ctx = (FileTransferContext*)param;
  if (!ctx) {
    ESP_LOGE(TAG, "Contexte de transfert invalide");
    vTaskDelete(NULL);
    return;
  }
  
  ESP_LOGI(TAG, "Démarrage du transfert pour %s", ctx->remote_path.c_str());
  
  // Important: Créer une instance locale de FTPHTTPProxy pose problème
  // car elle n'a pas accès aux données membres de l'instance originale
  // Utilisons directement le contexte transmis
  
  int ftp_sock = -1;
  int data_sock = -1;
  bool success = false;
  int bytes_received = 0;

  // Allocation du buffer avec PSRAM si disponible
  bool has_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0;
  const int buffer_size = 8192;  // Taille optimisée pour l'ESP32-S3
  char* buffer = nullptr;
  
  if (has_psram) {
    buffer = (char*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Utilisation de la PSRAM pour le buffer");
  } else {
    buffer = (char*)malloc(buffer_size);
    ESP_LOGI(TAG, "PSRAM non disponible, utilisation de la RAM standard");
  }
  
  if (!buffer) {
    ESP_LOGE(TAG, "Échec d'allocation pour le buffer");
    goto end_transfer;
  }

  // Connexion au serveur FTP - Utilisez une méthode statique ou accessible
  // Note: Pour simplifier, je mets directement le code connect_to_ftp ici
  {
    struct hostent *ftp_host = gethostbyname(ctx->ftp_server.c_str());
    if (!ftp_host) {
      ESP_LOGE(TAG, "Échec de la résolution DNS");
      goto end_transfer;
    }

    ftp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ftp_sock < 0) {
      ESP_LOGE(TAG, "Échec de création du socket : %d", errno);
      goto end_transfer;
    }

    // Configuration du socket pour être plus robuste
    int flag = 1;
    setsockopt(ftp_sock, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    
    // Augmenter la taille du buffer de réception
    int rcvbuf = 16384;
    setsockopt(ftp_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // Timeout pour les opérations socket
    struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(ftp_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(ftp_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(21);
    server_addr.sin_addr.s_addr = *((unsigned long *)ftp_host->h_addr);

    if (connect(ftp_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
      ESP_LOGE(TAG, "Échec de connexion FTP : %d", errno);
      goto end_transfer;
    }

    bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
    if (bytes_received <= 0 || !strstr(buffer, "220 ")) {
      ESP_LOGE(TAG, "Message de bienvenue FTP non reçu");
      goto end_transfer;
    }
    buffer[bytes_received] = '\0';

    // Authentification
    snprintf(buffer, buffer_size, "USER %s\r\n", ctx->username.c_str());
    send(ftp_sock, buffer, strlen(buffer), 0);
    bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
    if (bytes_received <= 0) {
      ESP_LOGE(TAG, "Échec de réception après USER");
      goto end_transfer;
    }
    buffer[bytes_received] = '\0';

    snprintf(buffer, buffer_size, "PASS %s\r\n", ctx->password.c_str());
    send(ftp_sock, buffer, strlen(buffer), 0);
    bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
    if (bytes_received <= 0) {
      ESP_LOGE(TAG, "Échec de réception après PASS");
      goto end_transfer;
    }
    buffer[bytes_received] = '\0';
    if (!strstr(buffer, "230 ")) {
      ESP_LOGE(TAG, "Authentification FTP échouée: %s", buffer);
      goto end_transfer;
    }

    // Mode binaire
    send(ftp_sock, "TYPE I\r\n", 8, 0);
    bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
    if (bytes_received <= 0) {
      ESP_LOGE(TAG, "Échec de réception après TYPE I");
      goto end_transfer;
    }
    buffer[bytes_received] = '\0';
  }

  // Configuration des headers HTTP
  {
    std::string extension = "";
    size_t dot_pos = ctx->remote_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
      extension = ctx->remote_path.substr(dot_pos);
      std::transform(extension.begin(), extension.end(), extension.begin(), 
                     [](unsigned char c){ return std::tolower(c); });
    }

    // Configuration du type MIME
    if (extension == ".mp3") {
      httpd_resp_set_type(ctx->req, "audio/mpeg");
    } else if (extension == ".wav") {
      httpd_resp_set_type(ctx->req, "audio/wav");
    } else if (extension == ".ogg") {
      httpd_resp_set_type(ctx->req, "audio/ogg");
    } else if (extension == ".mp4") {
      httpd_resp_set_type(ctx->req, "video/mp4");
    } else if (extension == ".pdf") {
      httpd_resp_set_type(ctx->req, "application/pdf");
    } else if (extension == ".jpg" || extension == ".jpeg") {
      httpd_resp_set_type(ctx->req, "image/jpeg");
    } else if (extension == ".png") {
      httpd_resp_set_type(ctx->req, "image/png");
    } else if (extension == ".ico") {
      httpd_resp_set_type(ctx->req, "image/x-icon");
    } else {
      // Type par défaut pour les fichiers inconnus
      httpd_resp_set_type(ctx->req, "application/octet-stream");
      
      // Extraire le nom du fichier pour Content-Disposition
      std::string filename = ctx->remote_path;
      size_t slash_pos = ctx->remote_path.find_last_of('/');
      if (slash_pos != std::string::npos) {
        filename = ctx->remote_path.substr(slash_pos + 1);
      }
      
      std::string header = "attachment; filename=\"" + filename + "\"";
      httpd_resp_set_hdr(ctx->req, "Content-Disposition", header.c_str());
    }
    
    // En-têtes pour permettre la mise en cache et les requêtes par plage
    httpd_resp_set_hdr(ctx->req, "Accept-Ranges", "bytes");
  }

  // Mode passif
  send(ftp_sock, "PASV\r\n", 6, 0);
  bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
  if (bytes_received <= 0 || !strstr(buffer, "227 ")) {
    ESP_LOGE(TAG, "Erreur en mode passif");
    goto end_transfer;
  }
  buffer[bytes_received] = '\0';

  // Analyse de la réponse PASV
  {
    char *pasv_start = strchr(buffer, '(');
    if (!pasv_start) {
      ESP_LOGE(TAG, "Format PASV incorrect");
      goto end_transfer;
    }
    
    int ip[4], port[2];
    if (sscanf(pasv_start, "(%d,%d,%d,%d,%d,%d)", &ip[0], &ip[1], &ip[2], &ip[3], &port[0], &port[1]) != 6) {
      ESP_LOGE(TAG, "Impossible de parser la réponse PASV");
      goto end_transfer;
    }
    int data_port = port[0] * 256 + port[1];
    
    // Connexion au port de données
    data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
      ESP_LOGE(TAG, "Échec de création du socket de données");
      goto end_transfer;
    }
    
    int flag = 1;
    setsockopt(data_sock, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    
    int rcvbuf = 32768;
    setsockopt(data_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    struct timeval data_timeout = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, &data_timeout, sizeof(data_timeout));
    
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(data_port);
    // Construire l'adresse correctement avec htonl
    data_addr.sin_addr.s_addr = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
    
    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) != 0) {
      ESP_LOGE(TAG, "Échec de connexion au port de données: %d", errno);
      goto end_transfer;
    }
  }

  // Envoyer la commande RETR pour récupérer le fichier
  snprintf(buffer, buffer_size, "RETR %s\r\n", ctx->remote_path.c_str());
  send(ftp_sock, buffer, strlen(buffer), 0);

  bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
  if (bytes_received <= 0) {
    ESP_LOGE(TAG, "Pas de réponse à la commande RETR");
    goto end_transfer;
  }
  buffer[bytes_received] = '\0';
  
  // Vérifier si le fichier existe
  if (!strstr(buffer, "150 ") && !strstr(buffer, "125 ")) {
    ESP_LOGE(TAG, "Fichier non trouvé ou inaccessible: %s", buffer);
    goto end_transfer;
  }
  
  ESP_LOGI(TAG, "Téléchargement du fichier %s démarré", ctx->remote_path.c_str());

  // Boucle principale de transfert de données
  {
    size_t total_bytes_transferred = 0;
    bool data_transfer_error = false;
    
    while (true) {
      bytes_received = recv(data_sock, buffer, buffer_size, 0);
      if (bytes_received <= 0) {
        if (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          ESP_LOGE(TAG, "Erreur de réception des données: %d", errno);
          data_transfer_error = true;
        }
        break;
      }
      
      // Mise à jour du total transféré
      total_bytes_transferred += bytes_received;
      
      // Envoi du chunk au client HTTP
      esp_err_t err = httpd_resp_send_chunk(ctx->req, buffer, bytes_received);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'envoi au client: %d", err);
        data_transfer_error = true;
        break;
      }
      
      // Journalisation périodique pour suivre la progression
      if (total_bytes_transferred % (512 * 1024) == 0) { // Log tous les 512KB
        ESP_LOGI(TAG, "Transfert en cours: %zu KB", total_bytes_transferred / 1024);
      }
      
      // Yield pour permettre à d'autres tâches de s'exécuter
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Vérifier que le transfert s'est bien terminé
    if (data_sock != -1) {
      close(data_sock);
      data_sock = -1;
    }
    
    if (!data_transfer_error) {
      // Attendre la confirmation du transfert complet
      bytes_received = recv(ftp_sock, buffer, buffer_size - 1, 0);
      if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        if (strstr(buffer, "226 ")) {
          ESP_LOGI(TAG, "Transfert terminé avec succès: %zu KB (%zu MB)", 
                  total_bytes_transferred / 1024,
                  total_bytes_transferred / (1024 * 1024));
          success = true;
        } else {
          ESP_LOGW(TAG, "Fin de transfert incomplète ou inattendue: %s", buffer);
        }
      } else {
        ESP_LOGW(TAG, "Pas de réponse de confirmation du transfert");
      }
    }
  }

end_transfer:
  // Nettoyage des ressources
  if (buffer) {
    if (has_psram) {
      heap_caps_free(buffer);
    } else {
      free(buffer);
    }
  }
  
  if (data_sock != -1) close(data_sock);
  if (ftp_sock != -1) {
    send(ftp_sock, "QUIT\r\n", 6, 0);
    close(ftp_sock);
  }
  
  // Terminer la réponse HTTP
  if (!success) {
    httpd_resp_send_err(ctx->req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erreur de transfert de fichier");
  } else {
    // Fin du chunk pour terminer la réponse
    httpd_resp_send_chunk(ctx->req, NULL, 0);
  }
  
  // Libérer la mémoire du contexte
  delete ctx;
  
  // Supprimer cette tâche
  vTaskDelete(NULL);
}

// Code corrigé pour le http_req_handler
esp_err_t FTPHTTPProxy::http_req_handler(httpd_req_t *req) {
  auto *proxy = (FTPHTTPProxy *)req->user_ctx;
  std::string requested_path = req->uri;

  // Suppression du premier slash
  if (!requested_path.empty() && requested_path[0] == '/') {
    requested_path.erase(0, 1);
  }

  ESP_LOGI(TAG, "Requête de téléchargement reçue: %s", requested_path.c_str());
  
  // Vérifier si la requête est pour favicon.ico et qu'il n'existe pas sur le serveur FTP
  if (requested_path == "favicon.ico") {
    // Envoyer une icône par défaut ou une réponse 404
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, "", 0);  // Juste une réponse vide pour l'instant
    return ESP_OK;
  }
  
  // Vérifier si c'est un lien de partage valide
  bool path_valid = false;
  
  // Format typique: /share/TOKEN
  if (requested_path.compare(0, 6, "share/") == 0) {
    std::string token = requested_path.substr(6);
    
    // Chercher le token dans les partages actifs
    for (const auto &share : proxy->active_shares_) {
      if (share.token == token) {
        requested_path = share.path;
        path_valid = true;
        ESP_LOGI(TAG, "Accès via lien de partage: %s -> %s", token.c_str(), requested_path.c_str());
        break;
      }
    }
  } else {
    // Vérifier si le fichier est dans la liste des fichiers connus
    // Tous les fichiers connus sont accessibles directement
    path_valid = true;
  }
  
  if (!path_valid) {
    ESP_LOGW(TAG, "Chemin non autorisé: %s", requested_path.c_str());
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Fichier non trouvé ou accès non autorisé");
    return ESP_FAIL;
  }

  // Créer le contexte de transfert avec vérification de mémoire
  FileTransferContext* ctx = new (std::nothrow) FileTransferContext;
  if (!ctx) {
    ESP_LOGE(TAG, "Erreur d'allocation pour le contexte de transfert");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erreur mémoire");
    return ESP_FAIL;
  }
  
  // Configurer le contexte avec toutes les informations nécessaires
  ctx->remote_path = requested_path;
  ctx->req = req;
  ctx->ftp_server = proxy->ftp_server_;
  ctx->username = proxy->username_;
  ctx->password = proxy->password_;

  // Créer une tâche dédiée pour le transfert de fichier pour éviter le blocage
  BaseType_t task_created = xTaskCreatePinnedToCore(
    file_transfer_task,           // Fonction de tâche
    "file_transfer",              // Nom de tâche
    8192,                         // Taille de la pile
    ctx,                          // Paramètres de la tâche
    tskIDLE_PRIORITY + 1,         // Priorité
    NULL,                         // Handle (non nécessaire)
    1                             // S'exécute sur le cœur 1 (laisse le cœur 0 pour l'interface WiFi)
  );

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Échec de création de la tâche de transfert");
    delete ctx;
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erreur serveur");
    return ESP_FAIL;
  }

  // La tâche dédiée va gérer le transfert et la réponse HTTP
  return ESP_OK;
}

// Code pour gérer le favicon.ico dans le handler de fichiers statiques
esp_err_t FTPHTTPProxy::static_files_handler(httpd_req_t *req) {
  // Interface principale
  if (strcmp(req->uri, "/") == 0 || strcmp(req->uri, "/index.html") == 0) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_INDEX, strlen(HTML_INDEX));
    return ESP_OK;
  }
  
  // Gestion spécifique du favicon.ico
  if (strcmp(req->uri, "/favicon.ico") == 0) {
    // Envoyer une icône par défaut (un petit fichier ico vide)
    // ou répondre avec 204 No Content
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
  }
  
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Fichier non trouvé");
  return ESP_FAIL;
}

}  // namespace ftp_http_proxy
}  // namespace esphome








