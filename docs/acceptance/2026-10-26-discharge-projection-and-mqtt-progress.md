# Discharge projection and MQTT send-progress checkpoint

## Scope and distinction

The owner authorized a complete chronological discharge projection, the configured 900 W ceiling, consistent PV accounting, targeted diagnostics, a bounded COM8 test, production OTA after functional acceptance, and at least fifteen minutes of subsequent observation. The owner also requested the SBT-style last-update text under the existing SIL uptime row in Home Assistant.

Earlier production MQTT outages were not evidence of repeated ESP resets: boot count and uptime continued, the owner observed the ESP and RS485 LEDs operating, and the broker logged client keepalive expirations. The original long outage required a manual reset. Its complete cause is not established by this checkpoint.

## Product changes

- The discharge snapshot now simulates all considered quarter-hours using an independent chronological energy/headroom ledger. Its first value remains the real current economic ceiling. Comparable-price windows can appear before they become current; materially better future prices preserve their existing reservation priority.
- Both discharge allowance and the AC load subtracted from forecast PV use configured MAX. The previous fixed 650 W assumption is removed. Initial energy below Reserve OFF is replenished before forecast storage becomes spendable. Qualified PV is booked once, after its interval, subject to charging limits and physical headroom. Forecast values and calibration are unchanged.
- The projection does not advance measured energy, source sequences, cache timestamps, or the live reserve-ON latch. Household demand, faults, source freshness, MOS capability, MIN, PID, ramps and correction remain authoritative. The series is a conditional allowance, not a guaranteed inverter command.
- Runtime diagnostics add current control age, loop count, the uptime of the maximum stage, and the most recent stage/loop taking at least 500 ms. The retained RTC layout is unchanged. The fixed MQTT formatter is checked for truncation; its worst tested payload is 631 bytes plus NUL in a 640-byte buffer.
- CM 4.10.2 fixes a reproduced false TX timeout. Previously, a FIFO that never became entirely empty expired after five seconds even while individual bytes progressed normally. The deadline now measures five seconds without a positive TCP send. Enqueue and transient socket errors do not renew it. FIFO capacity, call/time budgets, overflow and cleanup remain bounded. TCP acceptance is not broker acknowledgement or an absolute packet-age guarantee.

## Validation before production

The affected FuturePrices, ChargePlan, current-control/PID/TX, runtime-progress and MQTT serialization suites passed. Planning regressions include configured MAX 500/900/1200 W, flat and declining prices, nonpositive prices, partial intervals, scarce stored energy, reserve deficits, chronological PV, missing coverage, headroom, cache changes and unchanged live scalar decisions. Sixteen consumer-package tests passed.

The CM regression first failed at 5000 ms despite successful sends at 4900 ms. After the fix, continuous ordered progress for ten seconds and partial progress for twelve seconds survive; a subsequent 4999 ms stall survives and 5000 ms closes once. Transient errors, enqueues and uint32 rollover are covered. Separate tests using actual PubSubClient 2.8 distinguish a partial 142/165-byte CONNECT that recovers from sustained pressure reaching the unchanged one-second handshake timeout. CM USB and both SIL profiles built successfully.

COM8 used the physically identified MAC `3C:8A:1F:A7:22:7C`, version 5.4.5 and hard `SIL_MOCK_TRANSPORT=1`. Its 75 settings matched the saved test baseline and its broker, client ID, output and all inputs were isolated. WiFi power saving was false with native NONE and no policy error. Physical RS485 was not exercised on COM8.

The final COM8 functional run reached 900 W in the real controller/mock output path, published two valid 144-slot plans, and restored actual price/grid inputs after the bounded 75-second stimulus. One earlier TX timeout and two connection failures occurred while the main loop continued; they did not increase during the final functional comparison. The complete connection problem therefore remained open rather than being described as a network PASS. A prior 5.4.4 stimulus lacked a fresh price restore and was explicitly cleared by resetting only COM8; neither 5.4.4 candidate reached production.

## Frozen inputs and production operation

SIL 5.4.5 consumes `file://.local/cm-mqtt-progress-4.10.2-package`, with separate build outputs under `.pio/mqtt-progress-5.4.5`. The 188-file source archive, LICENSE and original manifest are tracked under `dependencies/cm/` and were roundtrip hash-checked. No Registry publication is implied. Earlier frozen packages remain unchanged.

- CM source ZIP SHA256: `974f1467cfc68e6be7f36f39472c145a574e15c3ed69e7c058e26fbb4d594721`.
- CM source manifest SHA256: `910bc0b64062932101e2c78d110881b2edbc29f5b318912d3b9f0ed2da3b240b`.
- Production BIN: 1,184,992 bytes, SHA256 `0580d368a0240f6a21d9c505b17fc7b16f9235b331a2986826691b5125b163a9`.
- Production ELF SHA256: `a6d18a507ea87b945e4284517881c5878f7f9011be079721220199c4d49ba6a9`.
- COM8 BIN SHA256: `89f02d25af603de9cbb01c8733354e0cfe7c4d0c28a96193fdbbf97ef1b57864`.

A preflight detected that the owner had changed Price Comfort from 0.21 to 0.20 EUR/kWh. It stopped before consuming any upload intent. A separate reconciled baseline preserved this actual setting; no settings were written. Exactly one OTA POST was then accepted at `2026-10-26T20:00:38.923015Z` for production `192.168.2.122`, MAC `3C:8A:1F:A7:EB:68`. Fresh post-reboot identity, running version 5.4.5, Settings-only profile, inactive OTA and unchanged 75 settings were verified. Masked secrets are comparable only as masked representations. COM8 was reset shortly afterward for comparable uptime observations.

The dedicated observation window ran from `2026-10-26T20:02:18.2621459Z` through at least `2026-10-26T20:17:18.2621459Z`. An independent final extraction covered 929.566 seconds and 302 runtime publications: production boot count remained 5, no uptime regression or SIL offline LWT occurred, and all nine transport counters remained zero. Maximum loop gap was 65 ms and maximum stage 44 ms, with no stage at or above 500 ms. The exact 900-second extraction found control age between 15 and 1990 ms. Periodic free heap stayed at least 130,924 bytes; minimum-ever free heap was 94,004 and the smallest reported maximum allocation block 77,812 bytes. Both plans ended valid with 144 intervals and the configured 900 W projection bound. The final HTTP/identity/settings verification also passed, and runtime source hashes still matched the frozen build. Task-local raw serial/MQTT logs, single-attempt OTA evidence, source patches and exact artifact manifests are preserved separately.

The observation also captured a distinct SBT panic restart. Its estimated boot time was `2026-10-26T20:12:13.324Z`; a fresh non-retained ESP publication at `20:12:23.531Z` reported uptime 10,207 ms and reset reason 4 (`ESP_RST_PANIC`). SIL itself continued normally. Missing requalified battery inputs temporarily invalidated both plans; HA recorded the discharge budget invalid approximately `20:12:25` to `20:14:29Z`, followed by successful recovery. Thus successful SIL availability must not be misreported as uninterrupted valid source data.

The contemporaneous COM8 comparison captured two separate WiFi stages of 5920 and 5822 ms, after an explicit WiFi disconnect. AP priority was enabled, Smart Roaming disabled, and test ICMP health probing disabled. The reconnect path can invoke a synchronous preferred-BSSID scan; this is a concrete blocking candidate, not direct proof of the precise native call. Production showed neither this stage nor a reset in the observation window. The isolated AMQTT fixture also has a five-second QoS1 PUBACK deadline, so its source-recovery behavior is not identical to production Mosquitto.

## Home Assistant

Only `secondary_info: last-updated` was added to the existing `sensor.solarlimiter_uptime_human` row, matching SBT. Full before/persisted YAML comparison found that single property addition. Desktop and mobile Chrome rendered it; Android itself was not tested. Exact HA history over the 900-second window showed one uninterrupted `ok` block and uptime advancing from 1m5s to 16m5s. Both desktop and mobile Chrome displayed the valid plan up to 900 W. Missing plans before the next complete forecast batch and during the independent SBT restart were data states, not chart configuration errors.

## Remaining boundaries

This checkpoint does not establish the cause of every real socket stall, prove production physical RS485 signalling, calibrate PV yield, or validate the CM change in SBT hardware. The SBT panic and COM8 WiFi reconnect stall remain separate open findings. The exact frozen SBT 1.4.8 ELF supports flash coredumps and its partition table reserves 64 KiB at 0x3f0000; a valid saved crash dump may exist, but existence, integrity and crash location were not checked. No backtrace is present in MQTT, and heap samples before the panic do not prove an allocation failure. Reconnect source qualification and the next periodic plan capture can still delay chart recovery; no protection fences were removed. No commit, push, merge, release or Registry publication was performed in this phase. Public issues must remain open until owner confirmation and their required gates.
