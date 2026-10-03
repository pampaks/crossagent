#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <vector>

class ReadingStats : public PersistableStore<ReadingStats> {
  ReadingStats();  // reserve(91) the daily log, nothing else allocates later
  friend class PersistableStore<ReadingStats>;

 public:
  struct DayEntry {
    uint32_t dayIndex;  // days since 1970-01-01 in LOCAL time
    uint16_t pages;
    uint16_t minutes;
  };

  static const char* getFilePath() { return "/.crosspoint/reading_stats.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Session lifecycle (called from ReaderActivity)
  void onSessionStart();
  void onPageTurn();
  void onBookFinished(uint32_t bookHash);  // recent nonzero hashes count once; 0 = unknown
  void onSessionEnd();                     // must be a no-op unless a session is active (safe to call twice)

  // Returns estimated minutes for pagesLeft: -1 = not enough data (fewer than 5 timed page turns),
  // 0 when pagesLeft <= 0, otherwise at least 1 (use lroundf).
  int estimateMinutesRemaining(int pagesLeft) const;

  // Last-read-book progress, used by the home screen card. bookHash identifies the book.
  void setLastProgress(uint32_t bookHash, int32_t chapterPagesLeft, int32_t bookPagesLeft);
  uint32_t getLastBookHash() const;
  int32_t getLastChapterPagesLeft() const;         // -1 = unknown
  int32_t getLastBookPagesLeft() const;            // -1 = unknown
  static uint32_t hashBookPath(const char* path);  // FNV-1a 32-bit over the bytes of path

  // Getters for the stats screen
  uint32_t getTotalPagesRead() const;
  uint32_t getTotalReadingSeconds() const;
  uint16_t getTotalBooksFinished() const;
  uint16_t getCurrentStreakDays() const;  // expires after a missed day when trusted time is available
  uint16_t getLongestStreakDays() const;
  uint32_t getSessionCount() const;                  // number of completed + active sessions ever
  bool hasEpoch() const;                             // true when trusted time is available right now
  static uint32_t todayIndex();                      // today's local day index, 0 when trusted time is unavailable
  const std::vector<DayEntry>& getDailyLog() const;  // up to 90 entries, newest dayIndex first

 private:
  static constexpr uint8_t FORMAT_VERSION = 2;
  static constexpr uint32_t MIN_REAL_DAY_INDEX = 10957;  // 2000-01-01
  static constexpr size_t MAX_DAILY_LOG_ENTRIES = 90;
  static constexpr size_t RESERVED_DAILY_LOG_ENTRIES = MAX_DAILY_LOG_ENTRIES + 1;
  static constexpr size_t MAX_FINISHED_BOOKS = 64;
  static constexpr uint32_t MIN_TIMED_PAGE_TURNS = 5;

  uint32_t totalPagesRead = 0;
  uint32_t totalReadingSeconds = 0;
  uint16_t totalBooksFinished = 0;
  uint16_t currentStreakDays = 0;
  uint16_t longestStreakDays = 0;
  uint32_t sessionCount = 0;
  uint32_t lastActiveDayIndex = 0;
  float avgSecondsPerPage = 0.0f;
  uint32_t lastBookHash = 0;
  int32_t lastChapterPagesLeft = -1;
  int32_t lastBookPagesLeft = -1;
  uint32_t finishedBookHashes[MAX_FINISHED_BOOKS] = {};
  uint8_t finishedBookCount = 0;
  uint8_t finishedBookNextIndex = 0;
  std::vector<DayEntry> dailyLog;  // max 90 entries, capacity 91

  // In-memory session state (not persisted)
  bool sessionActive = false;
  uint32_t sessionStartMs = 0;
  uint32_t sessionPageCount = 0;

  uint32_t currentDayIndex() const;
  void updateStreak(uint32_t dayIdx);
  DayEntry* findOrCreateDay(uint32_t dayIdx);
  void trimDailyLog();
};

#define STATS ReadingStats::getInstance()
