# Departures Board firmware instructions

## Change control

- Do not implement code, configuration, documentation, or generated-asset changes
  unless the user explicitly asks for implementation.
- Before any implementation, inspect the relevant code and present a concise
  plan covering the intended behavior, affected display targets, layout or
  message-flow changes, and validation steps.
- Wait for the user to confirm the plan before editing files, building, or
  uploading firmware.

## Display targets

This firmware supports two compile-time display targets:

- `esp32dev`: the original 256x64 SSD1322 OLED.
- `cyd`: the ESP32 Cheap Yellow Display (ILI9341) in 320x240 landscape mode.

Keep OLED behavior unchanged when making CYD-specific changes. Guard CYD-only
logic with `DISPLAY_CYD`; do not alter the shared display code unless the change
is required for both targets.

The CYD renderer is implemented by `U8G2_CYD_TFT` in `include/cydDisplay.h`.
It uses a full 320x240 U8g2 framebuffer (40x30 tiles). It supports targeted
partial tile updates (`updateDisplayArea`) for CYD tile bands (primary message
`y=104..127`, bottom ticker `y=168..199`, clock `y=200..239`, service panel
`y=64..175`), while automatically falling back to full `sendBuffer()` when called
with legacy OLED tile dimensions (`tw <= 32 && ty + th <= 8`).

## CYD layout and fonts

- Keep the logical CYD canvas at 320x240. Do not reintroduce the former
  304x136 canvas.
- The station title belongs at the top, with a 10 px top margin.
- The departure clock belongs at the bottom, with a 10 px bottom margin. Use
  explicit top positioning (`setFontPosTop()`) for the CYD clock; font descent
  arithmetic clipped the clock on hardware.
- On CYD, the background data-update icon (`"}"` / `→`) and Wi-Fi disconnect icon
  (`"\x7F"`) are positioned at the bottom left (`x=10, y=224`), vertically aligned
  with the clock digits and cleared via tile band `y=200..239` (`ty=25, th=5`).
- The clock width is anchored against `"88:88:88"` to prevent horizontal
  movement as its digits change.
- The full-screen NSE clock (`drawNSEclock`) is scaled and centered on CYD:
  50 px hours/minutes and 36 px bottom-aligned seconds centered horizontally
  (`x = 10..310`, 10 px margins) and vertically (`top = 95`, 95 px margins).
  Second-by-second updates clear `y = 93..147` via tile band `y = 88..151` (`ty = 11, th = 8`).
- CYD National Rail and Bus service and feed rows use the fixed-width, bold
  `u8g2_font_7x14B_tf` font at 1x scale.
- The primary CYD service uses two rows: scheduled time and destination first,
  then platform and expected/departure status. Keep this layout CYD-only; the
  OLED primary service remains a single row.
- Draw every non-empty `station.serviceMessage` immediately after the primary
  service at `y=108`. The secondary CYD service rows (`y=130` for
  time/destination and `y=152` for platform/status) cycle through upcoming
  departures (2nd, 3rd, 4th...) every 15 seconds via `cydSecondaryServiceIndex`.
  Reserve `y=174–193` for the single-line RSS/NRCC station-message ticker and
  `y=204` for the clock clear area. Render the primary and second services
  together as one static panel so redraws clear stale service pixels. The
  primary service message and RSS/NRCC ticker are separate 20 px clipped
  scrolling bands; do not apply per-row clipping to the static service rows.
- During the first CYD board render, ensure `u8g2.setFontPosBaseline()` is
  explicitly set at the start of `drawStationBoard()`, `drawBusDeparturesBoard()`,
  and `departureBoardLoop()`, because `setup()` leaves U8g2 in top-positioning mode
  (`setFontPosTop()`). Without this, the first frame renders all baseline coordinates
  as top coordinates, cutting off rows. Also initialize and draw both ticker bands
  and call `drawCurrentTime()` before the first `sendBuffer()`. Reset the detail font,
  text scale, and clip window before that first send.
- On CYD, route primary-service context (service message, calling points,
  origin/operator, seating, coach count) and station/NRCC notices to the
  primary message row. Reserve the bottom ticker for RSS, attribution, and weather only.
- Right-align CYD live-status text to the fixed right-column boundary at
  `SCREEN_WIDTH - 12` (`CYD_DETAIL_STATUS_RIGHT`) so its final glyph is not clipped by the physical edge.
- The default CYD palette/color scheme is Amber (`CYD_COLOR_AMBER`, `0xFD80` / `255, 176, 0`).
- Hardware input defaults in `writeDefaultConfig()` are scoped by display target:
  - CYD: `brightness = 200`, `touch = true` (supports onboard XPT2046 touchscreen on VSPI: CLK 25, MISO 39, MOSI 32, CS 33, IRQ 36 and active-LOW BOOT button on GPIO 0).
  - OLED: `brightness = 20`, `touch = false` (optional active-HIGH TTP223 sensor on GPIO 34).
- `displayedPrimaryServiceMessage` must remain sized to `MAXCALLINGSIZE + 12`
  (matching `cydPrimaryMessages` slot size) so that full calling-point strings
  can scroll without truncation.
- The 14 px detail glyphs must render with their baseline inside their row
  clipping rectangle. Use `railDetailBaseline()` and
  `railDetailScrollBaseline()` for National Rail and Bus service/feed rows rather than
  OLED-era `y - 1` baseline coordinates. Those earlier coordinates put the
  glyphs above CYD clip windows, leaving rows absent or partially cut off.

## CYD Bus Departures Board layout

- Header: Top station/stop header (`y = 0..63`) using `drawStationHeader(locationName, "", locationFilter, 0)`, clearing full width `SCREEN_WIDTH` (`y = 0..LINE1-1`).
- Departure rows: Up to 3 cleanly spaced service rows using `u8g2_font_7x14B_tf`:
  - Row 0: `y = 68`
  - Row 1: `y = 102`
  - Row 2: `y = 136` (rotates through 3rd, 4th, 5th... services every 10 seconds via `cydSecondaryServiceIndex` when `station.numServices > 3`).
- Route number: Drawn at `x = 0`. Destination begins at `busDestX` (calculated from widest route number in `7x14B` + padding).
- Live departure status (`Exp HH:MM` or scheduled time): Right-aligned to `CYD_DETAIL_STATUS_RIGHT` (`SCREEN_WIDTH - 12`).
- Bottom ticker: `y = 174..193` in tile band `y = 168..199` for `"Powered by bustimes.org"` attribution and weather messages.
- Clock: Use `drawCurrentTime()` (large centered `u8g2_font_logisoso20_tn` in tile band `y = 200..239`), NOT `drawCurrentTimeUG()`.
- Partial tile updates: `busDeparturesLoop()` updates only `CYD_TILE_SERVICE_PANEL_Y` (`y = 64..175`) on `UPD_NO_CHANGE`, tile band `0, 16, 40, 6` on 3rd service rotation, and `CYD_TILE_BOTTOM_TICKER_Y` (`y = 168..199`) during ticker scrolls.

## CYD Startup and Switch screens

- Startup splash screen:
  - Center logo horizontally: `logoX = (SCREEN_WIDTH - gadeclogo_width) / 2` (`113` on 320 px CYD).
  - Center logo vertically: `logoY = 80` (spans `y = 80..120`).
  - Use `bodyFont()` (`NatRailSmall9`) for the copyright notice below the logo at `logoY + gadeclogo_height + 16 = 136` to prevent text truncation.
  - Position Wi-Fi IP address below progress bar at `y = 110`.
- Mode switch and soft reset screens:
  - `departureBoardLoop()` leaves `u8g2.setTextScale(2)` active. Always explicitly reset `u8g2.setTextScale(1)` and set `u8g2.setFontPosTop()` in `showSwitchScreen()` and `softResetBoard()`.
  - In `showSwitchScreen()`, place mode title at `y = 80` and waiting message at `y = 115` to prevent text overlap.
  - In `softResetBoard()`, place `"Switching modes..."` at `y = 110` on CYD to avoid colliding with `progressBar()` at `y = 52`.

## Screenshot capture

- Endpoint `http://<ip>/screenshot.bmp` (and `/screenshot`) serves a 1-bit indexed
  BMP generated on the fly directly from `u8g2.getBufferPtr()` with zero heap allocations.
- Palette reflects the active target and color scheme (`cyd_fg_color` on CYD, amber on OLED).
  File size: 9,662 bytes on CYD (320x240), 2,110 bytes on OLED (256x64).
- Over USB Serial, entering `snap` or `screenshot` outputs the BMP as a hex stream.

## Builds and deployment

### Windows Console Encoding (UTF-8)

`esptool` upload output outputs unicode block characters for its progress bars (e.g. `[████░░░░]`).
On Windows, standard PowerShell defaults to code page 1252, causing Python `click` to crash with:
`UnicodeEncodeError: 'charmap' codec can't encode characters in position ...: character maps to <undefined>`.
Always configure UTF-8 encoding in PowerShell before running `platformio run`:

```powershell
$env:PYTHONIOENCODING = "utf-8"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001
```

### PlatformIO Core Directory

The shared PlatformIO core can be incompatible with the installed Python
version. Always use the **full absolute path** for the session-local PlatformIO core
(avoid relative `..\` path chains which can resolve outside the user directory):

```powershell
$env:PLATFORMIO_CORE_DIR = 'C:\Users\rober\.copilot\session-state\<session-id>\files\platformio-core'
```

### Building and Uploading

When validating a CYD change, first check whether the CYD is connected to a
serial port. If it is, build **and upload** to the attached device:

```powershell
$env:PYTHONIOENCODING = "utf-8"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001
$env:PLATFORMIO_CORE_DIR = 'C:\Users\rober\.copilot\session-state\<session-id>\files\platformio-core'
python -m platformio run -e cyd -t upload
```

If no CYD serial port is present, build without serial upload using
`python -m platformio run -e cyd`, then upload `.pio/build/cyd/firmware.bin`
via the board's HTTP `/update` endpoint. Discover reachable Departures Board devices using
`/info`, and confirm each CYD candidate is 320x240 via its public
`/screenshot.bmp` endpoint. If multiple eligible boards are found, show their
IP addresses and identifying details and **ask the user to choose one before
uploading**; never pick one automatically. If the target cannot be identified
unambiguously, do not upload. The endpoint accepts a multipart `update` field:

```powershell
curl.exe --basic --user admin --fail-with-body --show-error --form "update=@.pio/build/cyd/firmware.bin;filename=firmware.bin" "http://<chosen-ip>/update"
```

Use the configured web username if it differs from `admin`. Enter the admin
password only at the terminal's private prompt (or in the browser at `/update`),
never in chat or on the command line. Use HTTP Basic only on a trusted local
network. Verify the upload response, reboot and new firmware build timestamp
via `/info`, then complete the post-upload screenshot verification below.

Also build the OLED environment for changes that touch shared firmware paths:

```powershell
$env:PYTHONIOENCODING = "utf-8"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001
$env:PLATFORMIO_CORE_DIR = 'C:\Users\rober\.copilot\session-state\<session-id>\files\platformio-core'
python -m platformio run -e esp32dev
```

`TFT_eSPI` may warn that `TOUCH_CS` is undefined. This is expected because
TFT_eSPI touch support is not configured; it is not a build failure.

### Post-upload screenshot verification

After uploading firmware to the CYD hardware:
1. Wait 30 seconds for the board to reboot, re-associate with Wi-Fi, and complete its initial board draw.
2. Determine the device IP (e.g. from `arp -a` matching the device MAC or `/info`) and download the screenshot:
   ```powershell
   curl.exe -s http://<ip>/screenshot.bmp -o screenshots/verify.bmp
   python -c "from PIL import Image; Image.open('screenshots/verify.bmp').save('screenshots/verify.png')"
   ```
3. Inspect `screenshots/verify.png` with the `view` tool.
4. Visually compare the captured screenshot against the expected output for the requested changes (layout coordinates, row bounds, font metrics, ticker positions, clock placement) before concluding the task. Clean up temporary files in `screenshots/` after verification.

## Web assets

Source web assets are under `web\`. After changing one, regenerate the embedded
headers with:

```powershell
python scripts\generate_headers.py
```

The generated gzip headers can differ solely because of gzip timestamps. Do not
edit files under `include\webgui\` manually.
