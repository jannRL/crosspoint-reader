#pragma once
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class CloudSyncActivity final : public Activity {
  enum class State { CHECK_WIFI, WIFI_SELECTION, SYNCING_BOOKS, SYNCING_TODO, DONE, ERROR, NO_URL };

  State state = State::CHECK_WIFI;
  std::string statusMessage;
  std::string serverUrl;
  int newBooks = 0;
  bool todoSynced = false;

  void readServerUrl();
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiReady();
  void syncBooks();
  void syncTodo();

 public:
  explicit CloudSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CloudSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
