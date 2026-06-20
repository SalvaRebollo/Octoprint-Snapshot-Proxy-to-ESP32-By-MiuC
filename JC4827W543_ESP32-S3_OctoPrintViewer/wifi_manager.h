#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WifiManager {
public:
  static constexpr uint8_t MAX_SAVED_NETWORKS = 5;
  static constexpr uint8_t MAX_SCAN_RESULTS = 20;

  enum class State : uint8_t {
    IDLE,
    SCANNING,
    CONNECTING,
    CONNECTED,
    NO_KNOWN_NETWORK,
    ERROR
  };

  struct ScanNetwork {
    String ssid;
    int32_t rssi;
    bool secured;
    bool saved;
  };

  void begin();
  void loop();

  bool startScan(bool connectAfterScan = false);
  void reconnect();
  bool saveAndConnect(const String &ssid, const String &password);
  bool connectSaved(const String &ssid);
  bool forgetNetwork(const String &ssid);

  State state() const;
  bool isConnecting() const;
  bool isConnected() const;
  bool consumeConnectionChanged();
  const char *statusText() const;

  uint8_t savedCount() const;
  bool isSaved(const String &ssid) const;
  uint8_t scanCount() const;
  const ScanNetwork &scanNetwork(uint8_t index) const;
  uint32_t scanGeneration() const;

private:
  struct SavedNetwork {
    String ssid;
    String password;
  };

  struct Candidate {
    uint8_t savedIndex;
    int32_t rssi;
  };

  SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
  ScanNetwork scanNetworks[MAX_SCAN_RESULTS];
  Candidate candidates[MAX_SAVED_NETWORKS];

  uint8_t savedNetworkCount = 0;
  uint8_t visibleScanCount = 0;
  uint8_t candidateCount = 0;
  uint8_t candidateIndex = 0;
  uint32_t scanVersion = 0;
  uint32_t connectStartedMs = 0;
  uint32_t retryAtMs = 0;
  State currentState = State::IDLE;
  bool connectionChanged = false;
  bool autoConnectAfterScan = false;
  bool connectedBeforeScan = false;

  void loadSavedNetworks();
  bool persistSavedNetworks();
  int findSavedNetwork(const String &ssid) const;
  void processScanResults(int resultCount);
  void buildCandidates();
  void connectCurrentCandidate();
  void tryNextCandidate();
  void setState(State newState);
};

extern WifiManager wifiManager;