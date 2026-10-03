#include "ReadingStats.h"

#include <Arduino.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

namespace {
constexpr char STATS_TAG[] = "STATS";
constexpr uint32_t FNV1A_OFFSET = 2166136261u;
constexpr uint32_t FNV1A_PRIME = 16777619u;

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const int adjustedMonth = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned doy = (153u * static_cast<unsigned>(adjustedMonth) + 2u) / 5u + day - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool newerDayFirst(const ReadingStats::DayEntry& lhs, const ReadingStats::DayEntry& rhs) {
  return lhs.dayIndex > rhs.dayIndex;
}
}  // namespace

ReadingStats::ReadingStats() { dailyLog.reserve(RESERVED_DAILY_LOG_ENTRIES); }

void ReadingStats::toJson(JsonDocument& doc) const {
  doc["v"] = FORMAT_VERSION;
  doc["tp"] = totalPagesRead;
  doc["ts"] = totalReadingSeconds;
  doc["bf"] = totalBooksFinished;
  doc["cs"] = currentStreakDays;
  doc["ls"] = longestStreakDays;
  doc["sc"] = sessionCount;
  doc["lad"] = lastActiveDayIndex;
  doc["asp"] = avgSecondsPerPage;
  doc["lsed"] = lastSessionEndUnixDay;
  doc["cpl"] = lastChapterPagesLeft;
  doc["bpl"] = lastBookPagesLeft;
  doc["lbh"] = lastBookHash;

  JsonArray logArray = doc["log"].to<JsonArray>();
  for (const DayEntry& entry : dailyLog) {
    JsonObject dayObj = logArray.add<JsonObject>();
    dayObj["d"] = entry.dayIndex;
    dayObj["p"] = entry.pages;
    dayObj["m"] = entry.minutes;
  }
}

bool ReadingStats::fromJson(JsonVariantConst doc) {
  totalPagesRead = doc["tp"] | static_cast<uint32_t>(0);
  totalReadingSeconds = doc["ts"] | static_cast<uint32_t>(0);
  totalBooksFinished = doc["bf"] | static_cast<uint16_t>(0);
  sessionCount = doc["sc"] | static_cast<uint32_t>(0);
  avgSecondsPerPage = doc["asp"] | 0.0f;
  lastBookHash = doc["lbh"] | static_cast<uint32_t>(0);
  lastChapterPagesLeft = doc["cpl"] | static_cast<int32_t>(-1);
  lastBookPagesLeft = doc["bpl"] | static_cast<int32_t>(-1);

  const uint32_t version = doc["v"] | static_cast<uint32_t>(0);
  const bool legacyFormat = version < FORMAT_VERSION;
  if (legacyFormat) {
    currentStreakDays = 0;
    longestStreakDays = 0;
    lastActiveDayIndex = 0;
    lastSessionEndUnixDay = 0;
    requestResave();
  } else {
    currentStreakDays = doc["cs"] | static_cast<uint16_t>(0);
    longestStreakDays = doc["ls"] | static_cast<uint16_t>(0);
    lastActiveDayIndex = doc["lad"] | static_cast<uint32_t>(0);
    lastSessionEndUnixDay = doc["lsed"] | static_cast<uint32_t>(0);
  }

  dailyLog.clear();
  JsonArrayConst logArray = doc["log"].as<JsonArrayConst>();
  for (JsonObjectConst dayObj : logArray) {
    const uint32_t dayIndex = dayObj["d"] | static_cast<uint32_t>(0);
    if (dayIndex == 0 || (legacyFormat && dayIndex < MIN_REAL_DAY_INDEX)) {
      continue;
    }
    if (dailyLog.size() >= MAX_DAILY_LOG_ENTRIES) {
      break;
    }
    dailyLog.push_back(DayEntry{
        dayIndex,
        dayObj["p"] | static_cast<uint16_t>(0),
        dayObj["m"] | static_cast<uint16_t>(0),
    });
  }
  std::sort(dailyLog.begin(), dailyLog.end(), newerDayFirst);
  trimDailyLog();

  sessionActive = false;
  sessionStartMs = 0;
  sessionPageCount = 0;
  if (legacyFormat) {
    LOG_DBG(STATS_TAG, "Migrated legacy reading stats");
  }
  return true;
}

void ReadingStats::onSessionStart() {
  sessionCount++;
  sessionStartMs = static_cast<uint32_t>(millis());
  sessionPageCount = 0;
  sessionActive = true;
}

void ReadingStats::onPageTurn() {
  if (sessionActive && sessionPageCount < UINT32_MAX) {
    sessionPageCount++;
  }
  totalPagesRead++;

  DayEntry* entry = findOrCreateDay(currentDayIndex());
  if (entry != nullptr && entry->pages < UINT16_MAX) {
    entry->pages++;
  }
}

void ReadingStats::onBookFinished() {
  totalBooksFinished++;
  saveToFile();
}

void ReadingStats::onSessionEnd() {
  if (!sessionActive) {
    return;
  }

  const uint32_t sessionSeconds = (static_cast<uint32_t>(millis()) - sessionStartMs) / 1000u;
  totalReadingSeconds += sessionSeconds;

  const uint32_t trustedDay = todayIndex();
  if (trustedDay != 0) {
    lastSessionEndUnixDay = trustedDay;
  }
  const uint32_t dayIndex = trustedDay != 0 ? trustedDay : lastSessionEndUnixDay;

  if (dayIndex != 0) {
    DayEntry* entry = findOrCreateDay(dayIndex);
    if (entry != nullptr) {
      const uint32_t sessionMinutes = sessionSeconds / 60u;
      const uint32_t updatedMinutes = static_cast<uint32_t>(entry->minutes) + sessionMinutes;
      entry->minutes = static_cast<uint16_t>(std::min<uint32_t>(updatedMinutes, UINT16_MAX));
    }

    updateStreak(dayIndex);
  }

  if (sessionPageCount >= MIN_TIMED_PAGE_TURNS && sessionSeconds > 0) {
    const float sessionSecondsPerPage = static_cast<float>(sessionSeconds) / static_cast<float>(sessionPageCount);
    avgSecondsPerPage =
        (avgSecondsPerPage == 0.0f) ? sessionSecondsPerPage : 0.7f * avgSecondsPerPage + 0.3f * sessionSecondsPerPage;
  }

  trimDailyLog();
  sessionActive = false;
  sessionStartMs = 0;
  sessionPageCount = 0;
  saveToFile();
}

int ReadingStats::estimateMinutesRemaining(int pagesLeft) const {
  if (avgSecondsPerPage == 0.0f || totalPagesRead < MIN_TIMED_PAGE_TURNS) {
    return -1;
  }
  if (pagesLeft <= 0) {
    return 0;
  }

  const int rounded = static_cast<int>(lroundf(static_cast<float>(pagesLeft) * avgSecondsPerPage / 60.0f));
  return rounded < 1 ? 1 : rounded;
}

void ReadingStats::setLastProgress(uint32_t bookHash, int32_t chapterPagesLeft, int32_t bookPagesLeft) {
  lastBookHash = bookHash;
  lastChapterPagesLeft = chapterPagesLeft;
  lastBookPagesLeft = bookPagesLeft;
}

uint32_t ReadingStats::getLastBookHash() const { return lastBookHash; }

int32_t ReadingStats::getLastChapterPagesLeft() const { return lastChapterPagesLeft; }

int32_t ReadingStats::getLastBookPagesLeft() const { return lastBookPagesLeft; }

uint32_t ReadingStats::hashBookPath(const char* path) {
  if (path == nullptr) {
    return 0;
  }

  uint32_t hash = FNV1A_OFFSET;
  while (*path != '\0') {
    hash ^= static_cast<uint8_t>(*path);
    hash *= FNV1A_PRIME;
    path++;
  }
  return hash;
}

uint32_t ReadingStats::getTotalPagesRead() const { return totalPagesRead; }

uint32_t ReadingStats::getTotalReadingSeconds() const { return totalReadingSeconds; }

uint16_t ReadingStats::getTotalBooksFinished() const { return totalBooksFinished; }

uint16_t ReadingStats::getCurrentStreakDays() const { return currentStreakDays; }

uint16_t ReadingStats::getLongestStreakDays() const { return longestStreakDays; }

uint32_t ReadingStats::getSessionCount() const { return sessionCount; }

bool ReadingStats::hasEpoch() const { return todayIndex() != 0; }

uint32_t ReadingStats::todayIndex() {
  const int64_t trustedNow = trustedtime::trustedNow();
  if (trustedNow <= 0) {
    return 0;
  }

  const time_t now = static_cast<time_t>(trustedNow);
  struct tm local = {};
  if (localtime_r(&now, &local) == nullptr) {
    return 0;
  }

  const int64_t days = daysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                                     static_cast<unsigned>(local.tm_mday));
  if (days <= 0 || days > std::numeric_limits<uint32_t>::max()) {
    return 0;
  }
  return static_cast<uint32_t>(days);
}

const std::vector<ReadingStats::DayEntry>& ReadingStats::getDailyLog() const { return dailyLog; }

uint32_t ReadingStats::currentDayIndex() const {
  const uint32_t trustedDay = todayIndex();
  if (trustedDay != 0) {
    return trustedDay;
  }
  return lastSessionEndUnixDay;
}

void ReadingStats::updateStreak(uint32_t dayIdx) {
  if (dayIdx == 0 || dayIdx == lastActiveDayIndex) {
    return;
  }

  if (lastActiveDayIndex != 0 && dayIdx == lastActiveDayIndex + 1) {
    currentStreakDays++;
  } else {
    currentStreakDays = 1;
  }

  if (currentStreakDays > longestStreakDays) {
    longestStreakDays = currentStreakDays;
  }
  lastActiveDayIndex = dayIdx;
}

ReadingStats::DayEntry* ReadingStats::findOrCreateDay(uint32_t dayIdx) {
  if (dayIdx == 0) {
    return nullptr;
  }

  for (DayEntry& entry : dailyLog) {
    if (entry.dayIndex == dayIdx) {
      return &entry;
    }
  }

  auto insertPos = std::find_if(dailyLog.begin(), dailyLog.end(),
                                [dayIdx](const DayEntry& entry) { return entry.dayIndex < dayIdx; });
  const size_t insertIndex = static_cast<size_t>(insertPos - dailyLog.begin());
  dailyLog.insert(insertPos, DayEntry{dayIdx, 0, 0});
  trimDailyLog();
  return insertIndex < dailyLog.size() ? &dailyLog[insertIndex] : nullptr;
}

void ReadingStats::trimDailyLog() {
  if (dailyLog.size() > MAX_DAILY_LOG_ENTRIES) {
    dailyLog.resize(MAX_DAILY_LOG_ENTRIES);
  }
}
