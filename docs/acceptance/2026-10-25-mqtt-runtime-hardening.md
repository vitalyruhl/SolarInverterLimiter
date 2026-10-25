# MQTT runtime hardening acceptance

## Session recovery update 5.4.2 at 17:42 UTC

The subsequent owner-requested repair uses unpublished local CM 4.9.2. Real
PubSubClient 2.8 characterization first reproduced immediate closes on EAGAIN,
EWOULDBLOCK, EINTR, and positive short writes. The fixed 4608-byte TX FIFO then
passed these recovery cases without byte replay or reordering. Focused tests
also cover overflow, definitive reset, stop/new-session isolation, five-second
expiry including timer wrap, RX service under TX pressure, handshake polling,
separate time/call limits, and complete/fragmented/missing PINGRESP. A healthy
22720-byte synthetic burst passed. The host adapter object grew by 4648 bytes;
the TX storage is fixed, with no dynamic buffer. An independent source review
found no blocker. These tests do not model lwIP cancellation of accepted bytes.

Root repeated the focused CM runner and SIL's three diagnostic/configuration
serializer tests. The seven consumer-wrapper tests, CM default USB build, and
SIL real/mock builds passed. Separate build/dependency directories preserved
the old production artifacts. New diagnostics expose the existing RX/TX fatal
counters and four additional TX pressure/partial/expiry/overflow counters;
Planning/Config reports the actual configured `maxOutputW`.

The COM8 pretest ran from 17:32:32 UTC for ten minutes with one intentional start
reset, isolated topics, real input forwarding, and all 19 calculation settings
aligned to production. The sole differing price-deadband setting was updated
and all other fixture settings were verified unchanged. The existing fixture
ACL rejected the observer's overly broad initial filter; a second observer
subscribed to the allowed output subtree without resetting the device.

A one-shot 32-second partial-publish fault began at 17:36:40.690 UTC. The old
connection closed after 29.345 seconds and a new connection opened immediately.
Control output continued with a maximum observed 2.010-second interval; the
recorded loop maximum stayed 68 ms, boot/reset remained unchanged, heap
integrity stayed valid, and memory recovered. Both plans were valid again at
17:38:12 UTC with 144 values and the original accepted revisions. All transport
counters remained zero: this field test demonstrates reconnect/recovery, not a
triggered adapter RX deadline, real TX backpressure, or a ghost-CONNECT fix.

The frozen production BIN SHA-256 is
`cbcea8b11ebef980c8497abb6ae2af5a1d725a6c951174b620a4f0251e2bb8c2`
(1180816 bytes); matching ELF SHA-256 is
`108273a47cf1bb0e801477d22a4a6700a8eb2156de16081e1967dd0d1258488b`.
One OTA request was accepted at 17:42:46.693531 UTC. Fresh post-reboot MQTT
identity and HTTP readback verified version 5.4.2, Live disabled, Settings
enabled, OTA inactive, and 74/74 unchanged settings. The fixture reset was
asserted 0.337 seconds after OTA acceptance for approximately aligned uptime.
The first production Runtime sample showed boot=2/resetReason=3 and all nine
transport counters at zero; advancing ESP uptime confirmed the reboot.

The subsequent passive fixture run provided additional real TX evidence:
`txBackpressure` reached 117 and `txPartialWrites` reached 1 by 17:46:03 UTC,
then stayed unchanged through at least 17:47:28 UTC. Fatal TX, expiry, and
overflow counters stayed zero, the same proxy session remained open, boot and
heap integrity stayed unchanged, and control plus fresh valid plan publications
continued. This demonstrates recovery from those observed hardware TX events;
it does not identify the original production delayed-CONNECT cause.

Subsequent production data did not pass continuous MQTT availability: by
17:48:49 UTC it recorded two TX expiries/fatal closes, 878 pressure events, one
partial write, zero overflows, and one TCP connect failure. Broker evidence
shows old-session replacement around 17:48:13 and a broker keepalive timeout
around 17:48:47, followed by reconnect. Runtime publication gaps were roughly
20 and 30 seconds. ESP boot stayed 2, the observed loop maximum was 135 ms, and
both plans subsequently remained fresh and valid. Counters then stayed stable
through at least 17:53 UTC. The five-second application FIFO deadline does not
cancel bytes already accepted by the TCP stack. Therefore the production
availability defect is still open; this deployment is not a complete fix.

The built CM WiFi source unconditionally requests `WIFI_PS_MIN_MODEM`. This is
a source observation, not proof of the live driver state or the cause of these
stalls. A short later probe lost no packets to production SIL, fixture SIL, or
the broker; average ICMP times were 109, 87, and 1 ms respectively. That healthy
snapshot does not rule out intermittent transport problems. No speculative
power-save, broker, router, or source-safety change was deployed.

The current-price cache still resets on disconnect. Recovery can therefore wait
for fresh scalar price input, as observed for roughly one minute in this test.
Preserving that cache needs explicit source/mapping provenance and remains a
separate follow-up; no stale-source exception was introduced. Delayed old
CONNECT/session replacement, the earlier offline WiFi-stage pause, and long-run
stability remain open. No registry publication, commit, or automatic integration
was performed for this second repair.

The HA desktop/mobile PV axes were fixed at 0..2000 W. A diagnostic MQTT sensor
now reads the real `Planning/Config.maxOutputW` with a numeric/validity check,
the existing availability topic, and expiry. Its discovery was published with
QoS1/retain and read back exactly. HA reported 900 W with matching typed
attributes. A transparent reference series couples discharge autoscaling to
that setting without a new dependency; larger historical values can still
expand the scale. Persisted card readback and both rendered variants were
verified, including independent screenshot review. No price/control setting or
existing series color was changed for this dashboard adjustment.

## Production update at 15:04 UTC

The owner explicitly resumed and authorized production OTA after the earlier
infrastructure hold. The frozen 5.4.1 BIN recorded below was uploaded once and
accepted at `2026-10-25T15:04:28.300097Z` with HTTP 200/reboot. Fresh post-update
MAC/IP identity and HTTP readback confirmed version 5.4.1, Live false, Settings
true, OTA inactive and all 74 captured settings unchanged. Masked secrets could
only be compared as masked; the uploader did not write settings.

The COM8 reset was released at `15:04:28.780664Z`, about 0.481 seconds after
production HTTP acceptance. These host events do not prove simultaneous boots.
Both continuing serial and passive production MQTT evidence were retained.
Fresh production ESP samples advanced from 3m 5s to 6m 5s with resetReason=3,
without an observed further uptime decrease in that window.

The unexplained MQTT reconnections and offline WiFi-stage latency described
below remain open. The owner-authorized deployment is an observation candidate,
not a long-term stability acceptance. The subsequent owner request repurposes
COM8 temporarily for a separate SBT Settings-only pretest, after saving its SIL
state. Evidence remains in task-workspace `sil-hardening-20261025/`.

## Candidate and authorization

SIL 5.4.1 uses an unpublished CM 4.9.1 development package. The owner authorized
implementation, targeted logs, at most 15 minutes of fixture pretesting,
production OTA after successful checks, and a nearby fixture restart for uptime
comparison. No commit, push, public library release or Unraid operation is
authorized by that request.

Real-transport BIN SHA256:
`8F6CD7031C0F1F7ECC35331BE440E561D910D8E4C14636D2EF337E9813C10C5D`.
Mock BIN SHA256:
`01D0565909E7D76CDB187175B6DB13228BFD31EC6B35A771D5DE0EB000D3148C`.
The frozen real BIN is 1,179,760 bytes. Source, artifacts and private evidence
are separate from public release status.

## Focused checks

- Numeric ingress tests passed with undefined/float-cast-overflow trap checks.
  RED/GREEN covered INT_MAX float rounding, embedded null bytes and malformed
  text. The actual extracted PID/application test covers invalid gains with a
  configured 700 W minimum, requiring the direct 0 W error path.
- Fixed-storage progress tracking tests passed for stage retention, reset,
  report cadence and millisecond rollover.
- Build provenance tests passed for the pinned published package and explicit
  local development package. Development builds compare consumed MQTT headers
  with the reviewed source to reject stale copies.
- CM transport and real PubSubClient 2.8 host tests passed for fragmentation,
  first-byte deadline, deadline crossing during pump, rollover, malformed and
  oversized frames, partial TX, QoS1, adjacent packets, delayed/fragmented CONNACK
  and unexpected handshake packets. Independent review found a handshake budget
  defect; a delayed-CONNACK regression reproduced it before correction.
- Both SystemInfo schema variants passed. ESP includes resetReason; the public
  default retains WiFi uptime while SIL opts out of the duplicate.
- SIL usb/testesp and the CM default-transport usb builds passed. The real SIL
  build uses approximately 89.5 percent flash. These results do not substitute
  for long-term runtime evidence.

## Fixture identity and isolation

COM8 was physically identified as MAC `3C:8A:1F:A7:22:7C`. NVS, partition table,
OTA selector and the previous active app1 were backed up before USB upload.
NVS is at 0x9000/0x5000; the old active app1 is at 0x150000/0x140000.
The fixture runs at `192.168.2.126`.

Forty-four production control/MQTT/Temp values were copied and verified. The
fixture kept its own WiFi/static address, OTA, identity, NTP, I/O and RS485 pin
settings. Every MQTT input/output and the SBT root were redirected to the
isolated front `192.168.2.220:18884`, backend 18885. The bridge subscribes only on
production and publishes only inside the fixture. Mock builds independently
suppress RS485 and BMS emergency writes.

## Bounded device observations

The overall pretest window ran from approximately 13:30:38 to 13:45:38 UTC on
2026-10-25. The first serial capture was limited to 600 seconds; a separate
passive tail ended at the fixed 900-second overall cap. There was a capture gap
between those segments. Increasing uptime and the same boot context bridge the
recorded samples; this is not a continuous 900-second UART trace.

Natural grid, inverter, BMS and Victron data were used. Since no natural forecast
batch had arrived, an explicitly `[MOCKED!]` 144-slot price/PV batch and scalar
price were published only inside the fixture. Its revision was 1792935677.
The actual planner reported forecast=true. Synthetic allocations must not be
described as matching the production plan.

At 13:44:43.313 UTC the proxy inserted a partial PUBLISH header and withheld
normal server bytes from the target connection for 32.002 seconds. A new device
connection appeared after approximately 30.09 seconds. Serial samples continued
with increasing uptime, boot=1, unchanged price/PV revision 1792935677, maximum
loop gap 1048 ms and heap integrity=1. The observed minimum heap was 103220 bytes;
the largest free block remained 86004 bytes in the later samples.

The RX timeout counter stayed zero. Therefore this device run does not prove
that the explicit RX deadline fired; MQTT keepalive may have ended the connection
slightly earlier. The host tests independently exercise the deadline itself.
DischargeBudget/ChargePlan publication was not fully established in the seed
window, so this is not complete projection acceptance.

## Open gate and production status

Additional target connections at 13:45:59 and 13:47:57 UTC were observed outside
the one-shot fault cleanup. Their cause is not established. Broker traces include
incomplete receives and PUBACK timeouts without sufficient event/client timing
to attribute a firmware defect. Do not label normal-traffic recovery fully
accepted from this evidence.

The owner reported an ongoing Unraid update with potentially missing services.
Production OTA and the aligned fixture restart are held until infrastructure is
available and the remaining reconnect evidence is reconciled. No production
firmware upload has occurred at this point. No SBT firmware was modified.

The owner subsequently reported that the Unraid update had failed and requested
continued fixture operation without a broker until further notice. A new
serial-only observer was started at 13:57:20 UTC without resetting COM8 or
subscribing to production MQTT. Task-owned fixture broker/bridge/proxy services
were stopped by 13:58:03 UTC; both listeners were absent at 13:58:06 UTC. The Unraid
system itself is untouched. A subsequent offline sample retained boot=1 and heap
integrity=1 but recorded a cumulative 5966 ms loop gap with a 5929 ms WiFi stage.
Its cause remains open; the bounded pretest latency must not be extrapolated to
the entire subsequent offline run. This ongoing offline
observation is distinct from the completed bounded pretest and does not grant
production deployment readiness.

## Home Assistant

SBT collector uptime was added to the smaller Victron card as a separate
collector value with a 180-second expiry. BMS source health remains BMS-specific;
the existing Victron BLE data status is distinct from collector MQTT connection.
Fresh uptime samples decreased from over 12 hours to approximately 15 minutes,
then advanced to 16 minutes 10 seconds. This establishes an intervening SBT
restart, not its cause. HA was also temporarily unavailable; no agent restarted
HA. The final card screenshot and configuration/discovery backups are in the
task workspace `sil-hardening-20261025/ha-sbt/`.


## Later checkpoint: 5.4.3 WiFi policy

The hold and infrastructure statements above describe the earlier 5.4.1 test
stage. Subsequent broker evidence did not establish infrastructure maintenance as
the cause of the MQTT reconnects. The owner later authorized deployment of the
bounded intermediate versions while keeping that defect open.

The exact frozen 5.4.3 binary was accepted at 20:20:52.937270 UTC on 2026-10-25;
its SHA256 is `fc9df288d6bb51c9994c1e8a1057b795d0d187705fb25c95f1d1ae2b3bfc7aba`.
Post-OTA identity, version, Live disabled/Settings enabled, inactive OTA and all
74 prior settings plus exactly one WiFiPowerSave=false addition were verified.
The final native-OFF fixture phase had one TX expiry, then recovered both fresh
144-slot plans at revision 1792959300. Control continued, boot count stayed one,
heap integrity stayed valid and the observed maximum loop gap was 117 ms.
These are bounded observations, not a long-run acceptance or MQTT root-cause fix.
The open transport investigation remains CM issue #89.

The source package remains the immutable CM 4.10.0 freeze documented in README.
Its exact source ZIP, SHA256 and original manifest are tracked in
`dependencies/cm/`; the restored `.local` directory remains ignored. The current
companion CM 4.10.1 source does not recreate this WiFi-only snapshot. Original
frozen packages and private BIN/ELF artifacts remain unchanged.
