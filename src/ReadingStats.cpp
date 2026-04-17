#include "ReadingStats.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr char STATS_FILE_JSON[] = "/.crosspoint/reading_stats.json";
constexpr char STATS_TAG[] = "STATS";
constexpr size_t STATS_JSON_DOC_SIZE = 4096;
constexpr size_t MAX_DAILY_LOG_ENTRIES = 90;
}  // namespace

ReadingStats& ReadingStats::getInstance() {
  static ReadingStats instance;
  return instance;
}

void ReadingStats::onSessionStart() {
  sessionCount++;
  sessionStartMs = millis();
  sessionPageCount = 0;
}

void ReadingStats::onPageTurn() {
  sessionPageCount++;
  totalPagesRead++;

  DayEntry* entry = findOrCreateDay(currentDayIndex());
  if (entry != nullptr && entry->pages < UINT16_MAX) {
    entry->pages++;
  }
}

void ReadingStats::onBookFinished() { totalBooksFinished++; }

void ReadingStats::onSessionEnd() {
  const uint32_t sessionSeconds = (millis() - sessionStartMs) / 1000;
  totalReadingSeconds += sessionSeconds;

  DayEntry* entry = findOrCreateDay(currentDayIndex());
  if (entry != nullptr) {
    const uint32_t sessionMinutes = sessionSeconds / 60;
    const uint32_t updatedMinutes = static_cast<uint32_t>(entry->minutes) + sessionMinutes;
    entry->minutes = static_cast<uint16_t>(std::min<uint32_t>(updatedMinutes, UINT16_MAX));
  }

  if (sessionPageCount > 5) {
    const float sessionSecondsPerPage = static_cast<float>(sessionSeconds) / static_cast<float>(sessionPageCount);
    avgSecondsPerPage = (avgSecondsPerPage == 0.0f) ? sessionSecondsPerPage
                                                    : 0.7f * avgSecondsPerPage + 0.3f * sessionSecondsPerPage;
  }

  updateStreak(currentDayIndex());

  std::sort(dailyLog.begin(), dailyLog.end(),
            [](const DayEntry& lhs, const DayEntry& rhs) { return lhs.dayIndex > rhs.dayIndex; });
  if (dailyLog.size() > MAX_DAILY_LOG_ENTRIES) {
    dailyLog.resize(MAX_DAILY_LOG_ENTRIES);
  }

  saveToFile();
}

int ReadingStats::estimateMinutesRemaining(int pagesLeft) const {
  if (avgSecondsPerPage == 0.0f || totalPagesRead < 5) {
    return -1;
  }
  return static_cast<int>(static_cast<float>(pagesLeft) * avgSecondsPerPage / 60.0f);
}

void ReadingStats::setEpochFromUnixTime(uint32_t unixTimestamp) {
  if (epochUnixDay > 0) {
    return;
  }

  epochUnixDay = unixTimestamp / 86400;
  epochSessionCount = sessionCount;
}

uint32_t ReadingStats::getTotalPagesRead() const { return totalPagesRead; }

uint32_t ReadingStats::getTotalReadingSeconds() const { return totalReadingSeconds; }

uint16_t ReadingStats::getTotalBooksFinished() const { return totalBooksFinished; }

uint16_t ReadingStats::getCurrentStreakDays() const { return currentStreakDays; }

uint16_t ReadingStats::getLongestStreakDays() const { return longestStreakDays; }

bool ReadingStats::hasEpoch() const { return epochUnixDay > 0; }

const std::vector<ReadingStats::DayEntry>& ReadingStats::getDailyLog() const { return dailyLog; }

void ReadingStats::loadFromFile() {
  totalPagesRead = 0;
  totalReadingSeconds = 0;
  totalBooksFinished = 0;
  currentStreakDays = 0;
  longestStreakDays = 0;
  epochUnixDay = 0;
  epochSessionCount = 0;
  sessionCount = 0;
  lastActiveDayIndex = 0;
  avgSecondsPerPage = 0.0f;
  dailyLog.clear();

  if (!Storage.exists(STATS_FILE_JSON)) {
    LOG_DBG(STATS_TAG, "Stats file missing, using defaults");
    return;
  }

  String json = Storage.readFile(STATS_FILE_JSON);
  if (json.isEmpty()) {
    LOG_ERR(STATS_TAG, "Failed to read %s", STATS_FILE_JSON);
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR(STATS_TAG, "JSON parse error: %s", error.c_str());
    return;
  }

  totalPagesRead = doc["tp"] | static_cast<uint32_t>(0);
  totalReadingSeconds = doc["ts"] | static_cast<uint32_t>(0);
  totalBooksFinished = doc["bf"] | static_cast<uint16_t>(0);
  currentStreakDays = doc["cs"] | static_cast<uint16_t>(0);
  longestStreakDays = doc["ls"] | static_cast<uint16_t>(0);
  epochUnixDay = doc["ed"] | static_cast<uint32_t>(0);
  epochSessionCount = doc["esc"] | static_cast<uint32_t>(0);
  sessionCount = doc["sc"] | static_cast<uint32_t>(0);
  lastActiveDayIndex = doc["lad"] | static_cast<uint32_t>(0);
  avgSecondsPerPage = doc["asp"] | 0.0f;

  JsonArrayConst logArray = doc["log"];
  if (!logArray.isNull()) {
    for (JsonObjectConst dayObj : logArray) {
      if (dailyLog.size() >= MAX_DAILY_LOG_ENTRIES) {
        break;
      }

      dailyLog.push_back(DayEntry{
          dayObj["d"] | static_cast<uint32_t>(0),
          dayObj["p"] | static_cast<uint16_t>(0),
          dayObj["m"] | static_cast<uint16_t>(0),
      });
    }
  }

  std::sort(dailyLog.begin(), dailyLog.end(),
            [](const DayEntry& lhs, const DayEntry& rhs) { return lhs.dayIndex > rhs.dayIndex; });
}

void ReadingStats::saveToFile() {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["tp"] = totalPagesRead;
  doc["ts"] = totalReadingSeconds;
  doc["bf"] = totalBooksFinished;
  doc["cs"] = currentStreakDays;
  doc["ls"] = longestStreakDays;
  doc["ed"] = epochUnixDay;
  doc["esc"] = epochSessionCount;
  doc["sc"] = sessionCount;
  doc["lad"] = lastActiveDayIndex;
  doc["asp"] = avgSecondsPerPage;

  JsonArray logArray = doc["log"].to<JsonArray>();
  for (const DayEntry& entry : dailyLog) {
    JsonObject dayObj = logArray.add<JsonObject>();
    dayObj["d"] = entry.dayIndex;
    dayObj["p"] = entry.pages;
    dayObj["m"] = entry.minutes;
  }

  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(STATS_FILE_JSON, json)) {
    LOG_ERR(STATS_TAG, "Failed to write %s", STATS_FILE_JSON);
  }
}

uint32_t ReadingStats::currentDayIndex() const {
  if (epochUnixDay > 0) {
    return epochUnixDay + (sessionCount - epochSessionCount);
  }
  return sessionCount;
}

void ReadingStats::updateStreak(uint32_t dayIdx) {
  if (dayIdx == lastActiveDayIndex) {
    return;
  }

  if (lastActiveDayIndex != 0 && dayIdx == lastActiveDayIndex + 1) {
    currentStreakDays++;
    if (currentStreakDays > longestStreakDays) {
      longestStreakDays = currentStreakDays;
    }
  } else {
    currentStreakDays = 1;
    if (longestStreakDays < currentStreakDays) {
      longestStreakDays = currentStreakDays;
    }
  }

  lastActiveDayIndex = dayIdx;
}

ReadingStats::DayEntry* ReadingStats::findOrCreateDay(uint32_t dayIdx) {
  for (DayEntry& entry : dailyLog) {
    if (entry.dayIndex == dayIdx) {
      return &entry;
    }
  }

  dailyLog.push_back(DayEntry{dayIdx, 0, 0});
  return &dailyLog.back();
}
