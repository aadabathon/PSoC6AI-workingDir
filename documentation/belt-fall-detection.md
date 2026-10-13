# Belt-mounted activity and fall detection

The intended device clips to a belt and recognizes walking, sprinting, jumping,
biking, driving and falling. Standing, sitting, transitions and a dropped device
are useful additional labels. A suspected fall displays a local alert that the
wearer can cancel. There is no contact-notification path.

## New belt training profile

Use `analysis/profiles/belt-v1.json`: six channels, a 2-second starting window
(100 samples at 50 Hz), and a 0.5-second stride (25 samples). These are initial
research parameters that need validation for fall latency/recall. This profile
does not relabel the existing wrist model or add trained classes to it. The
generated belt CNN needs its own output mapping in `activity_model_config.h`;
`falling` is the exact class name recognized by the local alert task.

The three-channel wrist reference remains disabled unless explicitly enabled.
For a new belt export, set the firmware contract's input count to six and map
the actual exported output classes. Match the exported preprocessing to the
prepared dataset: ax/ay/az in g, then gx/gy/gz in deg/s. Follow the deployment
steps in [wearable.md](wearable.md).

Belt IMU motion may not reliably separate seated vehicle travel from sitting,
or all biking styles from other periodic movement. Include varied negative
examples and permit `unknown` instead of promising these distinctions before
evaluation. Test belt position, vehicle vibration, road bumps, rapid sitting,
jumping, and a device dropped while unworn.

## Local indicator and cancel control

After a verified belt model produces `falling` with confidence >= 0.85, LED2
flashes four times per second and stays latched until cancelled. This threshold
is provisional. Pressing USER BTN1 consumes the cancellation and leaves recording
unchanged. Further button presses toggle recording normally. Cancellation
suppresses new automatic alerts for 30 seconds; include that interval when
measuring missed falls. There is no automatic timeout or remote escalation.

Use the serial CLI to verify the enclosure controls without a model:

```text
alert test
alert status
alert cancel
```

`alert test` deliberately overrides cooldown. Its event is `TEST`, never a model
detection. Model alerts produce `ALERT`; cancellation produces `CANCEL`. Events
use the logger's output task and emit even when sensor recording is stopped.
Capture sessions preserve them separately in `events.csv`. `ts_ms` is the alert
time, while `source_ts_ms` identifies the inference input time. The legacy radar
build owns the LEDs and does not start the fall indicator task.

The enclosure needs a visible LED2 window/light pipe and a button that can be
pressed while worn. Verify visibility in daylight and cancel access on the belt.
This implementation provides a visual alert; sound requires a chosen transducer,
driver and pin allocation. Start with a repeatable position such as `belt_right`;
front, side and back positions are separate acquisition conditions. Record
orientation, strap fit and enclosure revision with each session.

## Fall evaluation and data collection

Measure **event recall, false alerts per monitored hour and onset-to-alert
latency**, not just window accuracy. `analysis/fall_eval.py` evaluates one-to-one
matches against human annotations and explicit monitored coverage. Include long
ordinary-activity recordings; balanced snippets alone cannot establish a useful
false-alarm rate. No validated fall-detection performance is claimed yet.

For initial fall research, use documented public recordings and controlled
non-human fixtures for impact/range checks; do not induce falls while wearing
the prototype. [SisFall](https://www.mdpi.com/1424-8220/17/1/198) is a published
waist-worn fall/activity dataset. Its sensor hardware, units and sampling differ
from this board, so it requires a documented import/resampling adapter and does
not validate this device by itself.

The BMI270 supports selectable ranges up to +/-16 g
([Bosch specifications](https://www.bosch-sensortec.com/en/products/motion-sensors/imus/bmi270)).
The current firmware remains +/-4 g, +/-2000 deg/s at a 50 Hz polling cadence.
Review clipping and timing before selecting wider ranges or a higher-rate FIFO
capture path; update conversion constants, capture metadata and QC together.

`falling` is transient. Do not label an entire long recording as a fall because
it contains one event. Human annotations should identify onset/end in the sensor
clock. The present continuous-label window builder is suitable for curated
labeled clips; it discards label-boundary windows and cannot reconstruct event
context automatically. Curate context clips separately for fall training and
retain the original continuous recording for event evaluation. Pre-impact,
impact and recovery context must be consistent between training and deployment.

Before field use, validate the exported model on the belt hardware, sustained
sampling, LED visibility, cancellation under load, cooldown behavior, battery
runtime and missed/false events. Board-only flash capture still needs the
durability work listed in [wearable.md](wearable.md#flash-logging-limitations).
