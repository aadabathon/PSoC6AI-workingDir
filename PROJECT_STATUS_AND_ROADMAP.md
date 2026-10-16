# PSoC 6 belt wearable: current state and roadmap

Updated: 2026-10-15. This is a snapshot of the implementation and decisions to
date; update it as hardware measurements, datasets and model exports become
available.

**Current stage:** the firmware and offline data tools provide a foundation for
collection and model integration. The immediate milestone is a secure belt
holder and a verified pilot recording. A trained belt CNN, reliable board-only
flash collection and on-device fall-detection performance are still pending.

## Intended device

- Hardware: Infineon CY8CKIT-062S2-AI, using the BMI270 accelerometer/gyroscope.
- Placement: belt-mounted, with a repeatable position and orientation.
- Target activities: walking, sprinting, jumping, biking, driving and falling.
- Supporting labels: standing, sitting, transitions and an unworn device drop.
- Suspected fall response: a local alert with a cancel option. The current
  implementation uses LED2; adding sound requires additional hardware work.
- Data workflow: preserve raw recordings and metadata, analyze offline in Python,
  train a CNN in DEEPCRAFT Studio, export preprocessing and inference as C, and
  validate the export on the board.

Driving and biking are target distinctions, not established capabilities. A
belt IMU may confuse vehicle travel with sitting or other periodic movement;
evaluate those classes and allow an unknown result where appropriate.

## Workspace and source of truth

Paths below are relative to the `workingDir/` repository containing this file.
The parent workspace is not itself a Git repository.

| Location | Role |
| --- | --- |
| `workingDir/` (this repository) | Firmware, model adapter, local alert and firmware tests |
| `../analysis/` (separate Git repository) | Capture, validation, offline dataset preparation and fall-event evaluation |
| `../DeepCraft/IMUexample/` | Legacy three-axis reference project and training artifacts |
| `../documents/` | Board fabrication, assembly, layout and other hardware documents |

At this review, firmware was on `main` at `9fe30aa`, with a clean working tree
before adding this document. The analysis repository contained modified and
untracked pipeline work; its HEAD (`a182dd7`) alone does not identify the current
tools. Review and version those changes before using Git revisions as dataset
provenance. Record the revision actually flashed, rather than assuming the host
checkout matches the device.

The detailed implementation guides remain:

- [Wearable firmware and deployment workflow](documentation/wearable.md)
- [Belt mounting, local alert and fall evaluation](documentation/belt-fall-detection.md)
- [Offline capture and dataset workflow](../analysis/README.md)
- [Generated model requirements](source/ml/generated/README.md)

## Current implementation

| Area | Implemented | Remaining evidence or work |
| --- | --- | --- |
| IMU acquisition | Timestamped six-axis samples at a nominal 50 Hz polling cadence | Measure timing, gaps and clipping on hardware under the intended load |
| Firmware profiles | Default multisensor/Wi-Fi build and optional `WEARABLE_PROFILE=1` build | Power management, battery integration and runtime measurements |
| CNN integration | DEEPCRAFT streaming Queue API adapter, unit conversion, class mapping, confidence checks and temporal resets | Supply a generated belt model and verify its full input/output contract |
| Local fall alert | Latched LED2 indicator, button/CLI cancellation, cooldown and separate alert/test/cancel events | Check visibility, access and behavior on the worn device |
| Host capture | UART/TCP tools preserve raw bytes, parsed samples, rejected rows, events and metadata | Validate real-device capture and transport behavior over sustained runs |
| Offline preparation | Integrity checks, segmentation, grouped splits, NumPy windows, continuous Studio tracks and dataset manifests | Collect and review a representative belt dataset |
| Fall evaluation | Event matching, recall, precision, false alerts per monitored hour and latency | Human event annotations and continuous held-out recordings |
| Onboard flash | Existing flash writer/dump path and host import support | Durability, alignment, integrity and synchronization fixes before board-only collection |
| Mechanical design | Board drawings available; enclosure requirements identified | CAD, fit prints, belt attachment and enclosure validation |

**Inference is disabled by default.** `source/ml/generated/` currently contains
instructions, not a generated C model. The existing `.h5` is not the MCU library
required by the adapter. No validated belt activity or fall accuracy is available.

### Configuration that must stay aligned

| Item | Current firmware or reference | Belt training starting point |
| --- | --- | --- |
| Acquisition | Nominal 50 Hz polling; sensor configured at 200 Hz | Validate the actual acquisition cadence before training |
| Sensor range | Accelerometer +/-4 g; gyroscope +/-2000 deg/s | Review impact clipping before retaining or changing these settings |
| Model inputs | Legacy contract: three acceleration channels | Six channels: `ax,ay,az,gx,gy,gz` |
| Model units | Acceleration converted from m/s^2 to g | `g,g,g,deg/s,deg/s,deg/s` |
| Window / stride | Legacy reference: 50 / 30 samples | `belt-v1`: 100 / 25 samples, or 2 s / 0.5 s |
| Outputs | Legacy: standing, running, walking, sitting, jumping, unlabeled | Exact classes and order from the new export |
| Adapter confidence | 0.70, provisional | Select using validation data |
| Automatic fall trigger | Exact class name `falling`, confidence >= 0.85 | Select and evaluate the operating point using validation events |

The six-channel dataset profile does not change the firmware's three-channel
model contract automatically. Update `activity_model_config.h` for the actual
export, including class order. Firmware passes each sample once; the exported
preprocessor owns sliding windows and overlap.

The local indicator flashes four times per second until cancelled. A button
press during an active alert cancels it without toggling recording. Cancellation
starts a 30-second suppression period. CLI commands are `alert test`,
`alert status` and `alert cancel`. A `TEST` event is not a model detection.
There is no automatic remote notification or escalation.

## Roadmap and completion gates

### 1. Design and print the first belt holder

Use **SOLIDWORKS through UW-Madison CAE**, subject to the applicable license.
The recommendation is based on existing campus access and its suitability for
dimensioned parts and assemblies. UW's standard educational license is for
instructional use; formal research needs the appropriate research license.

1. Complete the introductory parts/assemblies tutorials. Learn dimensioned
   sketches, extrude, cut, shell, holes, fillets and assembly mates.
2. Use the fabrication and assembly drawings in `../documents/`, then measure
   the physical board. Model the PCB and occupied component space, including
   underside components, mounting holes and the actual USB plug.
3. Design a rigid cradle with mounting posts, two belt/strap slots and a
   removable retaining bar or lid. Provide USB strain relief, visible LED2 and
   reachable cancel/record controls. Mark UP and the body-facing side.
4. Choose the power source and its attachment before closing the enclosure
   design. Maintain antenna clearance if collecting over Wi-Fi.
5. Print a shallow fit prototype first. Suggested starting values are 2 mm
   walls and 0.4 mm clearance per side around the board; adjust from fit tests.
6. Save editable CAD, a STEP export and the print mesh, plus an enclosure
   revision. PLA can serve for initial fit checks; select the wearable material
   with the printing staff based on toughness, temperature and attachment needs.

**Completion gate:** the board is retained without contacting components, the
holder stays stable on the belt, the USB connection fits, and the button and LED
are usable while worn. Record belt position, orientation and enclosure revision.

### 2. Verify acquisition and a small pilot recording

Do this before collecting a large training dataset. Hardware acquisition checks
should precede training; final model checks follow the C export.

1. Build and flash the intended collection configuration. Use the wearable
   profile for UART collection; use the default Wi-Fi-enabled build for TCP.
2. Verify units, axis directions, sample intervals, queue behavior and clipping.
   Measure under the actual logging load. If ranges or cadence change, update
   driver settings, conversion constants, metadata and quality checks together.
3. Exercise physical start/stop and `alert test` / cancellation. Check that
   cancellation preserves the recording state and events reach host capture.
4. Capture short labeled pilot sessions, audit them, and inspect raw traces,
   timing, rejected rows and metadata. Exercise host disconnect/reconnect and
   finalize separate sessions across resets or transport failures.
5. Use the pilot to settle the mounting, labels and acquisition settings.

**Completion gate:** reviewed real-board recordings have explained timing and
quality results, usable metadata and reproducible audit results. The mounting
and acquisition settings are recorded and stable enough for dataset collection.

### 3. Make board-only storage reliable if untethered collection is needed

Use UART or TCP host capture for initial collection. The current flash v1 path
is experimental and blocks reliable board-only data collection:

- Stop does not acknowledge a fully drained, committed recording; checkpoints
  can leave tail records undiscoverable after power loss or reset.
- Packed record sizes and sector erase assumptions are misaligned.
- Records lack CRCs, and writer/dump/erase operations need synchronization.
- Fall alert events are preserved by host capture, not flash v1 records.

Export existing contents before changing the flash layout. Implement a versioned
record format with CRCs, correct sector handling, sequence/overflow reporting,
serialized access, recovery and acknowledged flush/checkpoint at stop. Decide
how alert events are retained for disconnected evaluation.

**Completion gate:** fault tests cover interrupted writes, resets, power loss,
queue overflow, full storage and dump/erase access. Recovered data has explicit
boundaries and integrity results, with no silent loss presented as a clean run.
This work can proceed alongside CAD and host-based pilot collection.

### 4. Collect and version the belt dataset

1. Establish written label definitions and a capture protocol. Record subject,
   device, flashed firmware, belt position, orientation, enclosure revision and
   relevant notes for every session.
2. Begin with walking, sprinting, jumping, sitting, standing and transitions;
   add driving, biking and device-drop negatives with varied conditions.
3. Collect repeated takes across days and participants. The default pipeline
   requires at least three subject groups and class coverage in every split;
   three participants are a software minimum, not evidence of generalization.
4. Preserve whole participants for evaluation where possible. A personal-only
   exploratory study can explicitly group by session, with separate days and
   a limited claim about generalization.
5. Annotate fall onset/end separately from continuous recording labels. Retain
   surrounding context and long ordinary-activity recordings. The current
   single-label window builder does not create event-centered clips by itself.
6. Use documented public fall recordings and controlled non-human fixtures for
   initial research. Their hardware and sampling differences require explicit
   handling; they do not establish performance on this device.
7. Audit sessions and prepare a new, versioned dataset with the `belt-v1`
   profile. Freeze the held-out test set before model selection.

`unknown` and `unlabeled` are excluded from supervised windows. Use deliberate
background labels where needed, and keep human ground truth separate from model
outputs. A long recording containing one fall must not be labeled falling for
its entire duration.

**Completion gate:** a dataset manifest identifies source hashes, splits,
parameters, class coverage and quality results. Evaluation recordings include
explicit monitored coverage and event annotations, not only balanced snippets.

### 5. Train, evaluate and export the DEEPCRAFT CNN

1. Import prepared continuous tracks into Studio while preserving the assigned
   subject/session splits. Match channel order, units and sample cadence.
2. Start with the six-axis, 100-sample window and 25-sample stride profile;
   treat these as parameters to evaluate, especially for fall latency.
3. Tune model and confidence settings using validation data. Inspect per-class
   precision/recall and the confusion matrix. Evaluate fall event recall,
   false alerts per monitored hour and onset-to-alert latency separately.
4. Export self-contained C including preprocessing: `IMAI_` prefix, streaming
   Queue API without timestamps. Retain the export's input/output test vectors.
5. Place the export and dependencies in `source/ml/generated/`. Update the
   firmware contract and replay test vectors before enabling inference.

**Completion gate:** the export matches reference outputs within documented
tolerances; class mapping, units and preprocessing are verified. Record the
dataset, Studio project, export, firmware and threshold versions together.

### 6. Validate the complete wearable

- Measure sampling and inference timing, gaps, memory/stack use and transport
  behavior with all intended tasks active.
- Evaluate held-out belt recordings and the resulting latched alerts, including
  cancellation and the 30-second suppression period.
- Check mounting stability, orientation variation, button access and daylight
  LED visibility in the chosen enclosure.
- Measure runtime, charging/power behavior and temperature for the chosen power
  arrangement. The task profile alone is not a low-power design.
- Set acceptance targets for recall, false alerts, latency and runtime before
  final test evaluation. Document failures and coverage limits.

**Completion gate:** a hardware validation report ties measured results to a
specific board, enclosure, firmware and model. Establishing dependable fall
performance remains an open outcome of this work.

## Validation evidence currently available

- The prior implementation work reported 24 passing Python tests covering
  capture/import, integrity, segmentation, grouped preparation and fall metrics.
- Host C tests cover the adapter with a mock generated model and the local
  alert policy. These verify software behavior, not recognition accuracy.
- `build/wearable-build.log`, dated 2026-10-12, records a successful wearable
  firmware build. That collection build used 295,696 bytes of flash; it does
  not establish resource use after adding the CNN. The log also contains vendor
  linker warnings, so build success is not equivalent to hardware validation.
- Legacy walking CSVs exist in `../analysis/data/`. No reviewed belt session
  manifests, prepared belt dataset or deployed belt CNN were found in this
  review.
- This documentation update inspected source and the existing build log. It did
  not rerun the software suites, program a board or perform hardware tests.

## Commands and resources

Run firmware builds from this repository in ModusShell:

```sh
make build WEARABLE_PROFILE=1

# Only after supplying and verifying the generated model:
make build WEARABLE_PROFILE=1 ACTIVITY_MODEL_ENABLED=1 ACTIVITY_MODEL_CONTRACT_VERIFIED=1
```

Run host commands from the separate `../analysis/` repository with its Python
environment and requirements installed. Replace the port, flashed revision and
enclosure/orientation metadata with the actual setup:

```powershell
python collect.py --port COM3 --label walking --duration 45 --subject p01 --placement belt_right --orientation "USB upward, board facing outward" --enclosure v1 --firmware YOUR_FLASHED_REVISION
python wearable.py audit data/sessions/SESSION_ID

# After collecting sufficient subject and class coverage; output must be new:
python wearable.py prepare --sessions data/sessions --profile belt-v1 --out data/processed/belt-v1
python -m unittest discover -s tests -v
```

Useful design resources checked during planning:

- [UW SOLIDWORKS access and licensing](https://kb.wisc.edu/157644)
- [CAE SOLIDWORKS installation/remote access and research license route](https://labsoftware.cae.wisc.edu/title/detail/59)
- [UW introduction to SOLIDWORKS](https://sts.doit.wisc.edu/workshop-topic/intro-to-solidworks/)
- [UW CAD support and consultation](https://making.engr.wisc.edu/cad-support/)
- [Official SOLIDWORKS beginner tutorials](https://blogs.solidworks.com/products/solidworks/beginners-guide-to-solidworks/)

UW's CAD support primarily teaches Onshape and offers consultation appointments.
Use that support to review fit and printability as the first holder takes shape.
