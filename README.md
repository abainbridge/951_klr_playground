# 951_klr_playground
Some useless stuff for the Porsche 944 Turbo KLR ECU. The purpose is mainly to force me to learn the things jhnbyrn wrote about the system. See https://jhnbyrn.github.io/951-KLR-PAGES/

The Annotated_Stock1987_951KLR.asm file is copied from https://jhnbyrn.github.io/951-KLR-PAGES

## KLR wiring loom connections and electronics

The KLR (knock regulator and boost control unit) plugs into the engine loom
through a 25-way, two-row connector: pins 1-13 in one row, 14-25 in the other.

"Factory name" is the label Porsche gives the pin in the test plan. Wire colours
use the factory abbreviations (SW black, WS white, RT red, GN green, GE yellow,
GR grey, BR brown, BL blue) with the cross-section in square mm in front.

| Pin | Factory name | Dir | Wire | Connects to | What it does |
|----:|---|---|---|---|---|
| 1 | Diagnosis | out | - | Not wired in the loom | Diagnostic output. Reported to be unpopulated on the KLR board. |
| 2 | Timing valve (output) | out | 1.5 BL/RT | Cycling valve | Switched ground for the cycling (boost control) valve; the valve's other terminal is fed with +12 V. The KLR pulse-width modulates this pin (microcontroller P1.4) to set how much boost pressure reaches the wastegate. |
| 3 | LED | out | 1.0 GN | Test connector, "LED" terminal | Flashes the fault blink codes on a test LED (microcontroller P2.4). See `blink_codes.txt`. |
| 4 | - | | | Not used | |
| 5 | Intake vacuum signal (output for pressure display) | out | 1.0 RT/WS | 4-pin plug near the DME, terminal 3 | Analogue copy of the KLR's internal manifold pressure sensor, sent on to the dashboard boost gauge. |
| 6 | U | in | 2.5 SW/GE | Switched +12 V from the DME relay (shared with the injector feed) | Power supply. "U" is the German symbol for voltage. The KLR also measures this on ADC channel 1 (battery voltage). |
| 7 | - | | | Not used | |
| 8 | - | | | Not used | |
| 9 | Ignition signal (input) | in | 1.0 GN, shielded | DME pin 21 (also 4-pin plug terminal 2, the tachometer signal) | The DME's ignition signal. It reaches both the INT and T1 pins of the microcontroller through one inverter, so the microcontroller sees the opposite level to this pin (see "Ignition input circuit" below). The DME keeps the signal low (the dwell period) for about 3.7ms. |
| 10 | Ground | - | shield | Shield of the pin 9 wire | Ground for the screen around the ignition input wire. |
| 11 | Ground | - | 0.5 SW | Knock sensor terminal 2 | Ground side of the knock sensor. |
| 12 | Ground, shield knock sensor | - | shield | Shield of the knock sensor cable | Ground for the screen around the knock sensor wires. |
| 13 | Knock sensor | in | 0.5 GE | Knock sensor terminal 1 | Knock sensor signal. Inside the KLR it is amplified and feeds two ADC channels: overall noise level (channel 0) and a resettable integrator used for the actual knock decision (channel 5). |
| 14 | Car ground final stage timing valve | - | 1.5 BR/GE | Ground point | Power ground for the transistor that drives the cycling valve (pin 2). Kept separate from pin 20 so the valve current does not disturb the electronics. |
| 15 | Knocking yes / no (output) | out | - | Not wired in the 1985 loom diagram | Digital "knock detected" flag (microcontroller P1.6). |
| 16 | Ignition signal (output) | out | 1.0 SW/BL | DME pin 32 ("TD input final stage from KR") | The ignition signal handed back to the DME, delayed by however much retard the KLR wants. The DME's output stage drives the coil from this, not from its own signal. Microcontroller P2.7 (P2.6 carries the inverse). |
| 17 | - | | | Not used | |
| 18 | Full load signal (output) | out | 1.0 WS | DME pin 3 ("Full load signal from KLR") | Tells the DME the throttle is wide open, so it can switch to its full-load fuel and ignition maps (microcontroller P1.5). The DME plug has an idle contact input (pin 2) but no full-load contact; the KLR works full load out from the throttle angle sensor instead. |
| 19 | - | | | Not used | |
| 20 | Car ground (electronic ground connection) | - | 1.0 BR | Ground point | Ground for the KLR's electronics. |
| 21 | Angle sensor - supply | out | 1.0 WS/RT | Throttle position sensor | Reference voltage for the throttle potentiometer. The KLR reads this back on ADC channel 3 and works out the angle as a proportion of it. |
| 22 | Angle sensor - tap | in | 1.0 WS/GN | Throttle position sensor | Potentiometer wiper: the throttle angle (ADC channel 7). |
| 23 | Angle sensor - ground, shield for angle sensor wire | - | 1.0 WS/BL | Throttle position sensor | Ground end of the potentiometer and screen for its cable. |
| 24 | tr-signal (input) | in | 1.0 SW/GN | DME pin 31 ("Trigger signal for KR") | Trigger pulse from the DME, once per cylinder firing (twice per crank revolution). It is wired to the microcontroller's reset pin, so the KLR program restarts from address 0 on every trigger. |
| 25 | - | | | Not used | |

### How the signals fit together

* **Ignition loop.** The DME does not fire the coil directly. Its ignition
  signal leaves on DME pin 21, passes through the KLR (pin 9 in, pin 16 out)
  and returns on DME pin 32, where the DME's output stage switches the coil.
  This is what lets the KLR retard the spark when it detects knock. Bridging
  KLR plug terminals 9 and 16 is the usual way to bypass the KLR for testing.
* **Trigger.** DME pin 31 to KLR pin 24 tells the KLR where the crankshaft is.
* **Full load.** KLR pin 18 to DME pin 3 is the only information the KLR
  sends to the DME other than the ignition signal.
* **Boost control.** The manifold pressure sensor is inside the KLR, fed by a
  vacuum hose rather than the loom. The KLR compares it with a target and
  drives the cycling valve from pin 2.

### Ignition input circuit

Traced with a multimeter on the 1987 UK unit shown in `pcb_photos`, with the
unit disconnected and unpowered.
Resistor values were measured in circuit, so they are approximate.

```
connector pin 9 -- B106 -- R409 (~1k) --+-- R411 (~39k) -- ribbon 16 -- CD40106 pin 1 -|>o- pin 2 --+-- INT (8048 pin 6)
                                        |                                                           +-- T1  (8048 pin 39)
                                        +-- R410 (~13k) -- ground (connector pin 20)
```

* B106 is a wire link. B107, R417 and R418 are unfitted positions on the same
  net.
* There is only a pull-down on the KLR side, so the DME must drive the line
  high itself.
* INT and T1 are the same signal: the inverse of connector pin 9. INT is
  active low, so the external interrupt is requested whenever T1 is low, i.e. whenever
  connector pin 9 is high.
* The firmware only enables the external interrupt while T1 is high, and uses
  it to start the spark delay. That makes the rising edge at connector pin 9 the
  edge the KLR times precisely, so connector pin 9 is most likely low during
  dwell and rises at the spark. This last point is inferred from the firmware and has not been
  checked with an oscilloscope.
* The +5 V rail is on 8048 pins 5 (SS), 7 (EA), 26 and 40, and CD40106 pin 14.
* Connector pin 6 to connector pin 20 reads about 4.5 megohms, so connector
  pin 6 is not a ground.

### Ignition output circuit

Traced the same way as the input. Not yet complete: see the open points below.

```
                 +5 V
                  |
               R586 (~2k)
                  |
8048 pin 38 ------+-- B35 -- ribbon -- link 1 -D502 >|-- link 2 -- R508 (10k) --+-- T502 base
(P2.7)                                                                          |
                                                                           R509 (10k)
                                                                                |
                                                                             ground

T301 left leg --+
                +-- T500 collector
                    T500 (NPN) base -- ?
                    T500 emitter -- R507 (1.5k) --+-- R510 (510R) -- connector pin 16
                                                  +-- T502 collector   (T502 is NPN, emitter to ground)
                                                  +-- big diode (ITT), band at this end, other end to ground
```

* B35 and links 1 and 2 are wire links. Link 2 also runs to some unfitted
  link positions, so on this unit D502 is the only thing driving T502.
* T502 pulls connector pin 16 to ground through R510 when it is on. When it is
  off, connector pin 16 is pulled up through R507 from T500's emitter.
* D502's band is at the link 2 end, so 8048 pin 38 high turns T502 on and
  connector pin 16 goes low: connector pin 16 is the inverse of P2.7. With the
  one inversion on the input side, connector pin 16 follows connector pin 9
  whenever the firmware copies T1 to P2.7, which is what the "bridge connector
  pins 9 and 16" bypass relies on.
* Open points:
  * T500's base, and whether its collector net is the supply from connector
    pin 6. These
    set the voltage connector pin 16 is pulled up to.
  * What 8048 pin 37 (P2.6, driven opposite to P2.7 by the firmware) ends up
    controlling. Traced so far, on the digital board: 8048 pin 37 -- R960 (0R) --
    node with R654 (~2.3k) pull-up to +5 V -- wire link 60 -- unmarked resistor
    (~11.4k, between T650 and T651) -- T651 base. T651 is NPN. A second resistor
    is also on the base. T651's collector and emitter are not traced; one
    possibility is that it drives T500's base.
  * The type of the big diode (plain or zener). It is ITT-made in a black
    axial package with a silver band; no type number is visible.

### Sources and caveats

* Pin names: "Knock control plug connections - 944 Turbo" and "DME plug
  connections - 944 Turbo" in the Porsche
  [DME/KLR Test Plan '85](https://jhnbyrn.github.io/951-KLR-PAGES/reference/DME_KLR_Test_Plan.pdf).
* Destinations and wire colours: the "Wiring Diagram Type 944, 944 turbo
  Model 85" in the same document. It is a low-resolution scan, so the wire
  colours in particular should be checked against the car. It is also the 1985
  diagram; the KLR in `pcb_photos` is from a 1987 UK car, which may differ.
* Microcontroller pins and ADC channels: jhnbyrn's
  [pin assignments](https://jhnbyrn.github.io/951-KLR-PAGES/pin_assignments.html)
  and [hardware overview](https://jhnbyrn.github.io/951-KLR-PAGES/hardware_overview.html).
* Not confirmed: pin 6 being the supply is read from the wiring diagram and
  the "U" label rather than stated outright, and pin 1 being unpopulated is
  from forum posts.
