# pampaks fork

This repository is `pampaks/crossagent`, a fork of
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).
It tracks upstream `develop` and carries a small set of fork-only features.

## Remotes

- `origin`: `git@github-pampaks:pampaks/crossagent.git` (SSH alias `github-pampaks`, pampaks account only)
- `upstream`: `https://github.com/crosspoint-reader/crosspoint-reader.git` (read-only)

Pull requests target `master` of `pampaks/crossagent`. With the GitHub CLI pass
`--repo pampaks/crossagent --base master`, otherwise it targets upstream.

## Syncing with upstream

Merge, never rebase or squash:

```bash
git fetch upstream
git switch -c chore/sync-upstream-<date> master
git merge --no-ff <upstream-commit>   # pin a commit, develop moves daily
```

- Resolve conflicts in upstream-owned files to upstream's side, then re-apply the fork delta below.
- Merge the sync PR with **Create a merge commit**. Squash or rebase drops upstream ancestry and every later sync re-conflicts.
- Never `git push --tags`: the local clone holds every upstream tag.
- After the merge run the post-sync checks below.

## Fork delta

New files (not in upstream):

- `src/ReadingStats.{h,cpp}`: reading statistics store (`PersistableStore`, `/.crosspoint/reading_stats.json`); counts each finished book once (a persisted ring of the last 64 finished-book hashes)
- `src/activities/home/ReadingStatsActivity.{h,cpp}`: stats screen (tiles, streaks, 90-day heatmap)
- `.envrc`: direnv activation of the uv-managed `.venv`
- `FORK.md`

Hooks into upstream files:

- `src/main.cpp`: `STATS.loadFromFile()` at boot
- `src/activities/reader/ReaderActivity.{h,cpp}`: session start/end, page turns, book finished, last-read progress (`getPagesLeft`)
- `src/activities/reader/EpubReaderActivity.{h,cpp}`, `XtcReaderActivity.{h,cpp}`: `getPagesLeft`; EPUB status bar "N min left" when the title is hidden
- `src/activities/ActivityManager.h`, `src/activities/home/HomeActivity.{h,cpp}`: Reading Stats entry, shown only on the classic list home when there is no OPDS/Plugins row (the cover-grid home has a fixed five-tab array and a sixth list row overlaps the button hints)
- `src/activities/settings/SettingsActivity.{h,cpp}`: Reading Stats entry under System
- `src/components/themes/lyra/LyraTheme.cpp`: time-left lines on the home book card

Branding, OTA and release:

- `lib/I18n/translations/english.yaml`: `STR_CROSSPOINT` is "pampaks", plus the `STR_READING_STATS` and `STR_STATS_*` keys (English only)
- `src/activities/network/CrossPointWebServerActivity.cpp`: access point SSID `pampaks-reader`
- `src/images/Logo120.h`: boot and sleep logo
- `src/network/OtaUpdater.cpp`, `platformio.ini`: `CROSSPOINT_RELEASE_REPO` selects the repo the OTA check reads (`pampaks/crossagent`); the build fails with `#error` if it is missing
- `platformio.ini`: `[crosspoint] version`

## Releases

1. Bump `[crosspoint] version` in `platformio.ini` (the fork is >= 2.0.0; keep it above every upstream release).
2. Tag the commit with the bare version, no `v` prefix: `git tag -a 2.0.0 -m "pampaks 2.0.0"` and `git push origin refs/tags/2.0.0`.
3. Publish a GitHub release from that tag. Upstream's `release.yml` runs on `release: published`, checks the tag against `platformio.ini`, builds the devices and uploads `crosspoint-<version>-<device>.bin`.
4. Do not rename the assets: the OTA check looks for `crosspoint-<tag>-<device>.bin` in the latest release of `CROSSPOINT_RELEASE_REPO`.

Devices that ran fork firmware before 2.0.0 check upstream releases for updates, so the first 2.0.0 install must be flashed over USB.

## Post-sync checks

```bash
grep -n 'CROSSPOINT_RELEASE_REPO' platformio.ini src/network/OtaUpdater.cpp
grep -n 'pampaks-reader' src/activities/network/CrossPointWebServerActivity.cpp
grep -n '^STR_CROSSPOINT: "pampaks"' lib/I18n/translations/english.yaml
git diff --name-status <upstream-commit> HEAD   # should list only the fork delta above
```

## Known limitations

- "Avg. Session" and the time-left estimates average a whole session, so idle time with the book open inflates them.
- The home card time-left lines are shown only in the Lyra theme and only for the last book read.
- The Home menu entry is hidden when an OPDS or Plugins row is present; Settings > Reading Stats is always available.
- The stats screen draws its tiles directly instead of through the GUI theme helpers.
- Only English has the new strings; other languages fall back to English, and `STR_CROSSPOINT` is still "CrossPoint" in them.
- Web UI titles, the Wi-Fi hostname, USB product strings and User-Agent strings still say "CrossPoint".
