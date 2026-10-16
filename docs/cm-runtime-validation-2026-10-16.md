# ConfigurationsManager consumer update validation (2026-10-16)

## Scope and build provenance

- Repository baseline: `ae8c35e3073518c3c1e5ed416bd17207afbc0152`.
- Work branch: `feature/cm-runtime-update`; no commit, push, or integration performed.
- SIL version: `4.3.2`.
- CM version: `4.6.0`, pinned to `554869da7efc98e0c1874e74bf0c78d4bcd1664a`.
- Resolved dependency Git HEAD was checked directly. Only generated `src/html_content.h` differs after the library WebUI build.
- The wrapper now builds the pinned package in place. It no longer copies C++/WebUI sources from an unrelated local checkout.
- Production `usb` and `ota` board/platform/framework/build flags/dependencies/scripts were compared and are identical. The same USB-tested binary is used for OTA; no separate rebuild is substituted.
- OLED and GUI/MQTT logging are disabled. BME280 remains enabled; normal UART Info logging and ROM/panic diagnostics remain available.
- The existing limiter algorithm and partition layout are unchanged.

## Build results

| Configuration | Program bytes | OTA slot | Result |
| --- | ---: | ---: | --- |
| New CM, full hardware, without LTO | 1,317,625 | 1,310,720 | Does not fit; never deployed |
| New CM, full hardware, LTO | 1,217,521 | 1,310,720 | Passed; COM7 only |
| Final lean profile, UART Info, BME280 | 1,200,113 | 1,310,720 | Passed |

Final static RAM: 61,424 / 327,680 bytes (18.7%). Final program-section headroom reported by PlatformIO: 110,607 bytes (8.4%). Actual OTA binary headroom: 1,310,720 - 1,206,688 = 104,032 bytes. Removing OLED and GUI/MQTT logging while retaining UART Info saves 17,408 program bytes against the full LTO profile.

The Arduino ESP32 build adds `-fno-lto`; the post script removes that flag and enables `-flto`. Compiler-aware archive tools handle LTO objects. No CM shared checkout was edited.

Final binary: `.pio/sil-4.3.2-cm-554869d.bin` (also `.pio/build/usb/firmware.bin`), 1,206,688 bytes.

SHA-256: `5c115cdf741142e8d9ab2994c26ebf6da648c7ddb6aa69e0791237b14a32c556`.

ELF symbol inspection found zero matches for `LoggingManager::GuiOutput`, `MQTTLogOutput`, `Adafruit_SSD1306`, or `Adafruit_GFX`; five BME280 matches and five `SerialOutput` matches remain. Build scripts parse successfully; `git diff --check` passes.

## Test-device safeguards

- Test device: COM7, existing static IP `192.168.2.126`.
- Complete 4 MiB flash backup: local ignored `.pio/com7-before-sil.bin`.
- Backup SHA-256: `baff4bf7c7511c2d93a51d1b9c3becd9fb5bdefdd8b8d24db36819a073bd0c75`.
- Backup and target partition tables match, including NVS at `0x9000`, size `0x5000`.
- No chip/NVS erase was issued. Network settings and the static IP were retained.
- Initial bench build disables MQTT and RS485 in memory before outputs start.
- Only the test-device MQTT-enable and RS485-enable settings were saved as false through the existing settings API and verified by GET before loading normal firmware.
- The test device remains intentionally output-isolated after validation.
- Initial bench smoke: 5/5 successful HTTP samples, one controlled boot, zero Guru Meditation or Backtrace markers.
- Full-feature LTO intermediate smoke: 19/19 successful HTTP samples over 180 seconds, one controlled boot, zero Guru Meditation or Backtrace markers. This intermediate binary is not the final production artifact.

## Final deployment evidence

- Final COM7 gate: 180 seconds, 19/19 successful HTTP samples, one controlled boot, zero Guru Meditation or Backtrace markers.
- Final test firmware remained at `192.168.2.126`, version `4.3.2`, with GUI logging false and MQTT/RS485 disabled.
- Free heap during this test: 151,552 bytes at the first sample and 150,576 at the last sample; uptime increased to 179,630 ms after the controlled reset.
- Normal UART Info startup messages were observed: reset reason, setup start, BME280 initialization and setup completion. This confirms that `CM_ENABLE_LOGGING=0` does not suppress the separate application LoggingManager output.
- Test HTTP `/`, `/live_pages.json` and `/runtime_meta.json` returned 200; Display settings are absent and Temp settings remain.
- Production OTA target was verified as `192.168.2.122`, previously SIL `4.3.0`.
- The guarded sender checked the binary SHA-256 and final COM7 evidence before upload. Sender exit code was 0; live `/appinfo` independently confirmed SIL `4.3.2` and GUI logging false afterward.
- Final production gate: 180 seconds, 19/19 successful HTTP samples; uptime increased monotonically from 25,659 to 205,597 ms with no observed reboot or loss of reachability.
- Production free heap: 145,632 bytes at the first sample, 141,676 at the last sample, minimum 140,056. MQTT remained enabled and connected in all samples, with one initial connection.
- Production `/`, `/live_pages.json` and `/runtime_meta.json` returned 200. `/appinfo` reported version `4.3.2` and GUI logging false; Display settings are absent and Temp settings remain.
- Limiter, Temp and RS485 setting values were compared with their pre-OTA snapshot: no changes. WiFi remained on `192.168.2.122`.
- Live BME280 readings were available (approximately 15.7 C, 45.7% humidity and 1016.5 hPa). The existing production limiter remained enabled; MQTT input and inverter demand runtime values were present. No synthetic input or manual control action was sent.
- Raw local evidence remains in ignored `.pio/lean-final-com7-serial.log`, `.pio/lean-final-com7-http.json`, `.pio/production-final-http.json`, `.pio/production-acceptance.json`, `.pio/sil-production-ota.log` and the corresponding build/upload logs.
- No production serial cable was attached; production acceptance establishes stable HTTP/runtime uptime, not direct absence of every possible serial error.

## Limitations

- COM7 has no verified BME280/inverter wiring; output isolation deliberately prevents live inverter actuation from the test device.
- The acceptance window is a bounded smoke test, not a long-duration soak or sensor calibration.
- No dedicated affected automated test suite or enabled GitHub Actions workflow is present; verification uses firmware builds, ELF inspection, serial capture, HTTP/runtime checks and post-update settings comparison.
- BME280 support is retained. The maintainer authorized removing it only if a future demonstrated memory limit requires that tradeoff.
