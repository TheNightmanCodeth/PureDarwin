# SC8280XP / ThinkPad X13s display observations

Version 0.7, 25 November 2026. This is an evidence-backed partial specification.
It establishes the Linux runtime display path, physical scanout/flush/interrupt
register accesses and observed flip ordering. It also records PureDarwin
experiments on the inherited panel pipeline: vsync delivery, translated scanout
through MDSS context bank 4, and commit retirement. Section 18 states what a
first driver may rely on and what remains open. Later sections refine the
uncertainties retained in earlier ones.

## 1. Evidence and provenance

Source archive: `x13s-display-results.tar.gz`, supplied by the hardware owner.
SHA-256: `5fd8670991773381cf728f10a0682cfd21a3d2f172db8d142cb5f4cf049780e8`.
Raw captures remain outside the source tree. All 526 file hashes in the three
run manifests were verified: 162 idle, 162 motion, 202 controlled flips.
Every recorded snapshot command returned zero. All per-CPU trace overrun,
commit-overrun, and dropped-event counters were zero; trace enable errors were
empty. No `notes.txt` was supplied, so visual appearance and firmware boot path
are unconfirmed. The collector itself is included in each run.

The observation kernel reports `7.0.14-a690obs`, build #4, aarch64, built
19 October 2026. The runtime tree identifies `lenovo,thinkpad-x13s` and
`qcom,sc8280xp`. These are black-box observations and runtime metadata, not
Linux driver source. The clean-room rules in `README.md` apply.

Evidence labels below are paths relative to the archive's `x13s-display-results/`:

* [DT] `01-idle/before/device-tree.dts`, including its phandle graph.
* [MODE] `03-flips/during/modetest.txt`.
* [STATE] `03-flips/during/sys__kernel__debug__dri__1__state`.
* [CLK] `03-flips/during/sys__kernel__debug__clk__clk_summary`.
* [FLIP] `03-flips/trace.txt`; schemas in `03-flips/event-formats/`.
* [IDLE], [MOTION] the corresponding run's `trace.txt` and event schemas.
* [IRQ] each run's `before/proc__interrupts` and `after/proc__interrupts`.
* [TEST] `03-flips/workload-command.json`, `workload.txt`, `workload-result.txt`.
* [INV] `03-flips/before/debugfs-inventory.txt` and `available-events.txt`.

## 2. Display topology and resource windows

The enabled MDSS0 instance is `/soc@0/display-subsystem@ae00000`.
Its compatible is `qcom,sc8280xp-mdss`; address and size cells are both two.
Its `ranges` is empty. [DT]

| Resource | Base | Size | Evidence |
| --- | --- | --- | --- |
| MDSS0 parent (`mdss`) | 0x0ae00000 | 0x1000 | [DT] |
| DPU (`mdp`) | 0x0ae01000 | 0x8f000 | [DT] |
| DPU VBIF (`vbif`) | 0x0aeb0000 | 0x3000 | [DT] |
| DISPCC0 | 0x0af00000 | 0x20000 | [DT] |
| Referenced system SMMU | 0x15000000 | 0x100000 | [DT] |

DPU compatible: `qcom,sc8280xp-dpu`. The internal panel graph runs from DPU
`port@5` to `displayport-controller@aea0000` input, then from its output to the
`edp-panel` on its AUX bus. The eDP compatible is `qcom,sc8280xp-edp`.
Its five unnamed register ranges are, in order: [DT]

```
0x0aea0000 + 0x200
0x0aea0200 + 0x200
0x0aea0400 + 0x600
0x0aea1000 + 0x400
0x0aea1400 + 0x400
```

The eDP PHY phandle resolves to `phy@aec5a00`, compatible
`qcom,sc8280xp-edp-phy`. Its four ranges, in order, are
`0x0aec5a00+0x19c`, `0x0aec5200+0xec`, `0x0aec5600+0xec`, and
`0x0aec5000+0x1c8`. Do not assign register-block names absent additional evidence.
The eDP node declares data lanes `<0 1 2 3>`; this is not proof of the negotiated
lane count. [DT]

The MDSS IOMMU property is `<0x2c 0x1000 0x402>`; phandle 0x2c resolves to
`iommu@15000000`, compatible `qcom,sc8280xp-smmu-500`, `arm,mmu-500`, with
two IOMMU argument cells. Preserve the arguments verbatim here: this capture
does not establish stream-match programming or the selected context bank.
This is distinct from the GPU SMMU path. A GPU IOVA is not thereby a display IOVA.
[DT]

## 3. Mode and active pipeline

The connected internal connector is eDP-1, DRM connector 39, encoder 38,
CRTC object 88 (`crtc-0`); primary plane object 40 (`plane-0`). DP-1 and DP-2
are disconnected. Object IDs and card numbers are Linux runtime identifiers,
not hardware constants. [MODE, STATE]

Preferred mode: 1920x1200, pixel clock 153750 kHz; horizontal
`1920 1944 1992 2080`, vertical `1200 1203 1215 1232`, negative H/V sync.
The mode listing rounds refresh to 60.00 Hz. [MODE]

During controlled flips, primary scanout is XR24 little-endian (0x34325258),
linear modifier 0, pitch 7680 bytes, offset 0, buffer size 9216000 bytes.
No scaling or rotation beyond identity is reported. [STATE]

Two mixers cover the panel: LM0 is 960x1200 at x=0 and LM1 is 960x1200 at
x=960; both use CTL0 in the state dump. Pingpong resources 0 and 1 are assigned
to CRTC 88. The primary is `sspp_0`, stage 1 in state output. [STATE, FLIP]

Trace numbering differs from state numbering: primary scanout has `index:1`,
flush has `ctl_idx=1` and `intf_idx=6`, while video-IRQ enable prints
`intf_idx=5` and the tree uses port 5. Do not copy these software enum/index
values directly into hardware registers. The relationship is observed, not a
general numbering rule. [FLIP, DT]

## 4. Clock observations

The following are reported clock rates during the flip workload, not measured
oscillator frequencies or a validated initialization recipe. [CLK]

| Clock | Reported Hz |
| --- | ---: |
| disp0_cc_mdss_mdp_clk | 150000000 |
| disp0_cc_mdss_mdp_lut_clk | 150000000 |
| disp0_cc_mdss_vsync_clk | 19200000 |
| disp0_cc_mdss_ahb_clk | 19200000 |
| disp0_cc_mdss_dptx3_aux_clk | 19200000 |
| disp0_cc_mdss_dptx3_link_clk | 270000000 |
| disp0_cc_mdss_dptx3_link_intf_clk | 67500000 |
| disp0_cc_mdss_dptx3_pixel0_clk | 76875000 |

The reported stream pixel clock is half the mode pixel clock. That observation
alone does not establish the hardware's pixels-per-clock encoding. A performance
trace requests core rate 149022720; distinguish that requested value from the
150000000 clock summary. [CLK, FLIP]

## 5. Interrupt hierarchy and cadence

MDSS0's parent interrupt cells are `<0 0x53 4>`. DPU's child interrupt is `<0>`
with MDSS0 as interrupt parent; the eDP controller's child interrupt is `<0xf>`.
These are device-tree interrupt specifiers, not Linux IRQ numbers. [DT]

The runtime interrupt table identifies Linux IRQ 212 as domain `msm_mdss`,
child 0, action `msm`. Child 15 is Linux IRQ 216, action `dp_display_isr`.
The latter stays at count 96 across all three runs; it is not the observed
per-frame interrupt source. [IRQ]

| Run | IRQ 212 before | After | Delta |
| --- | ---: | ---: | ---: |
| idle | 4287 | 4363 | 76 |
| motion | 4563 | 5740 | 1177 |
| flips | 10765 | 11959 | 1194 |

Snapshots bracket more than the trace interval; do not equate these deltas to
exact trace event counts. The original collector's word-boundary matcher missed
`msm_mdss`, leaving IRQ candidate lists empty. Thus generic IRQ entry/exit
tracepoints were not enabled. DPU callback traces and these counters remain valid.

The video-mode vblank callback is registered as DPU IRQ tuple `[0, 23]`; underrun
callback as `[0, 22]`. These labels do not yet establish interrupt-register
offsets, bit positions, or clear semantics. [FLIP, first registration sequence]

In the controlled run, 1186 `drm_vblank_event` records for crtc=0 contain 1184
adjacent sequence pairs. Their payload timestamps have a median interval of
16.703721 ms (about 59.8669 Hz), minimum 16.690621 ms, maximum 16.716965 ms.
Use the event's `time` payload, not its trace-line emission timestamp. The motion
run includes a consecutive-sequence interval of 33.419689 ms, so do not assume
every software sequence increment represents exactly one physical refresh.
[FLIP, MOTION]

The idle trace enables/disables vblank reporting and video IRQ callbacks around
sporadic work. Sparse idle events do not imply that panel refresh stops. [IDLE]

## 6. Observed flip transaction

The recorded command is `modetest -M msm -D /dev/dri/card0 -s 39@88:1920x1200 -v`.
The logs report 19 frequency samples (59.81 through 60.21 Hz); exit -15 is the
collector's intended termination. The snapshot sysfs names say card1, while the
command records card0. The successful trace establishes the test ran; do not
use this discrepancy to prescribe a permanent device path. [TEST, MODE]

The stable part of the test alternates scanout `plane_addr[0]` values 4096
(0x1000) and 10268672 (0x9cb000), each observed 590 times. A transition address
1052672 (0x101000) appears once. These are trace-reported display addresses,
not proven CPU physical addresses. Each layout reports 9216000 bytes. [FLIP,
`dpu_plane_set_scanout` runtime schema]

A concrete first transaction, using trace timestamps in seconds: [FLIP]

```
710.435154  set scanout address 1052672
710.435522  trigger flush: VIDEO, intf_idx=6, ctl_idx=1, mask=0x200c1
710.450455  vblank event: crtc=0, seq=10767, payload time=710450330647 ns
710.450479  encoder frame-done callback
710.450998  complete commit: id=88
710.451065  complete flip: id=88
710.451072  deliver vblank event: seq=10767
```

This establishes the observed software sequence, not the exact instant at which
a hardware register latched. `pending_flush_ret=0x200c1` appears 1182 times;
transition values 0x200200c1 and 0x200c0 appear once each. Motion commonly uses
0x10200c1 while also updating a cursor plane. The differing bit is a correlation,
not an established bit definition. [FLIP, MOTION]

No register write addresses or values were traced in pass 1. In particular, do not turn
the pending flush mask into a programming constant without locating its register
and confirming how pending, active, and completion state interact.

## 7. Implementation contract (design, not measured hardware)

*Superseded for the inherited panel pipeline by the measured contract in 18.1.*

PureDarwin needs distinct display-mapped front/back buffers, a render-completion
dependency with display-visible writes, a synchronized display commit, and a
completion tied to that commit. The old front remains unavailable until the
new scanout is confirmed active. Timeout does not imply buffer retirement.

The two-mixer routing must be accounted for if retaining this Linux configuration.
Do not assume firmware's framebuffer uses the same routing, format modifier,
IOMMU domain, or clocks. This test uses modetest buffers and does not validate
Adreno render fences, cache maintenance, or GPU/display address sharing.

## 8. Missing evidence and next observation

*Status of each item: see 18.2.*

Still required before register-level implementation:

1. Display source-address/stride/format register offsets and block bases.
2. CTL flush/update/latch operations and an unambiguous commit-completion test.
3. MDSS and DPU interrupt status/enable/acknowledge offsets and semantics.
4. Display SMMU stream matching, context-bank state, page-table configuration,
   and a mapping strategy compatible with firmware handoff.
5. Power/clock retention and firmware display state before Linux reprograms it.
6. Render-fence visibility, delayed rendering, missed IRQ, and teardown tests.

[INV] exposes `kms`, `debug/core_irq`, `encoder-2/status`, `crtc-0/status`,
`eDP-1/dp_debug`, and per-SSPP debug directories. A targeted second capture can
read driver-mediated display diagnostics while the panel is active, plus the
corrected generic IRQ trace. Inventory their output before designing further
instrumentation; do not recursively read arbitrary debugfs files.

The kernel advertises `rwmmio` read/write events. Their presence alone does not
identify the address space or permit a display-only filter. First collect their
runtime formats. Do not enable unrestricted MMIO tracing: it could capture GPU
firmware traffic and irrelevant devices. Physical resource windows above cannot
be assumed to match trace fields containing kernel virtual addresses.

## 9. Pass 2: actual IRQ attribution and display register snapshot

Archive: `x13s-display-results-2.tar.gz`, SHA-256
`b1595d330ca18e1d8aaeb7df055c4549ad6d0d819bc78907aa509a97f9c9920b`.
All 786 manifested files verify, including 260 new files in `04-details` (56)
and `05-flips-irq` (204). Every new recorded command returns zero. The IRQ trace
has no overruns, dropped events, or enable errors. No visual notes were supplied.

Additional evidence labels, relative to this second archive:

* [DETAIL] `04-details/ae01000.display-controller__*` diagnostics.
* [KMS2] `04-details/ae01000.display-controller__kms`.
* [IRQ2] `05-flips-irq/trace.txt`, its schemas and before/after interrupt tables.
* [MMIOFMT] `04-details/rwmmio__*__format.txt`.

### 9.1 Link and pixel format

The panel diagnostic identifies `R133NW4K-R0`. The eDP diagnostic reports two
negotiated lanes, link rate 270000, link clock 270000000 Hz, bandwidth code 10,
and output bpp 24; output-bpc reports `Maximum: 8`. This distinguishes runtime
lane count from the four lanes declared in DT. [DETAIL: eDP-1 diagnostics]

Horizontal porch/sync values are 24/48/88; vertical values 3/12/17, agreeing with
the mode timing arithmetic in section 3. The diagnostic prints `active_low=0x0`
while modetest reports negative sync flags. Retain that discrepancy; do not infer
polarity register bits from these differently represented fields. [DETAIL, MODE]

The desktop at the register snapshot uses GNOME's XR30 framebuffer, modifier
0x500000000000001, pitch 7680, GEM size 10485760, through `sspp_0`. The earlier
modetest path is linear XR24. The KMS dump is therefore not a register recipe
for a linear framebuffer. [KMS2, embedded DRM state; DETAIL: crtc-0/status]

Encoder status reports interface 5, VIDEO mode, vsync count 13643 and underrun
count 0. Core IRQ status reports tuple `[0,23]`, count 13643, callback value 0.
These are sequential snapshots; the callback value does not establish an always
unregistered handler or an interrupt mask value. [DETAIL]

### 9.2 IRQ delivery, including cross-CPU completion

The new trace has 1201 IRQ 212 entries, each matched by an exit reporting
`handled`. All 1200 DRM vblank events occur between entry and exit of IRQ 212.
No traced entries for selected IRQs 214, 215, or 216 occur. This directly ties
the observed vblank path to the MDSS child-0 action, rather than the eDP child-15
action. Before/after IRQ 212 counts are 13998 and 15204 (delta 1206, including
the surrounding snapshot interval). [IRQ2]

The 1199 adjacent-sequence timestamp intervals have median 16.703467 ms,
minimum 16.689588 ms, maximum 16.716781 ms. Two linear-buffer scanout addresses
0x1000 and 0x9cb000 appear 598 times each. No enabled timeout, underrun, or
failure trace event is observed. This is a successful ordinary flip run, not an
error-injection test. [IRQ2]

One transaction demonstrates why software must synchronize shared state rather
than assume completion occurs after the interrupt handler returns: [IRQ2]

```
1232.456025 CPU0 IRQ 212 entry
1232.456065 CPU0 DRM vblank seq=14000 (payload time=1232456020499 ns)
1232.456100 CPU0 encoder frame-done callback
1232.456116 CPU7 commit waiter resumes
1232.456189 CPU7 complete flip
1232.456195 CPU7 deliver event seq=14000
1232.456225 CPU0 IRQ 212 exit
```

### 9.3 Block-relative register observations

`kms` yielded a 168012-byte driver-mediated DPU register/state dump with 83
register sections and one DRM state section. Its header says `dpu devcoredump`;
it was obtained by reading the KMS diagnostic, not a GPU dump, and the header
alone is not evidence of a display crash. Offsets restart at zero in each named
block. Physical block bases are not printed. [KMS2]

Selected observations below are values at byte offsets within those blocks.
Candidate meanings are deliberately separate from established values:

| Block | Offset(s) | Value | Interpretation status |
| --- | --- | --- | --- |
| sspp_0 | 0x00, 0x0c | 0x04b00780 | Matches packed 1200/1920 dimensions; field semantics unproven |
| sspp_0 | 0x24 | 0x00001e00 | Equals pitch 7680; candidate stride field |
| sspp_0 | 0x14, 0xa4 | 0x026d5000 | Repeated address-like value; active/shadow/readback roles unknown |
| sspp_0 | 0x1c, 0xac | 0x026cb000 | Second repeated address-like value; role unproven |
| sspp_0 | 0x30 | 0x800277ff | Format-related candidate only; no bit definition |
| lm_0, lm_1 | 0x04 | 0x04b003c0 | Matches 1200/960 dimensions |
| lm_0 | 0x00 | 0x00000002 | Observed only |
| lm_1 | 0x00 | 0x80000002 | Observed only; differing high bit not yet defined |
| ctl_0 | 0x00, 0x04 | 0x01000002 | Observed only |
| ctl_0 | 0x18 | 0x00000000 | Not proof that this is a flush or status register |
| intf_5 | 0x00 | 0x00000001 | Observed only |

The address-like values differ by 0xa000 (40960), consistent with the additional
40960-byte layout region seen in compressed desktop traces in pass 1. This is
cross-run correlation, not proof of an address register or metadata format.
The dump is sequential and may mix moments in time. It does not establish write
ordering, latch conditions, clear-on-read behavior, or safe register replay.

### 9.4 Next experiment: filtered access observations

The MMIO schemas expose `caller`, `caller0`, `addr`, `width`, and (for writes
and post-reads) `val`; `addr` is an unsigned-long field. The schema alone does
not translate it to a physical address. [MMIOFMT]

The collector's opt-in MMIO mode now derives a virtual-address allowlist from
live `/proc/vmallocinfo` ioremap records and verifies MDSS/DPU physical windows
against this machine's runtime DT. It traces only writes and post-reads in those
windows with width 32. It saves the mapping evidence and installed filters,
and refuses tracing if mappings or filters cannot be established. There is no
unfiltered fallback and no direct MMIO access. Neither SMMU nor GPU windows are
included. Mapping metadata format is documented in the kernel's
[proc interface documentation](https://kernel.org/doc/html/v6.7/filesystems/proc.html).

This should supply physical access offsets and ordering for steady flips and
IRQ acknowledgement if this kernel instruments these accesses. Empty MMIO
output is not proof that the device was not accessed. The later SMMU and
firmware-handoff work in section 8 remains necessary.

## 10. Pass 3: physical register accesses during linear flips

Archive: `x13s-display-results-3.tar.gz`, SHA-256
`4c656ee4b00ba963b073756da7fb622602490909200d6d6fff3370932ea1e732`.
All 996 manifested files verify, including 210 in the new `06-flips-mmio` run.
All eight CPU trace buffers report zero overrun, commit-overrun and dropped
events. Both installed MMIO filters match the recorded expression. No trace
enable error is reported. The run contains 45998 parsed 32-bit MMIO accesses:
32716 writes and 13282 post-reads, across 101 physical addresses.

[MMIO3] below denotes `06-flips-mmio/trace.txt`, translated with
`display-mmio-mappings.json` from this archive. Caller labels are emitted by
runtime tracing; no driver source or disassembly was used. The histogram was generated after checking the manifest and mapping each MMIO record to a physical address.

### 10.1 Address translation and scope

| Window | Physical start | Kernel virtual start in this run | Allowed size |
| --- | --- | --- | --- |
| DPU | 0x0ae01000 | 0xffff800085600000 | 0x8f000 |
| MDSS | 0x0ae00000 | 0xffff8000811e9000 | 0x1000 |

For each access, physical = physical start + (reported address - virtual start).
Virtual addresses are ephemeral and must never become driver constants. The
filter excludes page-rounded excess from the ioremap allocations. VBIF, eDP,
PHY, clocks, GPU, and SMMU registers are outside the trace. [MMIO3 mappings]

### 10.2 Primary scanout registers

The following physical writes occur 298 times each. The high-level scanout
events and runtime caller labels identify the source-address/rectangle/format
operation. These are observed settings for the tested linear XR24 pipeline;
they do not define every field or establish a minimal set of writes. [MMIO3]

| Physical address | DPU offset | Observed write | Observed use |
| --- | --- | --- | --- |
| 0x0ae05000 | 0x4000 | 0x04b00780 | Rectangle dimensions, 1920x1200 packed |
| 0x0ae05008 | 0x4008 | 0 | Rectangle setup |
| 0x0ae0500c | 0x400c | 0x04b00780 | Rectangle dimensions |
| 0x0ae05010 | 0x4010 | 0 | Rectangle setup |
| 0x0ae05014 | 0x4014 | 0x00acb000 or 0x00001000 | Source plane-0 address |
| 0x0ae05018 | 0x4018 | 0 | Remaining source address slot |
| 0x0ae0501c | 0x401c | 0 | Remaining source address slot |
| 0x0ae05020 | 0x4020 | 0 | Remaining source address slot |
| 0x0ae05024 | 0x4024 | 0x1e00 | Pitch 7680 |
| 0x0ae05028 | 0x4028 | 0 | Additional stride setup |
| 0x0ae05030 | 0x4030 | 0x236ff | Linear XR24 format setup |
| 0x0ae05034 | 0x4034 | 0x03020001 | Linear XR24 format setup |
| 0x0ae05038 | 0x4038 | 0x80000000 | Read then same-value write during format setup |

Each of the two source addresses is written 149 times. They differ from earlier
allocations; buffer addresses must be allocated/mapped, not hard-coded. Correlating
these accesses with the pass-2 block-relative dump places `sspp_0`'s source block
at physical 0x0ae05000. Pass 2's compressed XR30 format value 0x800277ff at +0x30
must not replace this run's linear XR24 value. The relation between primary and
duplicate address locations at +0xa4/+0xac is still unproven: this run writes
the +0x14/+0x1c locations. [MMIO3, KMS2]

### 10.3 Flush request and observed completion

CTL0's flush access is physical **0x0ae16018**, DPU offset 0x15018, consistent
with the `ctl_0` dump's +0x18. The ordinary request value is **0x200c1**
(299 writes). Two transition requests are 0x200200c1 and 0x200c0, once each.
Do not assign meanings to individual mask bits from this experiment alone.
[MMIO3]

The register is read back as the pending mask after the request, then as zero
in the vblank callback before frame completion. Across the capture it returns
0 on 1205 reads, 0x200c1 on 598 reads, and each transition mask on two reads.
There are no traced zero writes to this register. This supports hardware
consumption/clearing of the request; it does not prove a generic self-clearing
register model or an exact latch edge under every mode. [MMIO3]

One complete transaction, trace time in seconds, with physical addresses:

```
1690.443664 W 0x0ae05014 = 0x00acb000   source buffer
1690.443758 W 0x0ae05024 = 0x00001e00   pitch
1690.444849 W 0x0ae16018 = 0x000200c1   request flush
1690.444884 R 0x0ae16018 = 0x000200c1   still pending
1690.450794 R 0x0ae00010 = 0x00000001   MDSS parent status
1690.450806 IRQ 212 entry
1690.450862 R 0x0ae01014 = 0x00800100   DPU status
1690.450900 R 0x0ae01010 = 0x00c00000   enabled mask
1690.450937 W 0x0ae01018 = 0x00800100   acknowledge observed status
1690.451055 DRM vblank seq=17100
1690.451102 R 0x0ae16018 = 0            request consumed
1690.451107 encoder frame-done callback
1690.451525 complete flip
1690.451558 deliver event seq=17100
```

The capture also contains blendstage, fetch-pipe, format and other programming
before each flush. It does not prove that changing only address plus flush is
sufficient. That proposed minimal flip sequence requires a controlled test.

### 10.4 Interrupt registers and acknowledgement sequence

The repeated access pattern identifies this register group for the observed
vblank path: [MMIO3]

| Physical address | DPU offset | Observed role and values |
| --- | --- | --- |
| 0x0ae00010 | MDSS +0x10 | Parent status: 1 for DPU dispatch; 0x8000 for eDP dispatch |
| 0x0ae01010 | +0x10 | DPU enable mask: writes 0x800000 then 0xc00000; reads 0xc00000 |
| 0x0ae01014 | +0x14 | DPU status: 302 reads of 0x800100 |
| 0x0ae01018 | +0x18 | Acknowledge/clear write: 302 writes of 0x800100 |

Callback registration for `[0,23]` writes 0x800000 to +0x18 before writing
0x800000 to +0x10. Registration for `[0,22]` writes 0x400000 to +0x18, then
0xc00000 to +0x10. Combined with the callback labels from earlier passes, this
supports bit 23 for the tested video vblank and bit 22 for underrun enable.
There is no observed underrun firing to validate its error handling. [MMIO3]

On each DPU dispatch, Linux reads status, reads the enable mask, writes the
raw status back to the acknowledge address, then invokes the vblank callback.
Note that 0x800100 contains bit 8, which is absent from the enabled mask. Thus
the observed acknowledgement is **not** just `status & enable`. Bit 8 remains
unidentified. The sequence is consistent with write-one-to-clear status, but
isolated bit clearing/readback tests have not established all semantics.

Other banks are serviced in the same handler: +0x0c reads 0x200, +0x08 reads
enable 0, and +0x2c is written 0x200; interface physical 0x0ae3a1c4 reads 0xa00,
0x0ae3a1c0 reads enable 0, and 0x0ae3a1c8 is written 0xa00. Do not mistake
these raw nonzero status values for enabled errors, or ignore other banks in
a general interrupt implementation. No writes to MDSS +0x10 are observed.
[MMIO3]

Interface line count is read at physical 0x0ae3a0b0, DPU offset 0x390b0,
consistent with the `intf_5` dump's +0xb0. Its runtime caller and changing values
identify it as line-count observation. Counter origin, rollover and any
timestamp correction formula are not established by this spec. [MMIO3, KMS2]

### 10.5 Quality, timing impact and implementation boundary

There are 302 IRQ 212 entries and 302 DRM vblank events, plus two IRQ 216 eDP
entries. All 301 adjacent vblank payload intervals have median 16.703256 ms,
minimum 16.610154 ms, maximum 16.804345 ms. No enabled underrun/failure/timeout
event occurs. The five-second setting produced a longer enclosing trace because
snapshot collection and workload shutdown are included. Do not use this heavily
instrumented run to estimate normal IRQ service latency. [MMIO3]

The display-side specification now has observed physical addresses for source
updates, flush polling, vblank status/enable/acknowledgement, and line count.
This is enough to design a pending-commit state machine and targeted hardware
tests. It is **not** yet sufficient for a safe firmware framebuffer takeover:

* Firmware scanout routing and modifier may differ from Linux's two-mixer setup.
* Display SMMU mapping/context and GPU-to-display visibility remain unspecified.
* The minimal address-only update and flush mask need an isolated test.
* IRQ registration/routing through the platform, preservation of unrelated mask
  bits, shutdown, missed interrupts and render-fence failures need validation.

A proposed first driver should retain a known configuration, pin both buffers,
wait for rendering, publish a pending commit before arming the flush, and complete
only on the appropriate vblank with its pending flush consumed. This is a design
derived from the observations, not a claim of a tested PureDarwin implementation.
No further broad desktop trace is needed for the register-address questions;
the next experiments should target firmware handoff and the display SMMU.

## 11. Linux display IOMMU ownership

Archive `x13s-smmu-retry.tar.gz`, SHA-256
`cdb1a4e8c377bf2dc4ff4f9e4c40f448f85ea8c08b48dc944c6394b46e91c8f6`.
All 45 manifested files verified. Paths below are relative to `07-smmu-retry/`.

Linux attached MDSS0 (ae00000.display-subsystem) to group 7 at boot time
0.592440 s. The group has one member. Its IOMMU link resolves to the system
SMMU at 0x15000000. The child DPU reports active and has no separate group
link in the collector output. Group 7 reports type `identity` and one MSI
reservation at 0x08000000–0x080fffff inclusive. The boot log reports a
system-wide default of Translated. These values describe different scopes.
Sources: `iommu-topology.json` and `baseline/dmesg.txt`.

The group type describes Linux's default domain. Establishing the domain
used for an individual scanout requires an additional observation: correlate
a live framebuffer's backing physical pages with its programmed display
address, or inspect the active stream route and translation. The low addresses
in section 10 alone cannot distinguish these cases. In particular, group 7's
membership supplies no evidence that the system SMMU is exclusive to MDSS.

The SMMU provider reports runtime PM unsupported, while MDSS and DPU both
report active. The collector stopped before register access. SMR/S2CR values,
bank selection and firmware translation remain open. The same boot log shows
simpledrm initialized at 0.184309 s, providing a pre-MSM framebuffer milestone.
Its initialization message contains no scanout address or translation state.

## 12. PureDarwin inherited scanout at probe

Source: owner-supplied `pd-display`, SHA-256
`120592d956c93a6f18648ec9216874501f895093d4b5db906bad29cadf8655e3`.
At 25.072 s, the console reports physical 0xc6200000, 1920×1200, pitch 7680,
depth 32. SSPP0 +0x14 contains the same address. The three remaining address
slots are zero. Both rectangle sizes encode 1920×1200, with zero origins.
Pitch is 0x1e00. The format tuple at +0x30/+0x34/+0x38 is
0x237ff/0x03020001/0x80000000. Its first word differs by 0x100 from Linux's
tested XR24 setup. Preserve inherited format until the field is identified.

CTL0 +0x18 reads 0x20000000 in this single sample. Test the submitted bits
when investigating flush consumption and establish what happens to this
inherited bit before using whole-register zero as a completion condition.
INTF5 line count is 0x10b.

System SMMU IDR0/1/2 are 0x4c017ea2, 0x6000006e and 0x00005111. sCR0 is
0x00260406. The group count field is 162. The sampler's 128-group limit
produced END status=-1 before reading any routing entries. Identical numerical
addresses for the front buffer are compatible with either bypass or identity
translation of that allocation. Accessibility of a second buffer remains open.
The revised sampler inspects groups 0–127 and signals partial coverage with
status 1. A missing match in that prefix leaves the stream route unresolved.

## 13. Inherited MDSS stream route

Source: owner-supplied `pd-display-again.txt`, SHA-256
`abc8b4fbf0e94bc669258e525a2b282b305013326eada09a31031aa5af3221e1`.
Both probe markers are present. END status 1 denotes coverage of entries 0–127
of 162. One valid match was reported in that prefix.

| Register | Address | Value | Interpretation |
| --- | --- | --- | --- |
| SMR4 | 0x15000810 | 0x84021000 | Valid, SID 0x1000, mask 0x402, exactly the MDSS DT set |
| S2CR4 | 0x15000c10 | 0x00000004 | Translate route selecting context bank 4 |
| CBAR4 | 0x15001010 | 0x0001f000 | Stage 1 context with stage 2 bypass |
| CBA2R4 | 0x15001810 | 1 | 64-bit context |
| CB4 SCTLR | 0x15084000 | 0xe0 | M (bit 0) clear |

Both TTBRs read zero (low and high halves). The route selects a bank, but its
stage 1 translation is disabled. Under the architectural interpretation of
these registers, this route passes the input address through. That agrees with
the console physical address and programmed source address. The capture only
demonstrates scanout of the existing front buffer. The unsampled entries remain
unknown. Register interpretation follows Arm IHI 0062D.c, S2CR, CBAR and
context-bank SCTLR.

For an initial flip experiment preserve this route and bank configuration. Use
a pinned, physically contiguous second buffer addressable by the observed 32-bit
source register, matching the inherited pitch and pixel layout. First test
two static images and restoration of the console. Record the source write,
flush consumption, vblank sequence, any faults or underruns, and visible result
before treating a buffer as reusable. GPU rendering adds a separate producer
fence and cache-visibility requirement.

The first PureDarwin checkerboard run reported
`pd-flip SKIP no suitable contiguous buffer`. It passed the inherited-state
checks and returned before writing the source address or CTL flush register.
The original message grouped allocation, write-combined mapping and address
validation failures. It leaves second-buffer fetch and flip completion untested.

A second PureDarwin run returned `low32 allocation failed bytes=9216000
low16K=available`. The full-frame request failed and the single-page request
succeeded at probe time. The physical address of that page was not logged.
No display write was submitted. The cause of the full-frame failure is open.

## 14. PureDarwin visible commit on the inherited framebuffer

Source: owner-supplied `flip-ud.txt`, SHA-256
`f26fd0b07683fbf1646ec14161712657671d8c0c12fdc40c08ec77a35535e3a6`.
The operator saw the internal panel image turn upside down. Both commits used
the inherited framebuffer and wrote the CTL0 flush request mask 0x000200c1.

| Operation | SSPP register 0x0ae05038 written and read back | CTL0 flush first → final read | Scanline wraps |
| --- | --- | --- | --- |
| Invert vertically | 0x80004000 | 0x200200c1 → 0x20000000 | 2 |
| Restore | 0x80000000 | 0x200200c1 → 0x20000000 | 2 |

Both software checks passed. Test the requested bits for clearance with
`(flush & 0x000200c1) == 0`. Waiting for the whole register to reach zero
would miss the observed completion state. The poll loop waits for two wraps
as well as mask clearance, so these samples do not locate the latch edge or
measure flush latency. Visual confirmation covers inversion. The restore is
confirmed by register readback and mask clearance. Display IRQ delivery,
retirement of an old scanout allocation and release-fence timing still need
independent PureDarwin tests.

The first PureDarwin IRQ run reported `SKIP GIC interrupt controller
unavailable` and setup status 0xe00002d8 (kIOReturnNotReady). The probe
checked a readiness resource derived from a device-tree phandle and returned
before registering INTID 115 or enabling DPU bit 23. This run provides no
measurement of vsync delivery.

## 15. PureDarwin vsync interrupt delivery

Source: owner-supplied `vsync-route.txt`, SHA-256
`d6715024bddf455f169e3d3526c22834b11a9a3db7649986655d3f02f24df448`.

On the X13s internal panel, MDSS SPI 83 (GIC INTID 115) delivered 24 callbacks
with MDSS parent bit 0 and DPU status bit 23 set during 24 observed scanline
wraps. The handler acknowledged bit 23 on each hit. The probe found the GIC
registered and ready immediately. The DPU enable mask and MDSS parent status
both started at zero. The unexpected-interrupt count was zero, arm and detach
returned success, and the final DPU enable mask was zero.

These aggregate counts establish vsync delivery on inherited scanout. This run
used a fixed scanout address and recorded no per-interrupt flush samples.
The ordering of flush consumption, buffer retirement and fence signaling
remains open.

## 16. PureDarwin flush consumption per vsync, and the first translated-bank run

Sources: owner-supplied `x13s_dpu/flip4.txt`, `flip5.txt` and `flip6.txt`,
SHA-256 `02c568b25d3cfac91774e52c021b0652707bc4e4dec6296e27e867fb113abc3c`,
`932fe57dc4f52b42f79af53cc41280946de89a384988de485d5139e531c6a381` and
`aa8c05b3477eeba7bf49a58d88f79a621cb38fcbe18fc7cdf89851fe940771c3`.
No visual notes were supplied with these logs.

### 16.1 Inversion with the flush sampled in each callback (mode 4)

Each commit was written just after a vsync callback. The baseline callback
sampled CTL0 flush 0x20000000 at line 1.

| Commit | Line at submit | Flush after write | First callback with 0x200c1 clear | Time to it | Its sample |
| --- | --- | --- | --- | --- | --- |
| Invert (+0x38 = 0x80004000) | 2 | 0x200200c1 | 1 | 16642 µs | flush 0x20000000, line 1, status 0x00800100 |
| Restore (+0x38 = 0x80000000) | 3 | 0x200200c1 | 1 | 16635 µs | flush 0x20000000, line 1, status 0x00800100 |

Neither commit straddled a callback. The thread's own 50 µs poll first saw the
mask clear 25 µs after that callback's timestamp, at line 3. A request written
at the start of a frame is consumed by the next vblank. The callback raised by
that vblank already observes the request clear. The inherited bit 29 survives
consumption, consistent with section 14. The run took 126 callbacks with
unexpected 0, and the enable mask returned to 0 with detach 0.

These two commits were submitted near line 0. Consumption for requests written
late in a frame is not measured; mode 5 sweeps the submission phase.

### 16.2 Context bank 4 under translation (modes 5 and 6, first run)

Bank 4 before any write: SCTLR 0xe0, TCR2 0x60, TCR 0, MAIR0/1 0, TTBR0 0,
FSR 0x400. High-RAM allocations succeeded: B 0x109c00000, C 0x10a800000 (both 2 MB aligned), and
tables at 0x108e58000 (mode 6: 0x10906c000).

With the console identity-mapped and SCTLR 0x1127 written, ten vsync callbacks
arrived. The test then stopped before any flip. Its abort condition treated any
nonzero FSR as a fault, and FSR read 0x400 both before and after. FSR bits 10:9
are the read-only FORMAT field (Arm IHI 0062D.c, CB_FSR), and 2 denotes
AArch64 descriptors. This value is not a fault. The FAR, FSYNR0 and
CBFRSYNRA values logged with it (FAR 0x44902697afab and 0x44982615af8b) are
uninitialized register contents, not evidence. Both runs restored SCTLR 0xe0
and TTBR0 0, left the source address at 0xc6200000, and detached cleanly.

What this run does and does not show:

* No fault bit (FSR & 0xc00001fe) was set during ten frames with translation
  written on. The run did not read SCTLR back after enabling. It therefore
  does not prove that the bank was translating, only that no fault was raised.
* No flip took place. Second-buffer fetch, retirement and the negative control
  remain untested.

The test now masks FSR with the fault bits and logs the bank state after the
enable. Repeat modes 5 and 6 with it.

### 16.3 Two-buffer flip with retirement traced by the SMMU (mode 5, rerun)

Sources: owner-supplied `x13s_dpu/flip5.txt` (rerun), SHA-256
`75fc3593056a874a3ea011c0008c12c2ce58738ec0ca34254cb1ed7ef887a9ff`, and
`flip6.txt` (rerun), SHA-256
`d5d49d2c73fc91905f2ec7514a8def5603be026063e8f49bf750ae903cae74c9`.
The supplied mode 5 excerpt starts at the first flip. Its setup lines
(BEGIN, tables, bank state) are absent. The mode 6 log from the same build
shows the bank state after enable:

| Register | Written | Read back |
| --- | --- | --- |
| SCTLR | 0x1127 | 0x127 (bit 12 did not stick) |
| TTBR0 | ASID 4, table 0x108e54000 | 0x0004000108e54000 |
| TCR | 0x00800020 | 0x00800020 |
| TCR2 | 0x00038011 | 0x00038071 (inherited bits 6:5 remain set) |
| MAIR0 | 0x44 | 0x44 |

With M set and the console identity-mapped, ten frames raised no fault bit
(FSR & 0xc00001fe = 0).

Forty flips alternated between B (phys 0x109c00000, iova 0xd0000000) and
C (phys 0x10a800000, iova 0xd1000000). They started from the console A
(iova = phys 0xc6200000), and a 41st flip returned to A. Each flip wrote
only the SSPP0 address (+0x14) and the CTL0 flush request 0x200c1. The old
buffer's mapping was removed 5–63 µs after the first callback whose sample
had the request clear. Fault status was read two frames later.

* All 41 commits: `clear_cb=1`, no straddle, flush readback 0x200200c1.
* Submission sweep: the INTF5 line at submit ranged from 2 to 1215. The
  sum of phase and time-to-callback stays 16.6–16.7 ms. A request written
  at line 1215, 237 µs before the callback (n=14; n=28: line 1208, 339 µs),
  was still consumed at that vblank. No submission in this run landed later
  in the frame.
* Faults: 0 across all 41 retirements. The final state: bank 4 restored
  (SCTLR 0xe0, TTBR0 0), source 0xc6200000, 182 callbacks, unexpected 0,
  enable 0, detach 0.

Operator's visual report: the moving-bar sequence showed no tearing or
flashing. The buffer-identifying stripe alternated colour on each flip, as
designed. The console returned.

### 16.4 Negative control: unmap before the vblank (mode 6)

Three flips unmapped the old buffer immediately after the flush write. Each
produced a translation fault, FSR 0x80000002 (MULTI and TF), FSYNR0 0x20022,
with FAR inside the old buffer:

| Flip | Line at submit | FAR | Buffer offset | Source row (offset / 7680) | Stream |
| --- | --- | --- | --- | --- | --- |
| A→B | 2 | 0xc6220c00 | 0x20c00 | 17 | 0x1000 |
| B→C | 94 | 0xd0094e00 | 0x94e00 | 79 | 0x1000 |
| C→B | 186 | 0xd1141f00 | 0x141f00 | 171 | 0x1400 |

Totals: faults 3, all in the old buffer, none elsewhere. The restore flip B→A
retired cleanly (fault 0). Every commit had `clear_cb=1`. The run ended with
bank 4 restored, source 0xc6200000, unexpected 0 and detach 0.

Operator's visual report: no glitch was noticed. The run looked like a
much shorter mode 5. Each fault terminated fetches for part of at most one
frame, and that was not perceptible to the observer. This report does not
establish what the DPU emitted for the terminated reads.

The tracer therefore detects display fetches of an unmapped buffer. This gives
the zero-fault result of 16.3 evidential weight. The first fault records
the scanline being fetched at the time the mapping was removed. In
flips 2 and 3 the faulting row is exactly 15 below the INTF5 line count at
submission. This suggests a fixed offset between that counter and the
fetched source row. Flip 1's submission at line 2 precedes active fetch,
so it does not test that offset. Stream IDs 0x1000 and 0x1400 both occur
within MDSS's SMR4 set (mask 0x402). The assignment of streams to
fetch paths is not established.

### 16.5 Conclusions for the commit contract

Observed for linear XR24 1920×1200 scanout through SSPP0/CTL0 on the
inherited pipeline, with submission anywhere from line 2 to line 1215:

1. A source-address change plus the CTL0 request 0x200c1 is enough to flip
   between buffers. No blend, format or rectangle rewrite was needed for this
   pipeline.
2. The request is consumed at the next vblank. The vsync callback raised at
   that vblank already samples the requested bits clear (41/41, 2/2 in mode 4).
3. Once that callback has observed the bits clear, the old buffer was not
   fetched again: 41 retirements, 0 faults. A buffer unmapped before that
   point is fetched (3/3 faults).

Not yet measured: submissions in the last ~237 µs before vblank, where the
latch could slip a frame. That window needs the `straddle`/`clear_cb=2` case.
Also unmeasured: GPU-rendered buffers (producer fence and cache visibility),
missed or late callbacks, and the ASIDPNE bit that did not stick (it is
irrelevant to a single-ASID bank).

## 17. Late commits, missed callbacks and GPU-rendered buffers

Sources: owner-supplied `x13s_dpu/flip7.txt`, SHA-256
`36ea7ca3336693458ed3a163edf6485b84aab85bbaac28ee46b0fa2e95d6e021`, and
`flip8.txt`, SHA-256
`81d33eb9bb1aaf6f947fc16567cc79910e73fd79799ec19a90df8726755debea`.
The mode 7 excerpt begins at flip 28: the setup lines (including the measured
counter top) and the first pass are absent; its summary table covers both
passes. Targets run from top−24 = 1208, so the counter's top was 1232.

### 17.1 Flush writes at the end of the frame (mode 7)

Each flip spun until INTF5 reached a target line, then wrote SSPP0's address
and the CTL0 request. The line read immediately before and after the write
matched the target in every flip shown.

| Target lines | Flips | Outcome |
| --- | --- | --- |
| 1208–1232 (last 25 counter values) | 50 | All consumed at the next vblank (`clear_cb=1`), 0 faults |
| 1, 2, 3 (after the wrap, after that vblank's callback) | 6 | All consumed one frame later (`cb_dt` 16.64–16.66 ms), 0 faults |
| 0 | 2 | Callback raced the write (`straddle=1`), see 17.2; 2 faults |

At line 1232 the request was written 10 µs before the callback's handler
entry and was still consumed at that vblank. Across 1208–1232, `cb_dt` falls
by 13–14 µs per line, reaching 10 µs at 1232. No late write slipped to the
following vblank: within the resolution of this test, the latch point
coincides with the counter wrap, and the callback for that vblank already
sees the request clear.

### 17.2 A callback concurrent with the write can falsely retire

Both target-0 flips submitted at line 1 as the callback for the wrap ran.
In the flip shown (n=54, C→B), the vsync count was read before the write, and
the callback incremented it during the submission (`straddle=1`). The
callback entered 12 µs before the submission timestamp (`cb_dt=-12`). Its
flush sample (0x20000000) predates the request. The test treated it as
consumption and unmapped C 16 µs later. The SMMU then reported a translation
fault in C at offset 0x18400 (row 12), FSR 0x80000002, stream 0x1000. The
summary records two faults, both in the old buffer, consistent with both
target-0 flips. The request was consumed one vblank later.

Consequence for the commit contract: a callback retires a commit only if its
flush sample was taken after the request write. Counting callbacks from a
sequence read before the write is insufficient. Section 16.5 item 2 holds
only under that ordering. The test now ignores callbacks whose handler
entered before the write completed (`stale` in the flip line); a rerun of
mode 7 should report `stale=1`, `clear_cb=2` and no fault for target 0.

### 17.3 Missed and late vblank callbacks (mode 7)

Six flips submitted at line 43–45, then held the DPU vblank enable off for
20 ms or 37 ms.

| Mask | Flips | Delivered callback after submit | Its line | Request | Faults |
| --- | --- | --- | --- | --- | --- |
| 20 ms (one vblank missed) | 3 | 21.010–21.014 ms | 365–366 | clear | 0 |
| 37 ms (two vblanks missed) | 3 | 37.50–38.01 ms | 353–391 | clear | 0 |

The status latched while masked was delivered as one callback, mid-frame, when
the enable returned. Two missed vblanks produced one callback, not two. A
callback count is therefore not a vblank count, and a callback's line need
not be near 1. Retiring at that late callback raised no fault. Late delivery
of a consumed request is safe for buffer reuse. Presentation timing must
not be derived from the callback count or time alone.

Mode 7 totals: 64/64 flips, 305 callbacks, unexpected 0, detach 0, bank 4 and
the console source restored.

### 17.4 GPU-rendered buffers (mode 8)

The test ran at the first GPU open (cold bring-up at 49.3 s, CP running and
zap loaded). B (phys 0x113400000) and C (0x114000000) were CPU-painted grey
with their stripe, cleaned to DRAM, and wrapped as GPU objects at GPU iova
0x100018000 and 0x100a18000. In each of 30 flips the GPU used CP_MEM_WRITE
to erase the target's previous 64-row green band and draw a new one
(116928 dwords without a previous band, 233856 with). The fence was waited
for before the flip.

* Fence: signalled for all 30 submits; 5.4 ms for one band, 10.7–10.9 ms
  for two.
* After the fence, an uncached CPU read of the target in DRAM found the new
  band at its first and last row, the stripe intact, and the erased band
  grey: 30/30 `check=ok`, 0 mismatches.
* The flips (phase swept 0–15.3 ms) all had `clear_cb=1`. Retiring at that
  callback gave 0 faults across 31 retirements, including the return to the
  console. Final state: bank 4 restored, source 0xc6200000, 167 callbacks,
  unexpected 0, detach 0.

GPU writes made with CP_MEM_WRITE are in DRAM when the fence signals, with no
CPU cache maintenance. A non-snooping display fetch through bank 4's
non-cacheable attribute reads them. Not tested: flipping before the fence;
render paths other than CP_MEM_WRITE (draws through CCU/UCHE, UBWC targets);
GPU-allocated buffers rather than wrapped CPU allocations.

### 17.5 Revised commit contract

For linear XR24 scanout through SSPP0/CTL0 with retirement traced by bank 4:

1. Wait for the producer's fence; then write the source address and the CTL0
   request 0x200c1.
2. The request is consumed at the first vblank after the write. In this test,
   writes up to the last counter line (1232) made that vblank.
3. Retire the old buffer at the first vsync callback that **entered after the
   write** and samples 0x200c1 clear. A callback concurrent with the write must
   not be used (17.2).
4. Callbacks may coalesce or arrive mid-frame when vblanks are missed. Reuse
   remains safe at the clear-sampling callback. Vblank counting needs another
   source.

### 17.6 Mode 7 rerun with stale callbacks excluded

Sources: owner-supplied `x13s_dpu/flip7.txt` (rerun), SHA-256
`a8827b87e8199fb37fc822825344b53250db67bb3aff048745eeee1a89241e22`, and the
same boot's `dmesg-boot.0.txt`
(`24ced7fd3e40445d4a97d81bc5b8332556a6e839c20df1b9ed86512b0bf2b575`) and
`dmesg-boot.1.txt`
(`040bf68db6655693b1b9a284dd4514126b4725c740cdf14476dd42d3c6269e61`).
The saved copies were taken from the same boot and carry identical timestamps.
Together they cover flips 6–63, the summary table and END. Setup lines and
flips 0–5 had already left the kernel message buffer.

* Three submissions straddled a callback: both target-0 flips (n=25, n=54)
  and one target-1 flip (n=55). Each reports `stale=1`, a stale sample of
  0x20000000, `clear_cb=2` at 16.66 ms, and **no fault**.
* Lines 1208–1232 again: every flip shown consumed at the next vblank,
  0 faults. The table again records `cb1=2` for every one of these targets.
* Masked vblanks: 20 ms gave one callback at 21.009 ms (line 364–365); 37 ms
  gave one at 38.009–38.012 ms (line 389–390); all clear, 0 faults.
* END: 64/64 flips, clear_cb histogram 62/3 (callback 1/2), faults 0,
  bank 4 and console restored, 298 callbacks, unexpected 0, detach 0.

Excluding callbacks whose handler entered before the write completed removes
the false retirement of 17.2. This supports contract item 17.5(3).

## 18. Driver readiness

This section summarises sections 10–17 for an implementer. It adds no new
observation. Each statement cites its evidence.

### 18.1 Contract for a first driver: page flips on the inherited panel pipeline

Scope: the X13s internal panel as firmware leaves it. That is 1920×1200,
pitch 7680, linear XR24 through SSPP0 and CTL0, and the format tuple
0x237ff/0x03020001/0x80000000 kept unchanged (12). There is no mode setting.
A driver must check this inherited state before writing anything, as the
experiments did (12, 13), and decline otherwise.

1. **Addressing.** XNU has no RAM below 4 GiB on the X13s, and SSPP0 takes a
   32-bit address. Scanout buffers are therefore reached through stage-1
   translation in MDSS context bank 4, which the inherited route already
   selects (13). The observed working bank state was SCTLR 0x127 (M, TRE,
   AFE, CFRE, HUPCF; bit 12 does not stick), TCR 0x00800020 (T0SZ 32,
   non-cacheable walks, EPD1), TCR2 read back 0x00038071, MAIR0 0x44
   (attribute 0 normal non-cacheable), TTBR0 with ASID 4 (16.3). Faults
   terminate rather than stall, and no fault interrupt is raised. The
   console framebuffer stays identity mapped at 0xc6200000 (16.3).
   Translation was enabled and disabled under live scanout without
   visible effect (16.3, 17.4).
2. **Producer.** Wait for the render fence. CP_MEM_WRITE output was in DRAM
   at the fence, with no CPU cache maintenance (17.4). CPU-written pixels
   need a clean to DRAM (16.3).
3. **Commit.** Write SSPP0 +0x14 (0x0ae05014) with the buffer's 32-bit
   iova, then CTL0 0x0ae16018 with 0x200c1. No other register was needed to
   flip between two buffers in the same layout (16.3). Bit 29 (0x20000000)
   stays set and is not part of completion (14).
4. **Latch.** The request is consumed at the first vblank after the write,
   including writes at the last counter line 1232, 10 µs before the
   callback (17.1).
5. **Retire.** The old buffer may be reused at the first vsync callback
   that entered after the commit's write completed and samples 0x200c1
   clear. That callback is the first after the latching vblank. A callback
   concurrent with the write must be ignored (17.2, 17.6). The SMMU tracer
   confirmed no fetch of the old buffer after this point in 200 retirements
   (41 in mode 5, 31 in mode 8, 128 in mode 7 over two runs; the two
   straddled retirements of 17.2 are excluded). Unmapping before the
   vblank always faulted (16.4).
6. **Interrupt.** Use MDSS SPI 83 (GIC INTID 115) with DPU enable bit 23.
   In the handler, read status at +0x1014 and acknowledge bit 23 at
   +0x1018 (10.4, 15). Missed vblanks coalesce into one later callback,
   possibly mid-frame. Such a callback still retires correctly (17.3), but
   callbacks do not count vblanks.
7. **Teardown.** Return to the console buffer, wait for its retirement,
   restore bank 4's saved registers (SCTLR first) and restore the DPU
   enable mask. This sequence restored the inherited state in every run
   (16.3, 17.6).

### 18.2 Section 8 items

| Item | Status | Evidence |
| --- | --- | --- |
| 1. Source address/stride/format registers | Resolved for SSPP0 linear XR24. The +0x30 field differing by 0x100 from Linux remains unidentified | 10.2, 12 |
| 2. Flush, latch and completion | Resolved for address-only flips (18.1 items 3–5). Individual mask bits remain unassigned | 14, 16, 17 |
| 3. Interrupt status/enable/acknowledge | Resolved for vblank bit 23. Underrun bit 22, status bit 8 and the other banks of 10.4 are not exercised | 10.4, 15 |
| 4. Display SMMU | Bank 4 route and working translation established. Routing entries 128–161 unsampled; ownership of the system SMMU among PureDarwin drivers undecided | 12, 13, 16.3 |
| 5. Power/clock retention and firmware state | **Open** | — |
| 6. Render fence, delayed rendering, missed IRQ, teardown | Fence visibility (CP_MEM_WRITE), missed/late IRQ and teardown established. Flipping before the fence and real draw paths not tested | 17.3, 17.4 |

### 18.3 Open items before a driver relies on this contract

Blocking:

1. ~~**Page-granular translation.**~~ Closed by 19.1: 4 KB level-3 mappings
   of discontiguous GEM chunks scan out correctly and retire without fault.
2. ~~**Bank 4 exclusivity.**~~ Closed by 20.1: of 162 groups only SMR4 (MDSS)
   routes to bank 4, and no bank translates at probe. The display driver should
   program only bank 4's context registers. Who owns the global SMMU
   registers is still a project decision.
3. ~~**Vblank sequence and timestamp source.**~~ Closed by 20.2–20.3: INTF5
   +0xac counts frames, including vblanks merged into one callback, and the
   line counter runs 1–1232 at about 13.53 µs per line. Origin and wrap of
   +0xac, and the timestamp formula, remain to be validated.

Robustness:

4. Power, clock and bandwidth-vote retention for MDSS: nothing in the tree
   can gate them (21.1). Runtime confirmation awaits the soak.
5. ~~Console visibility on panic.~~ Closed by 21.2: a KDP callout restores
   the console within one frame, provided the console stays mapped.
6. Long-duration flipping under GPU load with underrun (bit 22) enabled
   and handled.
7. ~~Scanout format from Mesa.~~ Closed by 21.4: Argon composites into the
   driver's own linear buffers; Mesa imports them as LINEAR.

Out of scope for the first driver: mode setting, DPMS, backlight, suspend,
external DisplayPort, cursor and overlay planes, UBWC, and flush-mask bit
semantics.

## 19. 4 KB page translation in bank 4 (mode 9, first run)

Sources: owner-supplied `x13s_dpu/flip9/flip9.txt`, SHA-256
`5dac63c003f37bd90a141ada43330cc8bf02016d3051ec5eacb47e6284d7f8e6`, and the
same boot's `dmesg-boot.0.txt`
(`684b1314935599a21614dab5f8568d568bac431f018765a20ac14e60ad104ab2`) and
`dmesg-boot.1.txt`
(`9bf23a6f7efe4058d11162e84d9b5d13dcc87a47317d99f2c84c65f384b3485d`).
Together they cover setup through END.

B and C were GEM objects of 9224192 bytes from `pd_msm_bo_new`. Each had
141 chunks; B began at physical 0x109bf0000 and C at 0x10a4bc000. Neither
start is 2 MB aligned. Each 4 KB page was entered in a level-3 table (five
per buffer). The console remained a 2 MB block in the same level-2 table.
A software walk of all 4504 pages through L1→L2→L3 found no mismatch. Bank 4
read back as in 16.3, and ten console frames raised no fault.

Forty flips (phase swept as in mode 5) and the return to the console all
had `clear_cb=1`. Retirement removed the buffer's L2 table descriptors.
This raised 0 faults across 41 retirements. The final state: bank 4 restored,
source 0xc6200000, 304 callbacks, unexpected 0, detach 0.

Established: stage-1 level-3 page descriptors with the 18.1 attributes,
level-2 table descriptors mixed with a block descriptor, and scanout from
buffers not aligned to 2 MB. Not established: page ordering across
discontiguous memory. The allocator returned each object's chunks as one
physically contiguous run (`contiguous_runs=1`), so a misordered or
misplaced page would have needed an off-by-a-page error to show. No visual
report accompanies this run. The test now deals the chunks of both objects
alternately: B takes even chunks ascending, C odd chunks descending.
Every chunk boundary is then a discontinuity, and C's starts with a
48 KB chunk. Item 18.3(1) stays open until that variant has run.

### 19.1 Rerun with discontiguous chunks

Sources: owner-supplied `x13s_dpu/flip9-2/dmesg-boot.0.txt`, SHA-256
`1d9a45afe396b19e524528264d11bdd1e846ffd8b10b85ff5c74abcfd48a461e`, and
`dmesg-boot.1.txt`, SHA-256
`079fb863565efd9a203694808c430823673d59cb72fd55d546ffff8cbf0ff815`. The
two copies come from the same boot and together cover setup through END.
The operator reports that the diagonal stripes were unbroken throughout.

The chunks of two GEM objects were dealt alternately. B had 141 chunks in
ascending physical order, 0x109bf8000 through 0x10ad74000. C had 141 in
descending order, 0x10ad84000 down to 0x109c08000. Each buffer held 9224192
bytes in 141 separate physical runs (`contiguous_runs=141`), so every chunk
boundary is a discontinuity, backwards throughout C. The software walk of
4504 pages found no mismatch.

All 40 flips and the return to the console had `clear_cb=1`, with 0 faults
across 41 retirements. Bank 4 was restored and the source left at
0xc6200000. There were 288 callbacks, unexpected 0, detach 0.

Scanout through 4 KB pages of discontiguous, unordered chunks is therefore
established for this pipeline. Item 18.3(1) is closed: a driver may map GEM
objects into bank 4 page by page.

## 20. System SMMU routing and the INTF5 frame counter (mode 10)

Sources: owner-supplied `x13s_dpu/flip10/flip10.txt`, SHA-256
`cc4448ee12c157a648b3dcca4147f7927142a93345329e43dc4df675e8ffa877`, and
`dmesg-boot.0.txt` from the same boot, SHA-256
`6d3fa736f97ab16d97996c34fc23b9aeb7d38a5dbbd58f0ddc448f27f0a90853`, whose
lines match. The folder's `dmesg-boot.1.txt` is byte-identical to
`flip9-2/dmesg-boot.1.txt` and is not evidence for this run. The sample
was taken at probe (25.7 s), before any test write. The SMMU survey only
reads registers.

### 20.1 Routing: all 162 stream-match groups

IDR0 0x4c017ea2 and IDR1 0x6000006e give 162 groups and 110 banks. sCR0 read
0x00220406; section 12's sample from an earlier boot read 0x00260406, which
differs in bit 18. Nine groups are valid:

| SMR | Stream ID | Mask | S2CR | Bank |
| --- | --- | --- | --- | --- |
| 0 | 0x00e0 | 0 | translate | 0 |
| 1 | 0x04a0 | 0 | translate | 1 |
| 2 | 0x04e0 | 0 | translate | 2 |
| 3 | 0x00c0 | 0 | translate | 3 |
| 4 | 0x1000 | 0x0402 | translate | 4 (MDSS, section 13) |
| 5 | 0x0584 | 0x0011 | translate | 5 |
| 6, 7, 8 | 0x0820, 0x0860, 0x0800 | 0 | translate | 6 |

Each of banks 0–6 is a stage-1 context with stage-2 bypass (CBAR 0x0001f000)
in 64-bit format, with TTBR0 0. M is clear in every bank: SCTLR is 0xe0 for
banks 2–6 and 0x0a9f00e0 for banks 0 and 1. No bank translates, and no
other bank shows M set. **Only SMR4, the MDSS stream set, routes to bank 4.**
Bank 4 is therefore exclusive to the display. The streams routed to banks
0–3 and 5 are not attributed here. Bank 6's three streams match the USB
routing recorded in the USB runbook. Attributing the others requires the
device tree's `iommus` properties.

At this point in boot, after USB attach, no PureDarwin component has turned
on translation in this SMMU. A display driver that programs only bank 4's
context registers does not conflict with the observed state of any other
routed device. Ownership of the global registers, SMRs and S2CRs is a
project decision. The display experiments never wrote them.

### 20.2 INTF5 line counter

Spinning on +0xb0 for 20 ms gave a minimum of 1, a maximum of 1232, and the
wrap 1232→1. The counter therefore runs 1–1232: 1232 lines per frame. Five
consecutive callback intervals measured 16.651–16.672 ms (mean 16.664 ms),
giving a line period of about 13.53 µs. Snapshots taken in the thread
shortly after each callback read lines 4–6. The vblank callback samples of
sections 16–17 read line 1–2.

### 20.3 INTF5 frame counter at +0xac

Of INTF5's 192 words from +0x000 to +0x2fc, only +0xac and +0xb0 changed. At
+0xac:

* Right after six consecutive callbacks it read 0x1e2, 0x1e3, …, 0x1e7.
  That is +1 per callback, the same value 8 ms into each frame.
* Across the 37 ms hold-off, which delivered one late callback (section 17.3),
  it advanced from 0x1e8 to 0x1ea (+2).

+0xac counts frames, not callbacks: it advanced by two across two vblanks
delivered as one callback. Its value (482 at 25.7 s) is not frames since
boot, so its origin is not established. Its width, wrap behaviour, and the
exact edge at which it steps relative to the line wrap are also not
established. The samples only bracket the step between one frame's 8 ms
point and the next frame's post-callback snapshot.

Applying this, the commit contract gains a vblank source:
* **Sequence:** use the change in +0xac between callbacks as the number of
  vblanks elapsed, instead of counting callbacks.
* **Timestamp (proposed):** the time of the latest vblank ≈ read time −
  (line − 1) × 13.53 µs, with the line read at +0xb0. This formula follows
  from 20.2 and is untested against an independent clock.

Item 18.3(2) is closed for exclusivity; the ownership decision is recorded
in 20.1. Item 18.3(3) is closed: +0xac supplies the sequence, and the
timestamp formula awaits validation.

## 21. Robustness items

### 21.1 Clock, power and bandwidth retention (item 18.3(4)), by inspection

No PureDarwin component programs the display clock controller or an
interconnect, and none implements disabling unused clocks. The only clock
and power-domain provider in the tree is PDDRMmsm's GPU one, whose disable
and power-off operations are empty (`pd_msm_gpucc8280.c`: `cc_disable`,
`gdsc_off`). With the current tree, the display's clocks, power domain and
bandwidth vote therefore remain as firmware left them. Any later component
that gates unused clocks, power domains or interconnect paths must exclude
the display's resources. The runtime check is the soak of 21.3, which
compares the frame counter with elapsed time.

### 21.2 Console visibility on panic (item 18.3(5))

Mode 11 flipped to buffer B, kept the console mapped in bank 4, and
registered a KDP callout (`kdp_register_callout`, unsupported KPI). Then it
panicked on purpose. The callout runs at debugger entry, before the panic
log is written. It wrote SSPP0 +0x14 = 0xc6200000 and the CTL0 request
0x200c1, and waited for consumption. Only registers already mapped were
touched, with no allocation or locking.

Operator observations, boot arguments without a halting `debug=`:

* First variant: B, then reboot. The console was not seen.
* With a marker (the callout paints B's top 64 rows magenta and holds 1 s
  before switching): B, magenta for about 1 s, then reboot. The callout
  runs on panic.
* With the switch result held for 3 s, and a yellow band if the request
  stays pending: B, magenta for about 1 s, **then the console for about
  3 s**, then reboot. No yellow appeared.

Without a halting `debug=` setting, the panic path reboots immediately after
the callouts and the log. A restore that only waits for the latch is
therefore visible only for an instant. Contract for a driver:

1. Keep the console framebuffer mapped in bank 4 for the life of the driver.
2. Register a panic-time callout that writes the console's address and the
   CTL0 request, using registers that are already mapped.
3. Wait for the next vblank, which takes at most one frame (17.1).
4. Whether the panic text stays readable then depends on the system's panic
   policy (`debug=`), not on the display.

The 3 s hold existed only to observe the switch. The `pdmsmpanichook=0`
control has not been run.

### 21.3 Long-duration flipping with GPU-rendered buffers (item 18.3(6))

**Runs with a looping GL client (owner photo and `soak/dmesg-boot.0.txt`,
SHA-256 `4df174b7cd99eee8d7c012cfbb5f2989b947ec2097459317cadab65223afeb5a`).**
`pdgltri` ran in a loop the whole time, and the soak stopped after about
6 s both times. In the photographed run, the soak's own GPU submit
(seqno 97) did not signal within 1 s. The band was not written: the
uncached readback found grey where green was drawn and the previous green
band intact, so the IB did not execute. Up to that point the display side
was clean: 76 flips, all `clear_cb=1`, 0 faults, 0 underruns, frame
counter 365 against 364 expected. The return to the console and bank 4
restore succeeded. The stall is a GPU submission problem, outside this
specification. A second run with the GPU dump (owner photo) stopped at
flip 75 in the same way:

* Fence 94 of 96 completed. The ring read pointer was 1528 against write
  pointer 1531.
* IB1 was the soak's own IB (0x101820000) with 232608 of 233856 dwords
  remaining. CP_HW_FAULT read 0x03400000 and CP interrupt 0x4.
* GPU SMMU bank 0 FSR read 0x80000402: a translation fault, more than one.
  The first FAR was 0x100420180 with FSYNR0 0x000a0013 (a write, level 3),
  stream 0x400.

That address is the soak's buffer B (GPU iova 0x100420000) at offset
0x180, which is row 0, column 96. It is the first write of flip 0's band,
the write the run's very first readback found missing (band start grey,
band end green, 37.15 s). In that run, pages of the soak thread's GPU
objects were missing from the GPU page table while another client was
opening and closing. With no other client the 300 s run below shows none
of this. The cause lies in the GPU driver's mapping lifetime and is not
established here.

**Run without a looping client (`soak-2/dmesg-boot.0.txt`, SHA-256
`90c7e65e8746e767e00db62377c67897175e2f0494cffcb12c84cff6f18f928f`, and
`dmesg-boot.1.txt`,
`c0256f5423f676d80cd76d7086b6c3cfc38e028879bace08de140e51c13e03c8`).**
A single `pdgltri` started the GPU, and the soak then ran its full 300 s.
Summaries every 2 s are present through t=298 s, then END:

* 4483 GPU-rendered flips plus the return to the console. All 4484
  retirements were at callback 1, with stale 0, straddle 0, timeouts 0 and
  0 faults.
* GPU: 4483 submits all signalled, and every uncached readback check passed
  (mismatches 0, errors 0).
* The underrun interrupt was enabled by default; the run's configuration
  line is not in the excerpt. It counted 0 underruns.
* INTF5 frame counter: 18003 frames against 18005 expected at the nominal
  16.664 ms, a drift of 2 frames in 300 s. The difference is consistent
  with the measured period, about 16.666 ms, being slightly longer than
  nominal. No frame was dropped.
* 18020 callbacks, unexpected 0, detach 0, bank 4 restored, source
  0xc6200000.

**GPU stall status (parked, outside this specification).** A later loop
run stopped with a client's own job hung (fence 178 of 180). The GPU SMMU
reported a read translation fault at the first byte of that client's IB2,
while the soak's objects were fully mapped. Two loop runs with page-table
diagnostics ran 300 s without a stall: one checked before every submit, the
other only after a failure. A single-thread probe completed thousands of
rounds without error. Each round made, GPU-wrote and freed a 64 KB object,
emptying and re-creating its 2 MB region's L3 table. The failures need
another GPU client working concurrently, and two causes remain open:

* a TLB invalidate racing in-flight GPU traffic;
* a `bo_free` on another thread reading `last_fence` before the submit
  publishes it. The ring is kicked before `last_fence` is stored.

Neither affected the display path. Every loop run was clean on the display
side until the GPU stopped.

This confirms display retention (21.1) and the retirement contract (18.1)
over five minutes of continuous flipping with GPU-rendered buffers. Whether
display timing and retirement hold while other GPU clients run at the same
time awaits the GPU stall's resolution.

### 21.4 Scanout format (item 18.3(7)), by inspection

Argon never asks Mesa to allocate scanout memory. neuswc renders through GL
into the flip pair that the kernel exports as dma-bufs (`gpu_try_flip_pair`
in `libswc/fb.c`). neuwld imports each buffer with an explicit modifier of
0, which is LINEAR (`gl.c` `wld_gl_set_scanout_dmabufs`). If that import is
refused, it retries without a modifier. PDDRMmsm implements no buffer
metadata, so that import is linear as well. neuswc advertises only LINEAR to
Wayland clients (`libswc/dmabuf.c`). The scanout layout is therefore chosen
by the display driver, and linear XR24 by construction (12). UBWC and other
layouts are not reachable through this path.
