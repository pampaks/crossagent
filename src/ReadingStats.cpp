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
  doc["cpl"] = lastChapterPagesLeft;
  doc["bpl"] = lastBookPagesLeft;
  doc["lbh"] = lastBookHash;

  JsonArray finishedBooks = doc["fb"].to<JsonArray>();
  const size_t oldestIndex = finishedBookCount < MAX_FINISHED_BOOKS ? 0 : finishedBookNextIndex;
  for (size_t i = 0; i < finishedBookCount; i++) {
    finishedBooks.add(finishedBookHashes[(oldestIndex + i) % MAX_FINISHED_BOOKS]);
  }

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

  JsonArrayConst finishedBooks = doc["fb"].as<JsonArrayConst>();
  finishedBookCount = static_cast<uint8_t>(std::min<size_t>(finishedBooks.size(), MAX_FINISHED_BOOKS));
  for (size_t i = 0; i < finishedBookCount; i++) {
    finishedBookHashes[i] = finishedBooks[i] | static_cast<uint32_t>(0);
  }
  finishedBookNextIndex = finishedBookCount % MAX_FINISHED_BOOKS;

  const uint32_t version = doc["v"] | static_cast<uint32_t>(0);
  const bool legacyFormat = version < FORMAT_VERSION;
  if (legacyFormat) {
    currentStreakDays = 0;
    longestStreakDays = 0;
    lastActiveDayIndex = 0;
    requestResave();
  } else {
    currentStreakDays = doc["cs"] | static_cast<uint16_t>(0);
    longestStreakDays = doc["ls"] | static_cast<uint16_t>(0);
    lastActiveDayIndex = doc["lad"] | static_cast<uint32_t>(0);
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

  const uint32_t today = currentDayIndex();
  DayEntry* entry = findOrCreateDay(today);
  updateStreak(today);
  if (entry != nullptr && entry->pages < UINT16_MAX) {
    entry->pages++;
  }
}

void ReadingStats::onBookFinished(uint32_t bookHash) {
  if (bookHash != 0) {
    for (size_t i = 0; i < finishedBookCount; i++) {
      if (finishedBookHashes[i] == bookHash) {
        return;
      }
    }
  }
  if (totalBooksFinished < UINT16_MAX) {
    totalBooksFinished++;
  }
  if (bookHash != 0) {
    finishedBookHashes[finishedBookNextIndex] = bookHash;
    finishedBookNextIndex = (finishedBookNextIndex + 1) % MAX_FINISHED_BOOKS;
    if (finishedBookCount < MAX_FINISHED_BOOKS) {
      finishedBookCount++;
    }
  }
  saveToFile();
}

void ReadingStats::onSessionEnd() {
  if (!sessionActive) {
    return;
  }

  const uint32_t sessionSeconds = (static_cast<uint32_t>(millis()) - sessionStartMs) / 1000u;
  totalReadingSeconds += sessionSeconds;

  const uint32_t today = todayIndex();
  if (sessionPageCount > 0 && today != 0) {
    for (DayEntry& entry : dailyLog) {
      if (entry.dayIndex == today) {
        const uint32_t sessionMinutes = sessionSeconds / 60u;
        const uint32_t updatedMinutes = static_cast<uint32_t>(entry.minutes) + sessionMinutes;
        entry.minutes = static_cast<uint16_t>(std::min<uint32_t>(updatedMinutes, UINT16_MAX));
        break;
      }
    }
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

uint16_t ReadingStats::getCurrentStreakDays() const {
  const uint32_t today = todayIndex();
  if (today != 0 && lastActiveDayIndex != 0 && today > lastActiveDayIndex + 1) {
    return 0;
  }
  return currentStreakDays;
}

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

uint32_t ReadingStats::currentDayIndex() const { return todayIndex(); }

void ReadingStats::updateStreak(uint32_t dayIdx) {
  if (dayIdx == 0 || dayIdx == lastActiveDayIndex) {
    return;
  }

  if (lastActiveDayIndex != 0 && dayIdx == lastActiveDayIndex + 1) {
    if (currentStreakDays < UINT16_MAX) {
      currentStreakDays++;
    }
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
