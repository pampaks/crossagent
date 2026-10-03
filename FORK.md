# pampaks fork

This repository is `pampaks/crossagent`, a fork of
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).
It tracks upstream `develop` and carries a small set of fork-only features.

## Features

What this fork adds or changes compared to upstream.

**Reading statistics**
- A **Reading Stats** screen (Home menu, and Settings > Reading Stats) with total pages read, total reading time, books finished, average session length, the current and best reading streak, and a 90-day heatmap of the days you read.
- Streaks and the heatmap use real calendar days from the device clock. The X4 has no battery-backed clock, so connect to Wi-Fi once; until the clock is trusted the screen shows a hint and only the lifetime totals count.
- Each book is counted as finished once, even if you reopen it. Stats are stored in `/.crosspoint/reading_stats.json` on the SD card and survive clearing the reading cache. Files from fork versions before 2.0.0 are migrated: totals are kept, the old streaks and heatmap (which were session counters) are reset.

**Time left**
- With the status bar title hidden, the EPUB status bar shows "N min left" for the chapter.
- The Lyra home card shows chapter and book time-left lines under the author for the book you read last.
- Both come from your own average reading speed, which is learned when a reading session ends (at least five page turns). They do not appear before that.

**Home screen**
- Back on the home screen does nothing. Upstream reopens the most recent book on Back; this fork removes that. Continue Reading still opens it with Confirm.

**Identity and updates**
- The product name shown on boot and sleep is "pampaks", the file transfer Wi-Fi network is `pampaks-reader`, and the boot logo is the Straw Hat bitmap.
- The on-device update check reads releases from `pampaks/crossagent`, not upstream, so a fork device is never offered upstream firmware.

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
- `src/activities/home/HomeActivity.cpp`: also removes upstream's "Back on home opens the most recent book" (commit `63093e60`) and the "Resume" Back-button hint; restore both if upstream's behavior is wanted
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
grep -n 'Button::Back' src/activities/home/HomeActivity.cpp   # should find nothing: Back stays unhandled on home
git diff --name-status <upstream-commit> HEAD   # should list only the fork delta above
```

## Known limitations

- "Avg. Session" and the time-left estimates average a whole session, so idle time with the book open inflates them.
- The home card time-left lines are shown only in the Lyra theme and only for the last book read.
- The Home menu entry is hidden when an OPDS or Plugins row is present; Settings > Reading Stats is always available.
- The stats screen is always portrait, like every screen except the book reader (only the reader follows the Reading Orientation setting).
- The stats screen draws its tiles directly instead of through the GUI theme helpers.
- Only English has the new strings; other languages fall back to English, and `STR_CROSSPOINT` is still "CrossPoint" in them.
- Web UI titles, the Wi-Fi hostname, USB product strings and User-Agent strings still say "CrossPoint".

## Potential future work

**Faster page turns.** Researched 2026-10 and parked, not started. On the X4 (a 4.26" GDEQ0426T82 panel behind an SSD1677 controller over SPI) a typical page turn is about 680 ms: roughly 500 ms of panel waveform, about 125 ms of SPI transfers and housekeeping around it, and a few tens of milliseconds of rendering. For comparison, vendor figures for this panel class are about 0.4 s partial, 1.5 s fast and 3.5 s full refresh.

- *Why not 60 Hz.* The [Modos](https://modos.tech) Paper Monitor reaches 60 to 75 Hz with its open-source Caster controller ([Caster](https://github.com/Modos-Labs/Caster), [Glider](https://github.com/Modos-Labs/Glider)). That figure is how often the screen accepts new content, not how fast one pixel changes (a pixel still takes about 100 to 200 ms). It works because an FPGA drives the panel's raw gate and source signals, keeps separate waveform state and a timer for every pixel, and lets changed pixels restart mid-drive. The SSD1677 applies one waveform to the whole screen per update and the SDK has no per-pixel interface, so this is a hardware project (a different controller), not a firmware change. A single 48 KB frame over SPI at 10 MHz already takes 38.4 ms, longer than a 60 Hz frame.
- *What could apply (untested on this device; savings are the SDK authors' estimates).*
  - `-DFREEINK_X4_FAST_DU_SHORTCUT`: uses the controller's incremental `0x1C` fast path instead of the `0xFC` sequence. About 80 ms per refresh. Risk: ghosting or blotching over long sessions and across temperatures.
  - `-DFREEINK_X4_OVERCLOCK_SPI`: 40 MHz display SPI instead of 10 MHz. About 85 ms per page turn from the three plane transfers. Risk: signal integrity and the controller's own clock limit.
  - Skip the redundant black-and-white re-upload after a blocking fast refresh (about 38 ms), and use the SDK's rectangle update (`displayWindow`, byte-aligned) for small UI changes. The latter mainly saves SPI time; whether it shortens the waveform is unknown.
  - Custom waveforms (LUTs) can shorten drive time but risk ghosting, and an unbalanced waveform can damage the panel, so only with a validated table.
- *How to test.* Put the flags in a git-ignored `platformio.local.ini`, build `default`, flash, and read the `Wait complete: refresh (N ms)` lines from the serial log (`python3 scripts/debugging_monitor.py /dev/cu.usbmodemXXXX`). Judge ghosting over a long reading session at different temperatures before keeping any flag.

**Reading stats and UI**
- Let the stats screen follow the Reading Orientation setting (a fork-only behavior; the landscape layout code exists but nothing reaches it today).
- Make the average page time ignore idle gaps, for example with the dwell cap used by upstream's `ReaderSession`, so the time-left estimates stay realistic.
- Show the time-left lines in the other home themes, and find room for the Reading Stats row when an OPDS or Plugins row is present.
- Draw the stats tiles through the GUI theme helpers.

**Fork polish**
- Translate the new strings and finish the rebrand (web UI titles, mDNS name, Wi-Fi hostname, USB product strings, User-Agent strings, `STR_CROSSPOINT` in the other languages).
- Decide whether fork devices should keep using upstream's KOReader sync server and font downloads.
- Upstream's release workflow no longer attaches the bootloader, partition table, ELF and map files. Add them if recovery images from releases are wanted.
- A setting for "Back on home reopens the last book" instead of removing it outright.
