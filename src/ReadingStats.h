#pragma once
#include <cstdint>
#include <vector>

#define STATS ReadingStats::getInstance()

class ReadingStats {
 public:
  struct DayEntry {
    uint32_t dayIndex;  // days since epochUnixDay (or sessionCount if no NTP)
    uint16_t pages;
    uint16_t minutes;
  };

  static ReadingStats& getInstance();

  // Called from reader activities
  void onSessionStart();
  void onPageTurn();
  void onBookFinished();
  void onSessionEnd();

  // Returns estimated minutes remaining given pages left; -1 if < 5 page turns of data
  int estimateMinutesRemaining(int pagesLeft) const;

  // Called once when NTP provides a unix timestamp (e.g. from KOReader sync WiFi)
  void setEpochFromUnixTime(uint32_t unixTimestamp);

  // Getters for display
  uint32_t getTotalPagesRead() const;
  uint32_t getTotalReadingSeconds() const;
  uint16_t getTotalBooksFinished() const;
  uint16_t getCurrentStreakDays() const;
  uint16_t getLongestStreakDays() const;
  bool hasEpoch() const;

  // Returns up to 90 entries, sorted newest dayIndex first
  const std::vector<DayEntry>& getDailyLog() const;

  void loadFromFile();
  void saveToFile();

 private:
  ReadingStats() = default;

  uint32_t totalPagesRead = 0;
  uint32_t totalReadingSeconds = 0;
  uint16_t totalBooksFinished = 0;
  uint16_t currentStreakDays = 0;
  uint16_t longestStreakDays = 0;
  uint32_t epochUnixDay = 0;       // unix days at NTP sync; 0 = unknown
  uint32_t epochSessionCount = 0;  // sessionCount value when epoch was set
  uint32_t sessionCount = 0;       // monotonic session counter (increments each onSessionStart)
  uint32_t lastActiveDayIndex = 0;
  float avgSecondsPerPage = 0.0f;
  std::vector<DayEntry> dailyLog;  // max 90 entries

  // In-memory session state (not persisted)
  uint32_t sessionStartMs = 0;
  uint32_t sessionPageCount = 0;

  uint32_t currentDayIndex() const;
  void updateStreak(uint32_t dayIdx);
  DayEntry* findOrCreateDay(uint32_t dayIdx);
};
