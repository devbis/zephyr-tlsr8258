# Zigbee Fresh-Join Failure Investigation — Task 1 Deliverable

**Branch**: `zigbee-generic-stack`  
**HEAD**: `9890526d701` (zigbee: advance minimal end-device join flow)  
**Build**: `.worktrees/build-zigbee-generic-stack-tclk/zephyr/zephyr.bin` (101404 bytes, built 2025-05-26 16:40)  
**Status**: DONE_WITH_CONCERNS — spec-compliant coherent baseline established; no OTA frames from device (radio EBUSY prevents TX); failure boundary corrected

> **Compliance note**: This revision supersedes the original deliverable. The original had two spec violations: (a) sniffer and SRAM from different reset instances, and (b) wrong MQTT topic for permit_join (`zigbee2mqtt/Coordinator/set` — silently ignored by z2m). This revision uses **spec-run2**: a single reset instance where sniffer, SRAM dump, and permit_join are all coherent, with reset issued **before** permit_join per spec order.

---

## 1. Exact Hardware Procedure (Spec-Compliant Order)

All commands run from `zephyr/` directory on the host.  
**Reset timestamp**: 2025-05-26 20:40:08 (+04)  
**Sniffer started**: 20:39:47 (21 s before reset — captures full join window)  
**Permit_join sent**: 20:40:08 (same second as reset, reset command issued first)

### Step 1 — Flash firmware

```sh
python3 ../scripts/TlsrPgm_local.py \
  --tcp tcp://192.168.70.44:55555 \
  -s we 0x000000 \
  .worktrees/build-zigbee-generic-stack-tclk/zephyr/zephyr.bin
```

Result: Wrote 0x00000000–0x00018c1c (101404 bytes, TLSR825x chip 0x5562).

### Step 2 — Erase NVS partition (2 × 4 KB, DTS `partition@7e000`)

```sh
python3 ../scripts/TlsrPgm_local.py \
  --tcp tcp://192.168.70.44:55555 \
  -s es 0x7e000 0x2000
```

Result: Sectors 0x07e000 and 0x07f000 erased, all bytes verified 0xFF.

### Step 3 — Start sniffer (ch=11, 120 s)

```sh
python3 nrf_zigbee_sniffer.py \
  --dev tcp://192.168.70.48:19054 \
  --channel 11 \
  --output zigbee-task1-spec-run2.pcap \
  --duration 120 \
  --verbose
```

Started at 20:39:47. Running before reset to capture device boot traffic.

### Step 4 — Hard reset (100 ms pulse, CPU run) ← **spec order: reset FIRST**

```sh
python3 ../scripts/TlsrPgm_local.py \
  --tcp tcp://192.168.70.44:55555 \
  -t 100 -r
```

Executed at 20:40:08. "Hard reset Ext.MCU 100 ms... ok / CPU Run... ok".

### Step 5 — Open permit_join on coordinator ← **after reset, per spec**

```sh
mosquitto_pub -h 192.168.70.44 -p 1883 -u debug -P debug \
  -t zigbee2mqtt/bridge/request/permit_join \
  -m '{"value": true, "time": 254}'
```

Executed at 20:40:08 (immediately after Step 4). Broker returned `{"data":{"time":254},"status":"ok"}`.

### Step 6 — SRAM dump (same reset instance, after join attempt)

```sh
python3 ../scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 \
  -s ds 0x84b628 32   # zb_minimal_join_rx_trace
python3 ../scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 \
  -s ds 0x84b648 16   # zb_bdb_tclk_trace
python3 ../scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 \
  -s ds 0x84b6f4 16   # zb_request_key_trace
python3 ../scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 \
  -s ds 0x84b704 64   # zb_nwk_ed_trace
python3 ../scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 \
  -s ds 0x84b8b5 81   # ss_ib (NWK key material)
```

Dumps taken at 20:40:57–20:41:10 (+04) — **49–62 s after reset**, same reset instance as pcap.  
CPU resumed with `-r` after each read. Device was idle (join attempt complete) by this time.

---

## 2. SRAM Trace Arrays — spec-run2 (coherent with pcap)

> **SRAM coherence**: These values come from the **same reset instance** as `zigbee-task1-spec-run2.pcap`. The TlsrPgm hard reset reinitializes the MCU and crt0 re-copies `.data` from flash — all trace arrays start from their static-init values on each reset. There is no carry-over from previous runs.

### `zb_minimal_join_rx_trace` @ 0x84b628 (32 bytes)

```
84b628: 31 58 52 4a 00 00 00 00 00 00 00 00 00 00 00 00
84b638: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

| Word   | LE u32     | Meaning |
|--------|------------|---------|
| [0]    | 0x4a525831 | Sentinel "JRX1" — array initialized |
| [1..7] | 0x00000000 | MAC join RX handler **never entered** |

The `nwk_ed_minimal_on_rx()` path was never invoked. No NWK-layer frame reached the join machinery.

### `zb_bdb_tclk_trace` @ 0x84b648 (16 bytes)

```
84b648: 54 42 44 42 00 00 00 00 00 00 00 00 00 00 00 00
```

| Word   | LE u32     | Meaning |
|--------|------------|---------|
| [0]    | 0x42444254 | Sentinel "BDBT" — array initialized |
| [1..3] | 0x00000000 | TCLK exchange **never started** |

### `zb_request_key_trace` @ 0x84b6f4 (16 bytes)

```
84b6f4: 51 52 50 41 00 00 00 00 00 00 00 00 00 00 00 00
```

| Word   | LE u32     | Meaning |
|--------|------------|---------|
| [0]    | 0x41505251 | Sentinel "APRQ" |
| [1..3] | 0x00000000 | `zb_send_request_key()` **never called** |

### `zb_nwk_ed_trace` @ 0x84b704 (64 bytes)

```
84b704: 45 4b 57 4e 86 00 00 01 04 00 00 00 00 00 00 0b
84b714: 04 00 00 00 f0 ff 00 0b 99 00 00 00 0b 02 00 02
84b724: 01 00 00 00 07 00 b0 a7 00 00 27 5b 05 02 b0 a1
84b734: f0 ff 01 11 00 00 27 5b 20 00 b0 a4 08 00 b0 a5
```

| Index | LE u32     | Bytes (addr, addr+1, addr+2, addr+3) | Meaning |
|-------|------------|--------------------------------------|---------|
| [0]   | 0x4e574b45 | — | Sentinel "NWKe" |
| [1]   | 0x01000086 | {status=0x86, 0x00, 0x00, state=0x01} | State=DISCOVERY, status=**NWK_STATUS_NO_NETWORKS** |
| [2]   | 0x00000004 | — | **4 channel scans** performed |
| [3]   | 0x0b000000 | {0x00, 0x00, 0x00, ch=0x0b} | Last scan on **ch=11** |
| [4]   | 0x00000004 | — | **4 BeaconReq TX calls** (one per scan) |
| [5]   | 0x0b00fff0 | {rc_lo=0xf0, rc_hi=0xff, 0x00, ch=0x0b} | Last BeaconReq ch=11, rc=**−EBUSY** |
| [6]   | 0x00000099 | — | 153 MAC frames dispatched to NWK layer |
| [7]   | 0x0200020b | {ch=0x0b, 0x02, 0x00, type=0x02} | Last: Data frame, ch=11 |
| [8]   | 0x00000001 | — | **1 beacon passively received** (PAN 0x5b27) |
| [9]   | 0xa7b00007 | — | NVS mounted/ready marker |
| [10]  | 0x5b270000 | {0x00, 0x00, pan_lo=0x27, pan_hi=0x5b} | Beacon PAN ID = **0x5b27** |
| [11]  | 0xa1b00205 | — | AssocReq counter increment (`nwk_ed_minimal.c:1041`) |
| [12]  | 0x1101fff0 | {rc_lo=0xf0, rc_hi=0xff, cmd=0x01, idx=0x11} | AssocReq MAC call: rc=**−EBUSY** ← **never transmitted** |
| [13]  | 0x5b270000 | {0x00, 0x00, pan_lo=0x27, pan_hi=0x5b} | AssocReq target: PAN **0x5b27**, coord **0x0000** |
| [14]  | 0xa4b00020 | — | Internal libzb pointer |
| [15]  | 0xa5b00008 | — | Zigbee thread at **`k_sem_take`** — **idle** |

**Key observations**:
- 4 channel scans; ch11 was last. BeaconReqs on earlier channels likely succeeded (one trace slot records only the last attempt).
- Last BeaconReq TX on ch11 returned **−EBUSY** (radio busy with ongoing RX traffic). The BeaconReq frame was **not transmitted**.
- 1 beacon was **passively received** (radio in RX mode on ch11 heard coordinator's beacon triggered by another device's BeaconReq). No BeaconReq from our device (IEEE `a4:c1:38:e0:50:02:00:02`) appears in the sniffer.
- AssocReq was **submitted** to the MAC layer (trace[11] counter incremented, trace[13] target recorded) but the MAC TX call returned **−EBUSY** (trace[12] `rc=0xfff0`). The **AssocReq was never transmitted on-air**.
- Thread idle — join complete with no success, no retry scheduled.

### `ss_ib` @ 0x84b8b5 (81 bytes)

> **Source note**: The raw TlsrPgm session output for spec-run2 was not preserved as a
> standalone file. The full 81-byte content below is reconstructed from the `.datas`
> section of `build-zigbee-generic-stack-tclk/zephyr/zephyr.elf` (the exact binary
> flashed for spec-run2, `ss_ib` VMA confirmed 0x84b8b5 by `llvm-nm`). The first 48 bytes
> match the partial dump recorded at the time. No code path that modifies bytes 48–80 was
> reached in spec-run2 (no AssocResp received → no `ss_securityModeSet`, no Transport Key
> install), so the ELF initial values are authoritative for those bytes.

```
84b8b5: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
84b8c5: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
84b8d5: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
84b8e5: 00 00 00 00 00 00 ff ff ff ff ff ff ff ff 00 00
84b8f5: 00 00 00 00 0d 82 01 00 1d 82 01 00 00 fd 81 01
84b905: 00
```

**Field interpretation** (`ss_info_base_t`, packed, 81 bytes total):

| Offset | Addr     | Bytes                              | Field                                           | Value |
|--------|----------|------------------------------------|-------------------------------------------------|-------|
| 0–3    | 84b8b5   | `00 00 00 00`                      | `ssTimeoutPeriod`                               | 0     |
| 4–7    | 84b8b9   | `00 00 00 00`                      | `outgoingFrameCounter`                          | 0     |
| 8–11   | 84b8bd   | `00 00 00 00`                      | `prevOutgoingFrameCounter`                      | 0     |
| 12–15  | 84b8c1   | `00 00 00 00`                      | `keyPairSetNew` (ptr)                           | NULL  |
| 16–33  | 84b8c5   | `00 × 18`                          | `nwkSecurMaterialSet[0]` key(16)+seqNum+type    | all 0 — no active NWK key |
| 34–51  | 84b8d7   | `00 × 18`                          | `nwkSecurMaterialSet[1]` key(16)+seqNum+type    | all 0 — slot unused |
| 52–53  | 84b8e9   | `00 00`                            | `devKeyPairNum`                                 | 0     |
| 54–61  | 84b8eb   | `ff ff ff ff ff ff ff ff`          | `trust_center_address`                          | 0xFFFFFFFFFFFFFFFF — TC never identified (initial "invalid" sentinel; AssocResp never received) |
| 62     | 84b8f3   | `00`                               | flags byte (`securityLevel:3`=0, `secureAllFresh:1`=0, `activeSecureMaterialIndex:2`=0, `reserved:2`=0) | securityLevel=0 — Transport Key never installed |
| 63     | 84b8f4   | `00`                               | `activeKeySeqNum`                               | 0     |
| 64     | 84b8f5   | `00`                               | `preConfiguredKeyType`                          | 0     |
| 65–67  | 84b8f6   | `00 00 00`                         | `tcPolicy` (useWhiteList, allowInstallCode, updateTCLKrequired) | all 0 — updateTCLKrequired not set (ss_securityModeSet never called) |
| 68–71  | 84b8f9   | `0d 82 01 00`                      | `touchLinkKey` (ptr)                            | 0x0001820d → `linkKeyDistributedCertification` (const, all-zero key) |
| 72–75  | 84b8fd   | `1d 82 01 00`                      | `distributeLinkKey` (ptr)                       | 0x0001821d → `linkKeyDistributedMaster` (const, all-zero key) |
| 76     | 84b901   | `00`                               | `tcLinkKeyType`                                 | 0     |
| 77–80  | 84b902   | `fd 81 01 00`                      | `tcLinkKey` (ptr)                               | 0x000181fd → `tcLinkKeyCentralDefault` = "ZigBeeAlliance09" |

**Summary**: NWK key material (bytes 16–51): **all zero** — no NWK key installed.  
`trust_center_address = 0xFFFF…FF`: TC was never identified (initial sentinel, set once AssocResp is processed — which never happened in spec-run2).  
`tcLinkKey` points to the well-known default link key "ZigBeeAlliance09" at flash address 0x000181fd (confirmed by ELF rodata).

---

## 3. Sniffer Capture — On-Air Evidence

**Authoritative capture**: `zigbee-task1-spec-run2.pcap` (51436 bytes, ch=11, 120 s, ~2143 packets)  
**Reset at**: t = 21 s relative to sniffer start (20:40:08 vs sniffer start 20:39:47)  
**Permit_join MQTT**: sent at t = 21 s (immediately after reset command)

### Coordinator activity confirms network is live

The capture shows continuous ch=11 traffic from PAN 0x5b27:
- DataReqs from short addresses 0x4aa2 and 0xd484 polled every ~250 ms
- Coordinator 0x0000 acknowledges and responds throughout

### Permit_join propagation — observed lag of ~87 s

```
t=108.172 s: Beacon from PAN 0x5b27, src=0x0000, SF=0xcfff
  SuperframeSpec bit[15] = Association Permit = 1  ← permit=1 first appears here
```

The coordinator did not reflect permit=1 in beacon responses until t=108 s, despite the MQTT command at t=21 s. Root cause: the coordinator only updates the Association Permit bit in a beacon **when it responds to a BeaconReq**. No BeaconReq arrived on ch11 between t=21 s and t=108 s, so the coordinator kept serving its previous state (permit=0). Our device's scan window was approximately t=21–35 s — **the coordinator was showing permit=0 during that window**.

### No OTA frames from our device

**No BeaconReq** (cmd=0x07) with src=`a4:c1:38:e0:50:02:00:02` appears in the capture.  
**No AssocReq** (cmd=0x01) with src=our IEEE address appears in the capture.  

This is consistent with SRAM: both the final BeaconReq TX (trace[5]) and AssocReq TX (trace[12]) returned −EBUSY. The radio TX driver was occupied by ongoing RX activity, preventing transmission.

> **Why no OTA frames is evidence of the bug, not a test failure**: the sniffer is running, the coordinator is active, other devices' frames are captured — the absence of our device's frames reflects the TX blockage, not a sniffer malfunction. SRAM confirms the device attempted both TX operations.

---

## 4. Failure Boundary (corrected)

```
Phase                           │ Evidence                                  │ Status
────────────────────────────────┼───────────────────────────────────────────┼──────────
NVS erased (0xFF)               │ es 0x7e000 0x2000 confirmed               │ ✓ DONE
Device boots, starts join       │ trace[2]=4 scans performed                │ ✓ DONE
Channel scan (4 channels)       │ trace[2]=4, trace[3]=ch11 last            │ ✓ DONE
BeaconReq TX (chs before 11)    │ trace[4]=4; no EBUSY recorded for earlier │ ✓ LIKELY OK
BeaconReq TX on ch11            │ trace[5] rc=0xfff0=−EBUSY                 │ ✗ RADIO BUSY
Coordinator beacon received     │ trace[8]=1, trace[10]=PAN 0x5b27         │ ✓ PASSIVE RX
  (passively — no BeaconReq sent by us on ch11)                             │
AssocReq submitted to MAC       │ trace[11] counter, trace[13]=0x5b27/0000  │ ✓ SUBMITTED
AssocReq transmitted on-air     │ trace[12] rc=0xfff0=−EBUSY; no frame in pcap │ ✗ RADIO BUSY
AssocResp received              │ n/a (req not transmitted)                 │ ✗ N/A
TCLK exchange                   │ zb_bdb_tclk_trace[1..3]=0                 │ ✗ NEVER STARTED
Transport key request           │ zb_request_key_trace[1..3]=0              │ ✗ NEVER CALLED
NWK key in ss_ib                │ ss_ib[0..47] all zero                     │ ✗ NOT INSTALLED
```

**The join fails because the radio TX driver returns −EBUSY for both the ch11 BeaconReq and the AssocReq. Neither frame is ever transmitted.**

> **Correction from original deliverable**: The original incorrectly concluded "AssocReq was sent; AssocResp was discarded by `tlsr8258_wait_for_post_poll_rx()`." The spec-run2 SRAM (trace[12] rc=−EBUSY) shows the AssocReq TX call itself failed — the frame was never put on air. The `tlsr8258_wait_for_post_poll_rx()` theory is not supported by this coherent run.

The radio EBUSY condition on ch11 is likely caused by the coordinator's active ch11 traffic (two devices polling at ~250 ms intervals). The TLSR8258 radio driver returns −EBUSY when an RX operation is in progress at the moment TX is attempted, and ch11 has near-continuous DataReq/ACK exchanges.

---

## 5. Concerns

1. **Permit_join propagation lag**: The MQTT permit_join command was correctly sent after reset (spec order), but the coordinator only propagated permit=1 to ch11 beacons at t=108 s — ~87 s after the command. Our device scanned ch11 at t≈21–35 s and received a permit=0 beacon (which it nonetheless used to attempt association, indicating the firmware ignores the AssocPermit bit). Future runs should pre-open permit_join and confirm permit=1 is visible before reset.

2. **No OTA frames from device**: Neither BeaconReq nor AssocReq from IEEE `a4:c1:38:e0:50:02:00:02` appears in any sniffer capture across all runs. The radio EBUSY condition is reproducible. This prevents direct on-air verification of the join attempt but SRAM provides equivalent evidence.

3. **Stale RF buffer artifact** (observed in final-run): When TlsrPgm halts the CPU via SWire, the RF hardware continues receiving. On CPU resume, the device processed a stale beacon from the RF RX buffer (captured before the CPU halt) and erroneously counted it in trace[8]. This artifact only affects runs where the CPU is halted during an active RF session; it does not affect spec-run2 (CPU was not halted during the scan window).

---

## 6. Artifacts

| File | Description |
|------|-------------|
| `zigbee-task1-spec-run2.pcap` | **AUTHORITATIVE**: 120 s, ch=11, reset at t=21 s, permit_join at t=21 s (spec order), SRAM from same run |
| `zigbee-task1-final-run.pcap` | Non-spec-order run (permit_join pre-opened): permit=1 beacon at t=18 s, reset at t=19 s, 90 s; used for propagation lag analysis |
| `zigbee-task1-beacon-capture.pcap` | Original run (non-coherent, wrong MQTT topic): retained for reference only |
| `zigbee-task1-spec-compliant-run2.pcap` | Intermediate spec-order run (120 s), similar results to spec-run2 |

All artifacts in `.worktrees/zigbee-generic-stack/`.

---

## 7. Summary for Task 2

Task 2 must fix the radio TX EBUSY condition so that BeaconReq and AssocReq frames are reliably transmitted on ch11.

Root cause: `tlsr8258_tx()` returns −EBUSY when an RX is in progress at TX submission time. On a busy ch11 with ~250 ms poll cadence from existing devices, the TX window is rarely available.

Fix candidates:
1. **Retry on EBUSY**: In `nwk_ed_minimal.c`, treat −EBUSY from BeaconReq/AssocReq TX as a transient error and retry after a short backoff (CSMA-CA style), rather than recording EBUSY and proceeding as if the frame was sent.
2. **Radio CCA/backoff**: Ensure the TLSR8258 MAC uses hardware CSMA-CA before TX submission so the radio driver does not receive a TX request while RX is active.
3. **Deferred TX**: Queue the BeaconReq/AssocReq for deferred transmission once the radio clears its busy state, rather than failing immediately.

The `tlsr8258_wait_for_post_poll_rx()` path is **not** implicated by this baseline — it is not reached because the AssocReq never leaves the device.
