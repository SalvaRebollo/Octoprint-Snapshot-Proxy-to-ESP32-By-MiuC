#include "wifi_manager.h"

#include <Preferences.h>

namespace {
constexpr char WIFI_NAMESPACE[] = "octowifi";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t RETRY_DELAY_MS = 10000;

bool isSecured(wifi_auth_mode_t authMode) {
  return authMode != WIFI_AUTH_OPEN;
}
}

WifiManager wifiManager;

void WifiManager::begin() {
  loadSavedNetworks();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);

  startScan(true);
}

void WifiManager::loop() {
  if (currentState == State::SCANNING) {
    int result = WiFi.scanComplete();
    if (result >= 0) {
      processScanResults(result);
      WiFi.scanDelete();

      if (autoConnectAfterScan) {
        buildCandidates();
        if (candidateCount > 0) {
          candidateIndex = 0;
          connectCurrentCandidate();
        } else {
          retryAtMs = millis() + RETRY_DELAY_MS;
          setState(State::NO_KNOWN_NETWORK);
        }
      } else if (connectedBeforeScan && WiFi.status() == WL_CONNECTED) {
        setState(State::CONNECTED);
      } else {
        retryAtMs = millis() + RETRY_DELAY_MS;
        setState(State::IDLE);
      }
    } else if (result == WIFI_SCAN_FAILED) {
      WiFi.scanDelete();
      retryAtMs = millis() + RETRY_DELAY_MS;
      setState(State::ERROR);
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (currentState != State::CONNECTED) {
      setState(State::CONNECTED);
      connectionChanged = true;
    }
    return;
  }

  if (currentState == State::CONNECTED) {
    connectionChanged = true;
    retryAtMs = millis() + RETRY_DELAY_MS;
    setState(State::IDLE);
  }

  if (currentState == State::CONNECTING) {
    if (millis() - connectStartedMs >= CONNECT_TIMEOUT_MS) {
      tryNextCandidate();
    }
    return;
  }

  if (
    (currentState == State::IDLE ||
     currentState == State::NO_KNOWN_NETWORK ||
     currentState == State::ERROR) &&
    savedNetworkCount > 0 &&
    static_cast<int32_t>(millis() - retryAtMs) >= 0
  ) {
    startScan(true);
  }
}

bool WifiManager::startScan(bool connectAfterScan) {
  if (currentState == State::SCANNING) return false;

  if (currentState == State::CONNECTING) {
    WiFi.disconnect(false, false);
  }

  autoConnectAfterScan = connectAfterScan;
  connectedBeforeScan = WiFi.status() == WL_CONNECTED;

  int result = WiFi.scanNetworks(true, true, false, 300);
  if (result == WIFI_SCAN_FAILED) {
    retryAtMs = millis() + RETRY_DELAY_MS;
    setState(State::ERROR);
    return false;
  }

  setState(State::SCANNING);
  return true;
}

void WifiManager::reconnect() {
  WiFi.disconnect(false, false);
  retryAtMs = millis();
  startScan(true);
}

bool WifiManager::saveAndConnect(
  const String &ssid,
  const String &password
) {
  String cleanSsid = ssid;
  cleanSsid.trim();
  if (cleanSsid.isEmpty() || cleanSsid.length() > 32 || password.length() > 63) {
    return false;
  }

  int existingIndex = findSavedNetwork(cleanSsid);
  uint8_t targetIndex;

  if (existingIndex >= 0) {
    targetIndex = static_cast<uint8_t>(existingIndex);
  } else if (savedNetworkCount < MAX_SAVED_NETWORKS) {
    targetIndex = savedNetworkCount++;
  } else {
    for (uint8_t i = 1; i < MAX_SAVED_NETWORKS; i++) {
      savedNetworks[i - 1] = savedNetworks[i];
    }
    targetIndex = MAX_SAVED_NETWORKS - 1;
  }

  savedNetworks[targetIndex].ssid = cleanSsid;
  savedNetworks[targetIndex].password = password;

  if (!persistSavedNetworks()) {
    loadSavedNetworks();
    return false;
  }

  WiFi.scanDelete();
  WiFi.disconnect(false, false);
  candidateCount = 1;
  candidateIndex = 0;
  candidates[0].savedIndex = targetIndex;
  candidates[0].rssi = -127;
  connectCurrentCandidate();
  return true;
}

bool WifiManager::connectSaved(const String &ssid) {
  int savedIndex = findSavedNetwork(ssid);
  if (savedIndex < 0) return false;

  WiFi.scanDelete();
  WiFi.disconnect(false, false);
  candidateCount = 1;
  candidateIndex = 0;
  candidates[0].savedIndex = static_cast<uint8_t>(savedIndex);
  candidates[0].rssi = -127;
  connectCurrentCandidate();
  return true;
}

bool WifiManager::forgetNetwork(const String &ssid) {
  int index = findSavedNetwork(ssid);
  if (index < 0) return false;

  bool wasConnected =
    WiFi.status() == WL_CONNECTED && WiFi.SSID() == savedNetworks[index].ssid;

  for (uint8_t i = static_cast<uint8_t>(index) + 1; i < savedNetworkCount; i++) {
    savedNetworks[i - 1] = savedNetworks[i];
  }

  if (savedNetworkCount > 0) {
    savedNetworkCount--;
    savedNetworks[savedNetworkCount].ssid = "";
    savedNetworks[savedNetworkCount].password = "";
  }

  if (!persistSavedNetworks()) {
    loadSavedNetworks();
    return false;
  }

  if (wasConnected) {
    WiFi.disconnect(false, false);
    connectionChanged = true;
  }

  retryAtMs = millis();
  startScan(true);
  return true;
}

WifiManager::State WifiManager::state() const {
  return currentState;
}

bool WifiManager::isConnecting() const {
  return currentState == State::SCANNING || currentState == State::CONNECTING;
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::consumeConnectionChanged() {
  bool changed = connectionChanged;
  connectionChanged = false;
  return changed;
}

const char *WifiManager::statusText() const {
  switch (currentState) {
    case State::IDLE: return "esperando";
    case State::SCANNING: return "buscando redes";
    case State::CONNECTING: return "conectando";
    case State::CONNECTED: return "conectado";
    case State::NO_KNOWN_NETWORK: return "sin redes conocidas disponibles";
    case State::ERROR: return "error de escaneo";
    default: return "desconocido";
  }
}

uint8_t WifiManager::savedCount() const {
  return savedNetworkCount;
}

bool WifiManager::isSaved(const String &ssid) const {
  return findSavedNetwork(ssid) >= 0;
}

uint8_t WifiManager::scanCount() const {
  return visibleScanCount;
}

const WifiManager::ScanNetwork &WifiManager::scanNetwork(uint8_t index) const {
  static const ScanNetwork empty = {"", -127, false, false};
  if (index >= visibleScanCount) return empty;
  return scanNetworks[index];
}

uint32_t WifiManager::scanGeneration() const {
  return scanVersion;
}

void WifiManager::loadSavedNetworks() {
  savedNetworkCount = 0;

  Preferences preferences;
  if (!preferences.begin(WIFI_NAMESPACE, true)) {
    return;
  }

  uint8_t storedCount = preferences.getUChar("count", 0);
  if (storedCount > MAX_SAVED_NETWORKS) storedCount = MAX_SAVED_NETWORKS;

  for (uint8_t i = 0; i < storedCount; i++) {
    char ssidKey[8];
    char passwordKey[8];
    snprintf(ssidKey, sizeof(ssidKey), "ssid%u", i);
    snprintf(passwordKey, sizeof(passwordKey), "pass%u", i);

    String ssid = preferences.getString(ssidKey, "");
    if (ssid.isEmpty()) continue;

    savedNetworks[savedNetworkCount].ssid = ssid;
    savedNetworks[savedNetworkCount].password =
      preferences.getString(passwordKey, "");
    savedNetworkCount++;
  }

  preferences.end();
}

bool WifiManager::persistSavedNetworks() {
  Preferences preferences;
  if (!preferences.begin(WIFI_NAMESPACE, false)) {
    return false;
  }

  bool ok = preferences.putUChar("count", savedNetworkCount) > 0;

  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
    char ssidKey[8];
    char passwordKey[8];
    snprintf(ssidKey, sizeof(ssidKey), "ssid%u", i);
    snprintf(passwordKey, sizeof(passwordKey), "pass%u", i);

    if (i < savedNetworkCount) {
      ok &= preferences.putString(ssidKey, savedNetworks[i].ssid) > 0;
      size_t passwordResult =
        preferences.putString(passwordKey, savedNetworks[i].password);
      ok &= savedNetworks[i].password.isEmpty() || passwordResult > 0;
    } else {
      preferences.remove(ssidKey);
      preferences.remove(passwordKey);
    }
  }

  preferences.end();
  return ok;
}

int WifiManager::findSavedNetwork(const String &ssid) const {
  for (uint8_t i = 0; i < savedNetworkCount; i++) {
    if (savedNetworks[i].ssid == ssid) return i;
  }
  return -1;
}

void WifiManager::processScanResults(int resultCount) {
  visibleScanCount = 0;

  for (int i = 0; i < resultCount && visibleScanCount < MAX_SCAN_RESULTS; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;

    bool duplicate = false;
    for (uint8_t j = 0; j < visibleScanCount; j++) {
      if (scanNetworks[j].ssid == ssid) {
        if (WiFi.RSSI(i) > scanNetworks[j].rssi) {
          scanNetworks[j].rssi = WiFi.RSSI(i);
        }
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    ScanNetwork &network = scanNetworks[visibleScanCount++];
    network.ssid = ssid;
    network.rssi = WiFi.RSSI(i);
    network.secured = isSecured(WiFi.encryptionType(i));
    network.saved = isSaved(ssid);
  }

  for (uint8_t i = 0; i < visibleScanCount; i++) {
    for (uint8_t j = i + 1; j < visibleScanCount; j++) {
      if (scanNetworks[j].rssi > scanNetworks[i].rssi) {
        ScanNetwork temporary = scanNetworks[i];
        scanNetworks[i] = scanNetworks[j];
        scanNetworks[j] = temporary;
      }
    }
  }

  scanVersion++;
}

void WifiManager::buildCandidates() {
  candidateCount = 0;

  for (uint8_t scanIndex = 0; scanIndex < visibleScanCount; scanIndex++) {
    int savedIndex = findSavedNetwork(scanNetworks[scanIndex].ssid);
    if (savedIndex < 0 || candidateCount >= MAX_SAVED_NETWORKS) continue;

    candidates[candidateCount].savedIndex = static_cast<uint8_t>(savedIndex);
    candidates[candidateCount].rssi = scanNetworks[scanIndex].rssi;
    candidateCount++;
  }
}

void WifiManager::connectCurrentCandidate() {
  if (candidateIndex >= candidateCount) {
    retryAtMs = millis() + RETRY_DELAY_MS;
    setState(State::NO_KNOWN_NETWORK);
    return;
  }

  const SavedNetwork &network =
    savedNetworks[candidates[candidateIndex].savedIndex];

  WiFi.disconnect(false, false);
  WiFi.begin(network.ssid.c_str(), network.password.c_str());
  connectStartedMs = millis();
  setState(State::CONNECTING);

  Serial.printf("Intentando conectar a WiFi: %s\n", network.ssid.c_str());
}

void WifiManager::tryNextCandidate() {
  WiFi.disconnect(false, false);
  candidateIndex++;
  connectCurrentCandidate();
}

void WifiManager::setState(State newState) {
  currentState = newState;
}