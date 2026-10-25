# MQTT runtime hardening acceptance

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
