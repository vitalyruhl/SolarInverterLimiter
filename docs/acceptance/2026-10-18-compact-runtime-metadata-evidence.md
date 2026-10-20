# Historical implementation evidence

This is a preserved completed/historical plan and acceptance ledger, not active work instructions. The only current work queue is the [SIL / SBT / HA integration plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md). Historical permissions and pending checkboxes below do not override its latest owner instructions.

---

# SIL compact runtime metadata and BLE-free test-ESP handoff

Date: 2026-10-18. Initial status: implementation authorized, validation pending.

## Portable context and authority

The owner paused SBT panic diagnosis and explicitly requested compact SIL fields
where sensible, then installation on the COM7 test ESP without BLE for comparison
with SBT. This supersedes historical static-only SIL restrictions for this narrow
implementation/build/upload. Parent owns hardware operations and the owner owns
subsequent behavior acceptance. No commit, push, production OTA, inverter command,
network scan or additional NVS checking is authorized here.

Repository C:/Daten/_Codding/ESP32/SolarInverterLimiter, initially clean branch
feature/battery-energy-control at 91ca14c1b818ad5fd8a48d429360ab5242a5217a.
Read root AGENTS.md, origin/main:.github/AGENTS.md, refactor role and docs role.
The owner prohibits automatic final commits, overriding generic session-close
instructions. CM and SBT source are outside this agent's editing scope.

## Design and scope

- setupGUI has nineteen enabled fields: four BME280 floats and fifteen limiter
  diagnostics. Replace large live builders with compact metadata while preserving
  each existing getter, one-field provider name/order, keys, routing, labels,
  units and precision. Explicit layout creation retains page/card/group order.
- SbtService has forty fields: eleven health/control diagnostics and twenty-nine
  receiver channels. Register compact Float/Int/Bool/Text records based on the
  existing Kind contract; retain current precision and null behavior, including
  numeric presentation for unsupported channels. Receiver/provider/MQTT callbacks
  and lock behavior remain unchanged.
- Optional relay controls remain on the existing live builders because their
  getter/setter/initial-state/alarm registration is a separate dormant feature.
  Fan and heater default to zero; no optional relay metadata is instantiated in
  this test profile. No styles or sparse alarms exist on the migrated fields.
- Introduce a small production registration helper, with no full legacy records,
  to attach compact descriptors and the same individual value providers.
- Bump fallback VERSION 5.0.1 to 5.0.2 after scanning declarations. No VERSION
  override was found in local secrets; do not print or modify credentials.
- Keep controller algorithms, MQTT payloads, RS485 callbacks, settings keys and
  NVS layout unchanged. Only for SIL_MOCK_TRANSPORT, omit the startup overwrite
  that otherwise replaces a stored OTA password with a local secret. Production
  behavior remains unchanged; no credential seeding in the test exception.

## Isolated build and safety

Use an ignored project-local .pio/compact-runtime-metadata task configuration
based on env:testesp: SIL_MOCK_TRANSPORT=1 retains the existing mocked RS485
boundary, avoiding actual inverter TX. OLED/GUI/MQTT logging stay disabled and
BME280 stays enabled. Align CM logging/verbose to SBT's zero flags for comparison.
WiFi/MQTT and ordinary controller activity remain enabled.

Bind adjacent CM 4.7.0 with an explicit symlink and read-only preflight instead
of tools/precompile_wrapper.py, which otherwise rebuilds dependency WebUI. Do
not alter tracked dependency pins or pretend an unpublished CM release exists.
No CM_ENABLE_BLUETOOTH feature macro exists: BLE is opt-in through the
BluetoothManager header and NimBLE dependency. Prove its exclusion from the
resolved dependency graph, source includes and final ELF symbols.

The test ESP's known active slot is app0 at 0x10000 with capacity 0x1c0000. Use
an ignored matching build partition CSV; parent uploads the application image
only. The repository partitions.csv differs and must not be flashed. Do not read
NVS again. The known valid persisted WiFi/IP remains authoritative at
http://192.168.2.126/. Existing missing-only application defaults may initialize
new app settings; no promise of whole-NVS byte equality is made.

## Validation and acceptance

- Test production compact registration helpers and the forty actual SBT receiver
  descriptors with a clearly marked mocked sink plus real CM types/ArduinoJson.
  Check identities/routing/kinds/units/precision, provider values and failures.
- Run focused receiver/contract tests and the relevant testesp-derived PlatformIO
  build only. No unrelated controller test matrix or production upload.
- Record resolved CM/AsyncTCP/WebServer/framework versions, firmware sizes,
  hashes and BLE exclusion evidence in ignored logs plus this handoff.
- Parent performs app-only upload and short observation; owner compares behavior.
  Do not claim the earlier HTTP panic or partial delivery is fixed.

## Execution ledger

Plan written before metadata edits. The subsequent explicit owner request also
refactors platformio.ini to a shared [env] with profile differences only. Ten
identical USB/OTA options moved to [env]; profile interpolation references follow
the moved build_flags. Ignored config-before.json/config-after.json snapshots
were compared for all five normal profiles: zero differing option values. The
published CM pin is unchanged. A clear >=4.7.0 preflight failure documents that
normal pinned profiles cannot build the new API until an appropriate dependency
is selected; no publication is authorized.

Metadata implementation and focused host contract are complete. The real forty
SBT descriptors and production live-value helper pass type/routing/units/current
precision, individual getter/provider lifetime and registration-failure checks.
The target build passed; hardware acceptance remains with the parent/owner.

- Focused host metadata test passed, including all forty production SBT records,
  individual live provider getters after local group destruction, and first/middle/
  last metadata-admission failures. Receiver boundary and actual sibling publisher
  contract tests also passed (26 supported values, 3 unavailable).
- Target command: pio run -e testesp -c
  .pio/compact-runtime-metadata/platformio-test.ini. SUCCESS in 84.60 seconds;
  RAM 75,280 bytes, flash 1,239,021 / 1,835,008 bytes. No hardware operations.
- Resolved CM 4.7.0, ESPAsyncWebServer-esphome 3.4.0, AsyncTCP-esphome 2.1.4,
  Arduino-ESP32 framework 3.20017.241212+sha.dcc1105b (Arduino 2.0.17).
- ELF symbol scan found no CM BluetoothManager, NimBLE executable/data symbols,
  ble_hs_, nimble_port_init or esp_bt_controller_init. The six absolute linker
  boundary symbols named _nimble_* form three identical start/end pairs (zero
  bytes); those linker constants are not BLE functionality. No NimBLE dependency
  or BluetoothManager source include exists in this candidate.
- Exact local build evidence, resolved package list, hashes and symbol output are
  in ignored .pio/compact-runtime-metadata/build-evidence.json and
  firmware-symbols.txt. The target log is testesp-build.log. Snapshot equivalence
  evidence is config-equivalence.json (five normal profiles, zero differences).
- Product files are frozen after this build. No source edits, commit, push, serial,
  browser, NVS check or upload by this implementation agent. Remaining work is
  parent review/app-only COM7 installation and owner behavior acceptance.
- No tracked changelog exists; README and this plan record the compatible 5.0.2
  metadata/build maintenance. Historical versioned acceptance remains unchanged.
- Compile-time feature evidence: testesp task flags contain SIL_MOCK_TRANSPORT=1,
  CM_ENABLE_LOGGING=0, CM_ENABLE_VERBOSE_LOGGING=0, CM_DISABLE_GUI_LOGGING=1,
  FEATURE_OLED_DISPLAY_ENABLED=0 and FEATURE_MQTT_LOGGER_ENABLED=0. BME280 remains
  enabled by its existing default; optional fan/heater remain disabled.

firmware.bin: 1245600 bytes; SHA-256 `bec44dd41f91bec92ce1307063792bb975295d70bd5b7519aa9b768f0b7d0c68`.

firmware.elf: 12460796 bytes; SHA-256 `0abdd48a0e0ecfde9d21f11ef81af8ae903e5d610099cc016c33151be7b8fe19`.

## COM7 comparison and subsequent owner feedback

Application-only upload completed at the previously verified active app0 offset
0x10000. Image SHA-256: bec44dd41f91bec92ce1307063792bb975295d70bd5b7519aa9b768f0b7d0c68,
1,245,600 bytes. No NVS read/check/write, partition-table write or OTA-selection
write was performed by the deployment tool. The owner explicitly stopped further
NVS checks. No other device or physical inverter transport was used.

Serial observation: 70.1 seconds, no abort/backtrace/Guru/brownout/assert markers,
no software reset; one initial POWERON_RESET. The first browser navigation ended
with ERR_CONNECTION_RESET, without a corresponding ESP panic. Subsequent direct
HTTP checks confirmed version 5.0.2, all 80 metadata descriptions (12,631 bytes,
zero Bluetooth fields), runtime JSON (2,576 bytes), and the complete static page
(61,595 bytes in 7.78 seconds). The runtime endpoint reported freeHeap 113,988
bytes and heapFragmentation 43 at that sample. This is bounded smoke evidence;
no long-run stability or proven BLE root cause is claimed.

The owner subsequently reported that SIL's web UI feels about three times faster
than before. This is owner feedback, not an instrumented speed benchmark. The
owner is now testing the device; do not interrupt it with browser/monitor/upload
operations. COM7 was released after the observation. Private evidence is under
.pio/compact-runtime-metadata/device-COM7/.

After the owner questioned the still-pinned normal build and unused display
packages, the configuration was corrected beyond the initial organization-only
refactor: common lib_deps now selects symlink://../ConfigurationsManager, and
SSD1306/GFX/BusIO declarations and their unnecessary ignore entries were removed.
This intentionally changes dependencies for all standard profiles; the earlier
five-profile zero-difference result applies to the organization-only stage.
Normal USB build validation follows. There is no published immutable CM 4.7 pin
yet; the local checkout, including uncommitted changes, is an explicit development
requirement. No new Git commit/push was authorized or performed.

Normal-profile correction validation: `pio run -e usb` passed in 99.50 seconds
against the adjacent local CM 4.7.0 with OLED dependencies removed. This was a
build only; the device remained on the previously installed mock-transport SIL
comparison image while the owner tested it. The precompile resolver change and
package removal received an independent read-only sanity review with no concrete
findings. No source behavior or firmware version changed after deployment.
All new changes remain uncommitted and unpushed on feature/battery-energy-control.

Final consumer-build correction: the old wrapper regenerated shared CM WebUI
assets during the first normal build. That build-only side effect was detected
with git diff; the previously unchanged asset was restored exactly from the
reviewed fc02a11 checkpoint used by the deployed comparison. The wrapper now
only validates the selected CM name/version, matching source/WebUI version
mirrors and the existing generated asset. It never runs the CM WebUI generator
or changes shared source. Prepare intentional UI changes in CM itself.

Final `pio run -e usb`: SUCCESS in 45.51 seconds, selected local CM 4.7.0 printed
explicitly. RAM 75,304 bytes; flash 1,240,381 / 1,310,720 bytes for the normal
profile. git diff confirms no generated html_content.h content change after
this final build. This supersedes the earlier 99.50-second build as final evidence.
No device operations were performed after the owner began testing. The installed
COM7 mock-transport comparison image remains the original reviewed artifact.

## Owner-authorized SIL checkpoint (2026-10-18)

The owner explicitly requested a local SIL commit on the existing
`feature/battery-energy-control` branch. This supersedes earlier statements
that no commit was authorized. The adjacent CM checkout is now the consolidated
`feature/ble-compact-runtime` branch at
`51330563b2207a5453255aa6e9caa850f5de312c`; the SIL development dependency
continues to resolve that checkout through the existing local symlink.

Fresh checkpoint validation: the focused compact metadata host suite passed;
`pio run -e usb` succeeded in 9.02 seconds with CM 4.7.0, RAM 75,304 bytes and
flash 1,240,381 / 1,310,720 bytes. Product sources and configuration were unchanged
after these checks. The build log is private at
`.pio/compact-runtime-metadata/commit-usb-build.log`. No additional hardware,
browser, serial-monitor, NVS or upload operation was performed. No push, main
integration or release-branch update is part of this checkpoint. Firmware stays
5.0.2; stability acceptance and the intermittent HTTP investigation remain open.