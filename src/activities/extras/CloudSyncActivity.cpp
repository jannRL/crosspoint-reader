#include "CloudSyncActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

static constexpr const char* URL_PATH = "/cloud/url.txt";

void CloudSyncActivity::onEnter() {
  Activity::onEnter();
  newBooks = 0;
  todoSynced = false;
  readServerUrl();
}

void CloudSyncActivity::onExit() {
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
  }
  Activity::onExit();
}

void CloudSyncActivity::readServerUrl() {
  serverUrl = Storage.readFile(URL_PATH).c_str();
  // Trim whitespace/newlines
  while (!serverUrl.empty() && (serverUrl.back() == '\n' || serverUrl.back() == '\r' || serverUrl.back() == ' ')) {
    serverUrl.pop_back();
  }
  // Remove trailing slash
  while (!serverUrl.empty() && serverUrl.back() == '/') {
    serverUrl.pop_back();
  }

  if (serverUrl.empty()) {
    state = State::NO_URL;
    statusMessage = tr(STR_CLOUD_NO_URL);
    requestUpdate();
    return;
  }

  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CLOUD_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void CloudSyncActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    onWifiReady();
    return;
  }
  launchWifiSelection();
}

void CloudSyncActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             onWifiReady();
                           } else {
                             state = State::ERROR;
                             statusMessage = tr(STR_CLOUD_WIFI_FAILED);
                             requestUpdate();
                           }
                         });
}

void CloudSyncActivity::onWifiReady() {
  syncBooks();
  syncTodo();
  state = State::DONE;

  char buf[64];
  snprintf(buf, sizeof(buf), "%s  |  %d %s  |  %s", tr(STR_CLOUD_DONE), newBooks, tr(STR_CLOUD_BOOKS_NEW),
           todoSynced ? tr(STR_CLOUD_TODO_OK) : "");
  statusMessage = buf;
  requestUpdate();
}

void CloudSyncActivity::syncBooks() {
  state = State::SYNCING_BOOKS;
  statusMessage = tr(STR_CLOUD_SYNCING_BOOKS);
  requestUpdate(true);

  std::string listJson;
  const std::string listUrl = serverUrl + "/api/books";
  if (!HttpDownloader::fetchUrl(listUrl, listJson)) {
    LOG_ERR("CLOUD", "Failed to fetch book list from %s", listUrl.c_str());
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, listJson) != DeserializationError::Ok) {
    LOG_ERR("CLOUD", "Failed to parse book list JSON");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (!arr) return;

  for (JsonVariant v : arr) {
    const char* filename = v.as<const char*>();
    if (!filename) continue;

    const std::string destPath = std::string("/") + filename;
    if (Storage.exists(destPath.c_str())) continue;

    const std::string bookUrl = serverUrl + "/api/books/" + filename;
    LOG_INF("CLOUD", "Downloading: %s", filename);
    const auto result = HttpDownloader::downloadToFile(bookUrl, destPath);
    if (result == HttpDownloader::OK) {
      clearBookCache(destPath);
      newBooks++;
    } else {
      LOG_ERR("CLOUD", "Failed to download %s", filename);
    }
  }
}

void CloudSyncActivity::syncTodo() {
  state = State::SYNCING_TODO;
  statusMessage = tr(STR_CLOUD_SYNCING_TODO);
  requestUpdate(true);

  std::string todoJson;
  const std::string todoUrl = serverUrl + "/api/todo";
  if (!HttpDownloader::fetchUrl(todoUrl, todoJson)) {
    LOG_ERR("CLOUD", "Failed to fetch todo from %s", todoUrl.c_str());
    return;
  }

  Storage.mkdir("/todo", true);
  if (Storage.writeFile("/todo/list.json", String(todoJson.c_str()))) {
    todoSynced = true;
  } else {
    LOG_ERR("CLOUD", "Failed to write /todo/list.json");
  }
}

void CloudSyncActivity::loop() {
  if (state == State::WIFI_SELECTION) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onGoHome();
  }
}

void CloudSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto w = renderer.getScreenWidth();
  const auto h = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, tr(STR_CLOUD_SYNC));

  const int cy = metrics.topPadding + metrics.headerHeight + (h - metrics.topPadding - metrics.headerHeight) / 2 - 20;
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, cy, statusMessage.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
