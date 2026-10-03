#include "ReadingStatsActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "ReadingStats.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int kSummaryColumns = 4;
constexpr int kHeatmapColumns = 15;
constexpr int kHeatmapRows = 6;
constexpr int kHeatmapDays = kHeatmapColumns * kHeatmapRows;
constexpr int kHeatmapCellSize = 12;
constexpr int kHeatmapGap = 2;
constexpr int kTileHeight = 80;

void formatNumber(const uint32_t value, char* out, const size_t outSize) {
  char digits[16];
  std::snprintf(digits, sizeof(digits), "%lu", static_cast<unsigned long>(value));

  const char* src = digits;
  char* dst = out;
  size_t remaining = outSize;
  const size_t len = std::strlen(digits);
  const size_t firstGroup = len % 3 == 0 ? 3 : len % 3;

  for (size_t i = 0; i < len && remaining > 1; ++i) {
    *dst++ = src[i];
    --remaining;
    const bool isSeparatorPos =
        (i + 1) < len && ((i + 1 == firstGroup) || ((i + 1 > firstGroup) && ((i + 1 - firstGroup) % 3 == 0)));
    if (isSeparatorPos && remaining > 1) {
      *dst++ = ',';
      --remaining;
    }
  }

  *dst = '\0';
}

void formatDurationMinutes(const uint32_t totalMinutes, char* out, const size_t outSize) {
  const uint32_t hours = totalMinutes / 60U;
  const uint32_t minutes = totalMinutes % 60U;

  if (hours > 0U) {
    std::snprintf(out, outSize, tr(STR_STATS_DURATION_HM_FORMAT), static_cast<unsigned>(hours),
                  static_cast<unsigned>(minutes));
  } else {
    std::snprintf(out, outSize, tr(STR_STATS_DURATION_M_FORMAT), static_cast<unsigned>(minutes));
  }
}

void formatAverageSession(const ReadingStats& stats, char* out, const size_t outSize) {
  const uint32_t totalSeconds = stats.getTotalReadingSeconds();
  const uint32_t sessionCount = stats.getSessionCount();

  if (sessionCount == 0U) {
    formatDurationMinutes(0U, out, outSize);
    return;
  }

  const uint32_t averageSeconds = totalSeconds / sessionCount;
  formatDurationMinutes(averageSeconds / 60U, out, outSize);
}

void drawStatTile(GfxRenderer& renderer, int x, int y, int width, int height, const char* label, const char* value) {
  constexpr int kInnerPadding = 10;

  renderer.drawRect(x, y, width, height, true);
  renderer.drawText(UI_10_FONT_ID, x + kInnerPadding, y + kInnerPadding, label);

  const int valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  const int valueX = x + ((width - valueWidth) / 2);
  const int valueY = y + height / 2 + 2;
  renderer.drawText(UI_12_FONT_ID, valueX, valueY, value, true, EpdFontFamily::BOLD);
}

}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("READING_STATS", "Opening reading stats");
  requestUpdate();
}

void ReadingStatsActivity::onExit() {
  LOG_DBG("READING_STATS", "Closing reading stats");
  Activity::onExit();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    LOG_DBG("READING_STATS", "Back released, returning to previous screen");
    finish();
  }
}

void ReadingStatsActivity::render(RenderLock&&) { render(); }

void ReadingStatsActivity::render() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);

  const int contentLeft = marginLeft + metrics.contentSidePadding;
  const int contentRight = screenWidth - marginRight - metrics.contentSidePadding;
  const int contentWidth = contentRight - contentLeft;
  const int lineHeight10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int spacing = metrics.verticalSpacing;
  const int tileGap = spacing;
  const bool isLandscape = screenWidth >= screenHeight;
  const int summaryHeight = isLandscape ? (kTileHeight + spacing + 2) : (kTileHeight * 2 + tileGap + spacing);
  const bool hasEpoch = STATS.hasEpoch();

  const int streakHeight =
      hasEpoch ? (lineHeight10 + 10) : (lineHeight10 + renderer.getLineHeight(SMALL_FONT_ID) + spacing + 12);
  const int heatmapTitleHeight = lineHeight10 + 4;
  const int heatmapGridWidth = kHeatmapColumns * kHeatmapCellSize + (kHeatmapColumns - 1) * kHeatmapGap;
  const int heatmapGridHeight = kHeatmapRows * kHeatmapCellSize + (kHeatmapRows - 1) * kHeatmapGap;
  const int heatmapTop = marginTop + spacing;
  const int heatmapSectionTop = heatmapTop + summaryHeight + spacing + 2 + spacing + streakHeight + spacing;
  renderer.clearScreen();
  const int tileWidth =
      isLandscape ? (contentWidth - tileGap * (kSummaryColumns - 1)) / kSummaryColumns : (contentWidth - tileGap) / 2;

  char totalPages[24];
  char totalTime[24];
  char booksFinished[16];
  char averageSession[24];

  formatNumber(STATS.getTotalPagesRead(), totalPages, sizeof(totalPages));
  formatDurationMinutes(STATS.getTotalReadingSeconds() / 60U, totalTime, sizeof(totalTime));
  std::snprintf(booksFinished, sizeof(booksFinished), "%u", static_cast<unsigned int>(STATS.getTotalBooksFinished()));
  formatAverageSession(STATS, averageSession, sizeof(averageSession));

  if (isLandscape) {
    drawStatTile(renderer, contentLeft, heatmapTop, tileWidth, kTileHeight, tr(STR_STATS_TOTAL_PAGES), totalPages);
    drawStatTile(renderer, contentLeft + (tileWidth + tileGap), heatmapTop, tileWidth, kTileHeight,
                 tr(STR_STATS_READING_TIME), totalTime);
    drawStatTile(renderer, contentLeft + (tileWidth + tileGap) * 2, heatmapTop, tileWidth, kTileHeight,
                 tr(STR_STATS_BOOKS_FINISHED), booksFinished);
    drawStatTile(renderer, contentLeft + (tileWidth + tileGap) * 3, heatmapTop, tileWidth, kTileHeight,
                 tr(STR_STATS_AVG_SESSION), averageSession);
  } else {
    const int secondRowY = heatmapTop + kTileHeight + tileGap;
    drawStatTile(renderer, contentLeft, heatmapTop, tileWidth, kTileHeight, tr(STR_STATS_TOTAL_PAGES), totalPages);
    drawStatTile(renderer, contentLeft + tileWidth + tileGap, heatmapTop, tileWidth, kTileHeight,
                 tr(STR_STATS_READING_TIME), totalTime);
    drawStatTile(renderer, contentLeft, secondRowY, tileWidth, kTileHeight, tr(STR_STATS_BOOKS_FINISHED),
                 booksFinished);
    drawStatTile(renderer, contentLeft + tileWidth + tileGap, secondRowY, tileWidth, kTileHeight,
                 tr(STR_STATS_AVG_SESSION), averageSession);
  }

  const int summaryDividerY = heatmapTop + summaryHeight;
  renderer.drawLine(contentLeft, summaryDividerY, contentRight, summaryDividerY, true);

  char currentStreak[40];
  char bestStreak[24];
  std::snprintf(currentStreak, sizeof(currentStreak), tr(STR_STATS_CURRENT_STREAK_FORMAT),
                static_cast<unsigned int>(STATS.getCurrentStreakDays()));
  std::snprintf(bestStreak, sizeof(bestStreak), tr(STR_STATS_BEST_STREAK_FORMAT),
                static_cast<unsigned int>(STATS.getLongestStreakDays()));

  const int streakY = summaryDividerY + spacing;
  renderer.drawText(UI_10_FONT_ID, contentLeft, streakY, currentStreak, true, EpdFontFamily::BOLD);

  const int bestWidth = renderer.getTextWidth(UI_10_FONT_ID, bestStreak, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, contentRight - bestWidth, streakY, bestStreak, true, EpdFontFamily::BOLD);

  if (!hasEpoch) {
    renderer.drawText(SMALL_FONT_ID, contentLeft, streakY + lineHeight10 + spacing, tr(STR_STATS_NO_EPOCH), true,
                      EpdFontFamily::ITALIC);
  }

  renderer.drawText(UI_10_FONT_ID, contentLeft, heatmapSectionTop, tr(STR_STATS_READING_ACTIVITY), true,
                    EpdFontFamily::BOLD);

  const int gridX = contentLeft + ((contentWidth - heatmapGridWidth) / 2);
  const int gridY = heatmapSectionTop + heatmapTitleHeight + spacing;
  const auto& dailyLog = STATS.getDailyLog();
  const uint32_t todayIndex = STATS.todayIndex();
  const uint32_t anchorDayIndex = todayIndex != 0U ? todayIndex : (dailyLog.empty() ? 0U : dailyLog.front().dayIndex);

  for (int slot = 0; slot < kHeatmapDays; ++slot) {
    const int row = slot / kHeatmapColumns;
    const int col = slot % kHeatmapColumns;
    const int x = gridX + col * (kHeatmapCellSize + kHeatmapGap);
    const int y = gridY + row * (kHeatmapCellSize + kHeatmapGap);

    uint16_t pages = 0;
    if (!dailyLog.empty()) {
      const uint32_t dayOffset = static_cast<uint32_t>(kHeatmapDays - 1 - slot);
      if (anchorDayIndex >= dayOffset) {
        const uint32_t dayIndex = anchorDayIndex - dayOffset;
        for (const auto& entry : dailyLog) {
          if (entry.dayIndex == dayIndex) {
            pages = entry.pages;
            break;
          }
          if (entry.dayIndex < dayIndex) {
            break;
          }
        }
      }
    }

    if (pages > 0U) {
      renderer.fillRect(x, y, kHeatmapCellSize, kHeatmapCellSize, true);
    } else {
      renderer.drawRect(x, y, kHeatmapCellSize, kHeatmapCellSize, true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
