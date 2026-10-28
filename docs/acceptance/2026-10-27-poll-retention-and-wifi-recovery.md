# SIL 5.4.7 polling retention and WiFi recovery acceptance

All event times below are UTC on 2026-10-26. The local calendar date was already
2026-10-27 during the final observations.

The application uses the immutable CM 4.10.3 recovery package. The production
binary SHA256 is `e608149c612820ba10afb41b7361de984040c99175f0f782a8aedfe417172cc5`;
the exact ELF SHA256 is `8c87eeadc564fed88ae44ab3b2ccbddfa935a086e0212f12f4329ae40db9284b`.

## Change and focused verification

Complete validated price and PV inputs now maintain a separate content revision.
Exact comparison happens before the existing active/pending swap, without an
additional array copy or a hash. Poll-only `r`/`g` updates preserve content identity.
Value, quality, trust, time-grid, coverage and withdrawal changes still revoke the
old projection. Public revisions, cache keys, source freshness and hardware
permissions retain their existing behavior. The held frame keeps its original
generation and the original 180-second limit.

Receiver and retention RED-to-GREEN cases cover unchanged polls, real value
changes, incomplete chunks, withdrawal, coverage and trust changes. The isolated
recovery hook is excluded from normal production and test binaries. Its boundary
tests exclude quarter-hour changes from this specific polling comparison.
The host object-size change is eight bytes in total; PV state excluding its JSON
arena is 720 bytes.

## Isolated hardware result

The mock-RS485 fixture ran from 22:55:37 through the fixed analysis cutoff
23:07:24.711. All 75 settings matched the previous isolated fixture. A planned
WiFi stack reset began at 23:04:30.128 and ended at 23:04:31.162 without a CPU reset.
The real 23:00-to-23:05 price/PV input poll changed only `r`/`g`.

Both previously valid 144-slot frames were republished byte-for-byte, including
original generation, revisions and arrays; neither became invalid. Fresh budget
and charge captures followed 36.603 and 44.844 seconds after reset began. All 85
mock requests in the fault window were zero watts. A separate startup request of
460 W followed the existing source-loss/idle fallback and is not included in that
zero-output claim.

Boot count and heap integrity stayed stable. The expected forced-disconnect
connection-failure increment was one; RX/TX timeout, malformed, oversized and TX
failure counters remained zero. Final free heap was 131008 bytes and the largest
block was 81908 bytes.

## Production result and limits

Production OTA was accepted at 23:08:31.475. Fresh device identity and HTTP readback
confirmed 5.4.7, the intended UI profile and 75 unchanged settings. The complete
900-second observation from 23:11:43 through 23:26:43 passed: 293 runtime samples,
boot counter seven, no uptime regression or offline event, all nine transport
counters zero, and both plan types continuously valid with 144 slots.

This is bounded recovery and production evidence. The bench uses mocked RS485;
physical inverter behavior and long-term crash freedom are not established.
Home Assistant availability can still hide a preserved frame while SIL itself
is disconnected from MQTT. The separate SBT panic investigation remains open.
