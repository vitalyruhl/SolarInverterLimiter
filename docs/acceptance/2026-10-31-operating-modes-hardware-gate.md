# Operating modes: authorized hardware gates and deployments

Date: 2026-10-31, Europe/Berlin. The owner authorized five panic-free minutes
on the test ESP before each production upload.

## SIL 5.5.0

After SBT was installed, SIL 5.5.0 was flashed on COM8 with mocked RS485,
DHCP and test MQTT isolation. Its observation lasted 300.078 seconds: one
expected initial boot, no panic/fault or unexpected boot events, and continuing
serial activity. The device identified itself as 5.5.0.

Two ArduinoOTA transfers aborted while production remained 5.4.10. A local
diagnostic uploader with a longer per-chunk wait succeeded; production then
identified itself as 5.5.0. Its image and non-secret source were preserved.

## SIL 5.6.0 and PV follower

The owner separately approved the fourth mode with the same deployment gate.
Targeted host tests and independent review passed. USB, OTA and testesp builds
passed. The reviewed test image was flashed on COM8 and identified as 5.6.0.
A fresh 300.062-second observation had no panic or unexpected boot events.

ArduinoOTA aborted at 773,120 / 1,194,736 bytes with host-side Windows
`ConnectionAbortedError(10053)`; production remained 5.5.0. The existing
password-protected CM `POST /ota_update` route accepted the identical reviewed
image and returned HTTP 200 `{"status":"ok","action":"reboot"}`.
`/version` subsequently confirmed `Solar Inverter Limiter 5.6.0`.

Final MQTT showed the expected software restart (boot 12), advancing loops,
fresh SBT input, and configured/effective Automatic with reason `reserve`.
Initial `bms_unverified` cleared after source requalification on the same boot;
the source adapter and session fence were not changed.

Only after production confirmation was the existing HA selector updated to
four options, retaining its unique ID. All mappings/expiry/availability templates
passed offline checks; the broker retained the exact definition. The real HA menu
displayed Automatik, Hausverbrauch folger, Batterie-lade-Vorrang and PV-Folger
beneath the MOS switches. It was closed without selecting another mode.

PV follower uses a 0.90 conversion estimate to cap the requested output by fresh
PV, corrected household demand and the configured maximum. It is not a measured
guarantee of exactly zero battery current under changing sun or inverter losses.
Positive-output cases exercised the real output application path in host tests;
the physical bench transport was mocked. This is not multi-day acceptance.

MOS policy, capacity/reserve settings and disabled Live UI were preserved.
No production mode/MOS command, commit, push or release was performed.
Evidence, image hashes, captures and uploader responses are retained in
`C:/Temp/energy-rollout-20261031` and `.local/pv-follow-artifacts.json`.
