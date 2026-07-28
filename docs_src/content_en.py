# English manual content.  Page ids and heading ids must match content_ja.py
# so that switching language keeps the reader on the same topic.

SECTIONS = [
    ("start", "Getting started"),
    ("use", "Using it"),
    ("api", "API"),
    ("how", "How it works"),
    ("dev", "Development"),
]

STRINGS = {
    "lang": "en",
    "html_lang": "en",
    "title": "M5Tab5 Logic Analyzer — Documentation",
    "brand": "M5Tab5 Logic Analyzer",
    "tagline": "8ch / up to 80 MSa/s / ESP32-P4",
    "toc": "Contents",
    "menu_label": "Contents",
}

PAGES = [
{
    "id": "overview",
    "section": "start",
    "title": "Overview",
    "body": """
  <h1>Overview</h1>
  <p class="lede">Firmware that turns an M5Stack Tab5 (ESP32-P4) into an eight channel logic analyzer. Capture, trigger, waveform navigation, protocol decoding and microSD export all happen on the 5-inch touch panel — no host PC required.</p>

  <figure>
    <img src="img/01-overview.png" alt="Main screen showing eight channels of waveform data with per-channel measurements">
    <figcaption>The main screen right after a capture. Acquisition settings on top, waveforms in the middle, cursors and measurements below, controls at the bottom.</figcaption>
  </figure>

  <h2 id="highlights">What it does</h2>
  <div class="cards">
    <div class="card"><div class="big">80 MSa/s</div><p>Measured peak sampling rate using PARLIO + DMA, all eight channels at once.</p></div>
    <div class="card"><div class="big">8 MSa</div><p>Maximum capture depth in PSRAM. One byte per sample.</p></div>
    <div class="card"><div class="big">3</div><p>Built-in protocol decoders: UART, I2C and SPI.</p></div>
    <div class="card"><div class="big">0.00%</div><p>Measured frequency error against a 1 MHz reference.</p></div>
  </div>

  <table>
    <tr><th>Area</th><th>Detail</th></tr>
    <tr><td>Acquisition</td><td>8 channels, 64 kSa – 8 MSa depth, 100 kSa/s – 80 MSa/s</td></tr>
    <tr><td>Engines</td><td><b>PARLIO</b> (DMA, fast) and <b>CPU</b> (polling, fallback), selected automatically</td></tr>
    <tr><td>Trigger</td><td>Per channel Rise / Fall / Both / High / Low. Edges are OR'ed, levels are AND'ed. Pre-trigger 0–95%</td></tr>
    <tr><td>Display</td><td>Pinch zoom, drag pan, level-of-detail rendering that keeps single-sample glitches visible when zoomed out</td></tr>
    <tr><td>Measurements</td><td>Per channel frequency, duty cycle, edge count and minimum high/low pulse width</td></tr>
    <tr><td>Decoders</td><td>UART (with auto baud), I2C, SPI</td></tr>
    <tr><td>Export</td><td>CSV (visible window), VCD (whole capture, opens in PulseView and GTKWave), decoder output, screen BMP</td></tr>
    <tr><td>Remote control</td><td>Line-oriented JSON API over USB CDC</td></tr>
  </table>

  <h2 id="compare">Compared with a budget USB analyzer</h2>
  <p>The FX2LP (CY7C68013A) analyzers that sell for a few dollars advertise 8 channels at 24 MSa/s. Here is how this project measures up.</p>
  <table>
    <tr><th></th><th>FX2LP class</th><th>This project</th></tr>
    <tr><td>Channels</td><td>8</td><td>8</td></tr>
    <tr><td>Peak rate</td><td>24 MSa/s (claimed)</td><td><b>80 MSa/s (measured)</b></td></tr>
    <tr><td>Capture depth</td><td>Streamed over USB, host dependent</td><td>8 MSa on board</td></tr>
    <tr><td>Display</td><td>Requires PC software</td><td>Self-contained</td></tr>
    <tr><td>Decoders</td><td>The full sigrok collection</td><td>UART / I2C / SPI</td></tr>
  </table>
  <div class="note"><p>On decoder variety sigrok is far ahead. Export a VCD and PulseView will open it, so capturing on the instrument and analysing on a PC is a practical split.</p></div>

  <h2 id="limits">Limitations</h2>
  <ul>
    <li><b>3.3 V logic inputs only.</b> Anything above that will damage the ESP32-P4 without a divider or level shifter.</li>
    <li><b>The trigger is a software search.</b> The buffer is filled first and the condition is looked for afterwards, so a rare one-shot event can be missed.</li>
    <li>Rates are integer divisions of 160 MHz, so only 80 / 53.33 / 40 / 32 / 26.67 / 22.86 / 20 MSa/s and so on are reachable.</li>
    <li>Digital only — there is no analogue measurement.</li>
  </ul>
""",
},
{
    "id": "setup",
    "section": "start",
    "title": "Setup",
    "body": """
  <h1>Setup</h1>
  <p class="lede">Probe wiring, building and flashing.</p>

  <h2 id="wiring">Probe wiring</h2>
  <p>Probes go on the Tab5's <b>M5-Bus header (2×15, 30 pins)</b>.</p>

  <figure>
    <img src="img/m5bus-pinout.svg" alt="M5-Bus header pinout with the eight probe channels highlighted in their trace colours" style="background:#0f1216">
    <figcaption>M5-Bus pinout. CH0–CH7 are coloured to match their on-screen traces.</figcaption>
  </figure>

  <table>
    <tr><th>Channel</th><th>GPIO</th><th>M5-Bus pin</th><th>Note</th></tr>
    <tr><td>CH0</td><td>G2</td><td class="num">21</td><td>general purpose</td></tr>
    <tr><td>CH1</td><td>G3</td><td class="num">19</td><td>general purpose</td></tr>
    <tr><td>CH2</td><td>G4</td><td class="num">20</td><td>general purpose</td></tr>
    <tr><td>CH3</td><td>G5</td><td class="num">11</td><td>shared with M5-Bus SPI_SCK</td></tr>
    <tr><td>CH4</td><td>G16</td><td class="num">2</td><td>general purpose</td></tr>
    <tr><td>CH5</td><td>G17</td><td class="num">4</td><td>shared with M5-Bus PB_IN</td></tr>
    <tr><td>CH6</td><td>G18</td><td class="num">7</td><td>shared with M5-Bus SPI_MOSI</td></tr>
    <tr><td>CH7</td><td>G19</td><td class="num">9</td><td>shared with M5-Bus SPI_MISO</td></tr>
    <tr><td>GND</td><td>—</td><td class="num">1 / 3 / 5</td><td>must be common with the circuit under test</td></tr>
  </table>

  <div class="warn">
    <p><b>Inputs are 3.3 V.</b> Connecting a 5 V or 12 V signal directly will destroy the ESP32-P4. Use a divider or a 3.3 V level shifter/buffer for anything higher.</p>
    <p><b>Pins 25 / 27 / 29 carry HVIN and pins 28 / 30 carry 5 V and the battery.</b> Keep probe leads away from them.</p>
  </div>

  <p>Idle inputs have their internal pull-ups enabled, so an unconnected channel reads as a steady high rather than picking up noise.</p>

  <div class="tip">
    <p>All eight pins sit in GPIO bank 0 (GPIO0–31), which lets the CPU engine grab every channel with a single register read. If you remap them, staying within one bank avoids losing throughput. The map lives in the first few lines of <code>include/config.h</code>.</p>
  </div>

  <h2 id="build">Build and flash</h2>
  <p>The project uses PlatformIO with the ESP32-P4 capable <a href="https://github.com/pioarduino/platform-espressif32">pioarduino</a> platform.</p>
  <pre><code>pio run -t upload</code></pre>
  <p>Serial monitor:</p>
  <pre><code>pio device monitor</code></pre>
  <p>The first build downloads the toolchain, which takes 5–10 minutes.</p>

  <div class="warn">
    <p><b>On Windows, do not run <code>pio</code> from Git Bash or MSYS.</b> The ESP-IDF tool installer refuses with <code>MSys/Mingw is not supported</code>. Use PowerShell or cmd.</p>
  </div>

  <h2 id="firstrun">First check without any wiring</h2>
  <p>The built-in test generator can validate the whole signal path with nothing connected. Over the <a href="#" data-goto="api">control API</a>:</p>
  <pre><code>gen ch=0 freq=1000000 duty=50
trigger clear=1
config engine=parlio rate=26666666 depth=2097152
single</code></pre>
  <p>Once the capture finishes, send <code>stats</code>. If <code>stats[0].freq</code> comes back near 1000000 then the GPIO matrix, PARLIO, DMA and the buffer are all healthy.</p>
""",
},
{
    "id": "ui",
    "section": "use",
    "title": "Reading the screen",
    "body": """
  <h1>Reading the screen</h1>
  <p class="lede">The 1280×720 display is divided into five regions.</p>

  <figure>
    <img src="img/03-zoomed.png" alt="Zoomed waveform view with all eight channels carrying traffic">
    <figcaption>A mixed-signal capture zoomed in. CH0/CH1 carry I2C, CH2 UART, CH3 a PWM and CH4–CH7 SPI.</figcaption>
  </figure>

  <table>
    <tr><th>Region</th><th>Contents</th></tr>
    <tr><td>Top bar</td><td>RUN / SINGLE, sample rate, depth, engine, trigger mode, and status on the right</td></tr>
    <tr><td>Left gutter</td><td>Channel names and their GPIO. <code>inv</code> marks an inverted channel</td></tr>
    <tr><td>Plot</td><td>Time ruler, eight traces, two rows of decoder annotations</td></tr>
    <tr><td>Middle panel</td><td>Cursors, per-channel measurements, status and messages</td></tr>
    <tr><td>Bottom bar</td><td>Zoom, pan, cursor controls and the overlay buttons</td></tr>
  </table>

  <h2 id="statusline">The status readout</h2>
  <p>It reads like <code>CPU TRIG'D @ 5.000 MHz #2</code>.</p>
  <ul>
    <li><b>Engine</b> — which acquisition engine is actually in use (<code>PARLIO</code> or <code>CPU</code>)</li>
    <li><b>State</b> — <code>IDLE</code> / <code>SAMPLING</code> / <code>TRIG'D</code> / <code>UNTRIG</code> / <code>FAILED</code></li>
    <li><b>@ rate</b> — the effective sample rate, that is what the hardware really produces rather than what was requested</li>
    <li><b>#n</b> — capture count</li>
  </ul>

  <h2 id="measure">Reading the measurements</h2>
  <p>The MEASUREMENTS block reports per-channel statistics over the whole capture.</p>
  <ul>
    <li><b>Frequency</b> — derived from the mean rising-edge interval, so a burst that starts or ends mid-window does not skew it</li>
    <li><b>Duty cycle</b> — the mean high time over complete periods only</li>
    <li><b>Edges</b> — total transitions in the capture</li>
    <li>For aperiodic lines you get <code>N edges  min 120 ns</code>, the narrowest pulse seen</li>
  </ul>
  <div class="tip"><p>That minimum pulse width tells you whether the sample rate is adequate: if it is only two or three sample periods, raise the rate.</p></div>
""",
},
{
    "id": "capture",
    "section": "use",
    "title": "Capture and trigger",
    "body": """
  <h1>Capture and trigger</h1>
  <p class="lede">Choosing a rate and depth, and building a trigger condition.</p>

  <figure>
    <img src="img/02-capture-64k.png" alt="A 65.5 kSa capture taken at 5 MSa/s">
    <figcaption>65.5 kSa captured at 5 MSa/s, immediately after the sweep.</figcaption>
  </figure>

  <h2 id="rate">Rate and depth</h2>
  <p>Aim for <b>5 to 10 times</b> the fastest edge you care about. The capture window is <code>depth ÷ rate</code> and appears as <code>window</code> in the middle panel.</p>
  <table>
    <tr><th>Control</th><th>Effect</th></tr>
    <tr><td>Rate <code>-</code> <code>+</code></td><td>100 kSa/s to 80 MSa/s. Check the effective value after <code>@</code> in the status readout</td></tr>
    <tr><td>Depth <code>-</code> <code>+</code></td><td>64 kSa to 8 MSa. <b>Changing the depth discards the current capture</b>; changing the rate does not</td></tr>
    <tr><td><code>ENG</code></td><td><code>AUTO</code> / <code>PARLIO</code> / <code>CPU</code>. AUTO prefers PARLIO whenever it is available</td></tr>
  </table>
  <div class="note"><p>Rates are integer divisions of 160 MHz, and the menu only offers values that are actually reachable (80 / 53.33 / 40 / 32 / 26.67 / 22.86 / 20 MSa/s …). Exactly 24 MSa/s is not one of them, so use 26.67 MSa/s just above it.</p></div>

  <h2 id="trigger">Trigger conditions</h2>
  <figure>
    <img src="img/09-trigger.png" alt="The trigger overlay">
    <figcaption>The Trigger overlay. Tapping a channel cycles through the available conditions.</figcaption>
  </figure>
  <ul>
    <li>Each tap on a channel steps <code>--</code> → <code>Rise</code> → <code>Fall</code> → <code>Both</code> → <code>High</code> → <code>Low</code> → <code>--</code>.</li>
    <li><b>Edge conditions are OR'ed</b> with each other, <b>level conditions are AND'ed</b>, and when both are present the rule is "any edge AND every level".</li>
    <li>For example CH0=<code>Rise</code> with CH1=<code>Low</code> means "CH0 rises while CH1 is low".</li>
    <li><b>pre %</b> sets how much of the window precedes the trigger. At 25% the left quarter of the screen is pre-trigger data.</li>
    <li><b>Clear all</b> resets every channel to <code>--</code>. With no condition at all the capture is simply displayed as taken.</li>
  </ul>

  <h3>Trigger modes</h3>
  <table>
    <tr><th>Mode</th><th>When the condition is not found</th></tr>
    <tr><td>AUTO</td><td>Display the capture anyway</td></tr>
    <tr><td>NORMAL</td><td>Keep capturing and searching for 5 seconds, leaving the previous picture on screen</td></tr>
    <tr><td>SINGLE</td><td>One capture, then stop</td></tr>
  </table>

  <div class="note">
    <p>The trigger is a software search rather than a hardware comparator. The buffer is filled first and then scanned, so NORMAL mode loops capture → search → recapture. That is plenty for repetitive signals, but a rare one-shot event can fall between sweeps. It is a deliberate trade for never dropping a sample <i>during</i> a capture.</p>
  </div>
""",
},
{
    "id": "navigate",
    "section": "use",
    "title": "Navigation and cursors",
    "body": """
  <h1>Navigation and cursors</h1>
  <p class="lede">Panning, zooming, and timing measurements with the two cursors.</p>

  <figure>
    <img src="img/08-cursors.png" alt="Cursors A and B placed on the waveform">
    <figcaption>With both cursors placed, the middle panel shows Δt and its reciprocal.</figcaption>
  </figure>

  <table>
    <tr><th>Gesture</th><th>Action</th></tr>
    <tr><td>Drag</td><td>Pan horizontally</td></tr>
    <tr><td>Two-finger pinch</td><td>Zoom the time axis about the midpoint between the fingers</td></tr>
    <tr><td>Tap</td><td>Place the active cursor</td></tr>
    <tr><td><span class="kbd">Zoom-</span> <span class="kbd">Zoom+</span></td><td>Halve or double the span about the centre</td></tr>
    <tr><td><span class="kbd">Fit</span></td><td>Fit the whole capture on screen</td></tr>
    <tr><td><span class="kbd">Auto</span></td><td>Scale so the narrowest pulse is about 3 px wide (the default after a capture)</td></tr>
    <tr><td><span class="kbd">|&lt;</span> <span class="kbd">&gt;|</span></td><td>Jump to the start or the end</td></tr>
    <tr><td><span class="kbd">&lt;&lt;</span> <span class="kbd">&gt;&gt;</span></td><td>Scroll by half a screen</td></tr>
    <tr><td><span class="kbd">Trig</span></td><td>Jump to the trigger point</td></tr>
    <tr><td><span class="kbd">Cur A</span> <span class="kbd">Cur B</span></td><td>Choose which cursor a tap places; the active one is highlighted</td></tr>
    <tr><td><span class="kbd">Clr</span></td><td>Remove both cursors</td></tr>
  </table>

  <h2 id="autoscale">Why the view is not fitted after a capture</h2>
  <p>Fitting a whole capture into the plot means drawing 2 MSa across roughly 1180 pixels — about 1700 samples per column. Every channel collapses into a solid bar and no structure is visible at all.</p>
  <p>So after each capture the zoom is chosen from the data instead: <b>the median of the per-channel minimum pulse widths is scaled to about 3 pixels</b>. The median rather than the minimum, because a fast SPI clock sitting next to a slow UART would otherwise pull the zoom in so far that every other channel became a flat line. <span class="kbd">Fit</span> gives the overview when you want it, and <span class="kbd">Auto</span> returns to the automatic scale.</p>

  <h2 id="lod">Why glitches survive zooming out</h2>
  <p>Naively decimating 8 MSa down to 1150 columns would hide single-sample spikes, which defeats the purpose of a logic analyzer.</p>
  <p>Instead, each completed capture gets an <b>(OR, AND) summary pyramid</b>. OR answers "was this ever high", AND answers "was it always high", so however far you zoom out a one-sample spike still renders as "both levels present" — a vertical line. It is <b>decimation that does not lose information</b>.</p>
""",
},
{
    "id": "channels",
    "section": "use",
    "title": "Channel settings",
    "body": """
  <h1>Channel settings</h1>
  <p class="lede">Visibility and logical inversion.</p>

  <figure>
    <img src="img/10-channels.png" alt="The channels overlay">
    <figcaption>The Channels overlay.</figcaption>
  </figure>

  <ul>
    <li><b>SHOWN / hidden</b> — hiding a channel gives the remaining lanes more height, which is useful when you want to study two or three signals closely.</li>
    <li><b>INVERTED / normal</b> — logical inversion for open-collector lines or an inverting probe. It applies to the waveform, the measurements and to CSV and VCD export alike.</li>
  </ul>
  <p>The assigned GPIO appears under each channel name, and an inverted channel is marked <code>inv</code> in the left gutter.</p>
""",
},
{
    "id": "decode",
    "section": "use",
    "title": "Protocol decoding",
    "body": """
  <h1>Protocol decoding</h1>
  <p class="lede">UART, I2C and SPI annotations drawn under the waveform.</p>

  <figure>
    <img src="img/11-decode-options.png" alt="The decoder overlay">
    <figcaption>The Decode overlay. Changing any option re-runs the decoder immediately.</figcaption>
  </figure>

  <h2 id="i2c">I2C</h2>
  <figure>
    <img src="img/05-decode-i2c.png" alt="I2C decode output">
    <figcaption>Address <code>50 W</code> followed by data <code>50 5A 3C</code>, with ACK (<code>A</code>) and NAK (<code>N</code>) on the row below.</figcaption>
  </figure>
  <p>Assign SCL and SDA and that is all. <code>START</code>, <code>Sr</code> (repeated start), <code>STOP</code>, the address with its R/W bit and the data bytes appear on the upper row.</p>

  <h2 id="uart">UART</h2>
  <figure>
    <img src="img/06-decode-uart.png" alt="UART decode output">
    <figcaption>Data bytes <code>4C 'L'</code> and <code>41 'A'</code>, with <code>S</code> (start) and <code>T</code> (stop) below.</figcaption>
  </figure>
  <table>
    <tr><th>Option</th><th>Meaning</th></tr>
    <tr><td>Line</td><td>Channel carrying the signal</td></tr>
    <tr><td>Baud</td><td><code>auto</code> or a fixed value. Auto treats the narrowest observed pulse as one bit</td></tr>
    <tr><td>Data bits</td><td>5 to 9</td></tr>
    <tr><td>Parity</td><td>N / E / O</td></tr>
    <tr><td>Stop bits</td><td>1 or 2</td></tr>
    <tr><td>Polarity</td><td><code>idle high</code> for ordinary TTL serial, <code>inverted</code> after an inverting probe</td></tr>
    <tr><td>Bit order</td><td>LSB first in almost every case</td></tr>
  </table>
  <p>A frame whose stop bit is not high is flagged <code>T!</code> in red and its byte is drawn in the error colour. The decoder then resynchronises on the next edge instead of trusting the bit clock, so one corrupt byte does not destroy the rest of the stream.</p>
  <div class="note"><p>Auto baud assumes at least one single-bit-wide pulse exists. A stream of <code>0x00</code> or <code>0xFF</code> will fool it, so set the baud rate explicitly in that case.</p></div>

  <h2 id="spi">SPI</h2>
  <figure>
    <img src="img/07-decode-spi.png" alt="SPI decode output">
    <figcaption>MOSI on the upper row, MISO below. <code>CS\\</code> and <code>CS/</code> mark select and deselect.</figcaption>
  </figure>
  <p>CLK is required; MOSI, MISO and CS can each be disabled with <code>--</code>. With CS disabled the bus is treated as permanently selected. CPOL and CPHA choose the sampling edge — when CPOL equals CPHA, data is sampled on the rising edge.</p>
""",
},
{
    "id": "save",
    "section": "use",
    "title": "Saving and export",
    "body": """
  <h1>Saving and export</h1>
  <p class="lede">CSV, VCD, decoder output and screenshots on microSD.</p>

  <figure>
    <img src="img/12-save.png" alt="The save overlay">
    <figcaption>The Save overlay. The card is mounted on first use.</figcaption>
  </figure>

  <table>
    <tr><th>Button</th><th>File</th><th>Contents</th></tr>
    <tr><td>Save CSV (view)</td><td><code>/la/capNNNN.csv</code></td><td><b>The visible window only</b>, one row per sample. Time is relative to the trigger</td></tr>
    <tr><td>Save VCD (all)</td><td><code>/la/capNNNN.vcd</code></td><td>The whole capture, value changes only</td></tr>
    <tr><td>Save decode</td><td><code>/la/decNNNN.txt</code></td><td>Decoder output, tab separated</td></tr>
    <tr><td>Screenshot BMP</td><td><code>/la/shotNNNN.bmp</code></td><td>The full screen as a 24-bit BMP</td></tr>
  </table>

  <div class="note"><p>CSV is deliberately limited to the visible window: a full 8 MSa capture would be tens of megabytes and take minutes to write. Use VCD when you need everything — writing only transitions keeps it orders of magnitude smaller.</p></div>

  <h2 id="pulseview">Opening the VCD in PulseView or GTKWave</h2>
  <ol>
    <li>Copy the VCD to a PC</li>
    <li>In PulseView choose <i>Import Value Change Dump data</i></li>
    <li>The whole sigrok decoder collection is then available</li>
  </ol>
  <p>The timescale is chosen automatically as the coarsest unit that still gives at least ten steps per sample, so resolution is preserved at any rate.</p>
""",
},
{
    "id": "api",
    "section": "api",
    "title": "Remote control API",
    "body": """
  <h1>Remote control API</h1>
  <p class="lede">The USB CDC port used for flashing doubles as a line-oriented control API: send one line, get one line of JSON back.</p>

  <div class="warn">
    <p><b>Do not touch DTR or RTS.</b> On the ESP32 USB-Serial-JTAG those two lines are wired to reset and boot, so toggling them drops the board into the ROM bootloader and it stops responding. Open the port with both left false.</p>
  </div>

  <p>Log output is turned down, but ESP-IDF still prints a few lines at boot. <b>Skip any line that does not start with <code>{</code>.</b></p>
  <p>Commands are serviced <b>between</b> captures. A capture blocks for its whole sweep, so a <code>stop</code> sent mid-sweep takes effect before the next one.</p>

  <h2 id="api-basic">Basics</h2>
  <table>
    <tr><th>Command</th><th>Purpose</th></tr>
    <tr><td><code>ping</code></td><td>Liveness check and firmware information</td></tr>
    <tr><td><code>status</code></td><td>Full state snapshot; most commands return this same shape</td></tr>
    <tr><td><code>run</code> / <code>single</code> / <code>stop</code></td><td>Continuous capture / one capture / stop</td></tr>
    <tr><td><code>config [rate=Hz] [depth=samples] [engine=auto|parlio|cpu]</code></td><td><code>rate</code> snaps to the nearest menu entry, <code>depth</code> rounds up to a power of two</td></tr>
    <tr><td><code>trigger [mode=] [pos=] [chN=off|rise|fall|both|high|low] [clear=1]</code></td><td>Trigger condition</td></tr>
    <tr><td><code>channel n=0-7 [on=0|1] [inv=0|1]</code></td><td>Visibility and inversion</td></tr>
  </table>
  <pre><code>&gt; ping
{"ok":true,"fw":"0.1.0","api":1,"board":22,"channels":8,"parlio_built":true,"cpu_mhz":360}</code></pre>

  <h2 id="api-read">Reading the capture</h2>
  <table>
    <tr><th>Command</th><th>Purpose</th></tr>
    <tr><td><code>stats</code></td><td>Per-channel frequency, duty, edge count and minimum pulse width</td></tr>
    <tr><td><code>edges ch=N [from=] [count=]</code></td><td><b>The first choice for automation.</b> Returns transition positions only, orders of magnitude smaller than raw samples. Up to 2048 per call</td></tr>
    <tr><td><code>read [from=] [count=]</code></td><td>Raw samples as hex, one byte per sample with bit N being channel N. Up to 4096 per call</td></tr>
    <tr><td><code>ann [from=] [count=]</code></td><td>Decoder annotations, up to 512 per call</td></tr>
  </table>
  <pre><code>&gt; edges ch=0 count=8
{"ok":true,"ch":0,"from":0,"level":1,"sec_per_sample":3.75e-08,
 "edges":[13,27,40,54,67,81,94,108],"next":109,"more":true}</code></pre>

  <h2 id="api-decode">Decoders</h2>
  <pre><code>decode kind=i2c scl=0 sda=1
decode kind=uart line=2 baud=auto bits=8 parity=n stop=1
decode kind=spi clk=4 mosi=5 miso=6 cs=7 cpol=0 cpha=0 order=msb bits=8</code></pre>
  <p>Any change re-runs the decoder. <code>mosi</code>, <code>miso</code> and <code>cs</code> accept <code>-1</code> to disable that lane.</p>

  <h2 id="api-gen">Built-in test generator</h2>
  <p><code>gen ch=N [freq=Hz] [duty=0..100]</code> drives that channel's pin with LEDC while still sampling it, so <b>the whole path can be validated with nothing connected.</b></p>
  <pre><code>&gt; gen ch=0 freq=1000000 duty=50
{"ok":true,"gen":"on","ch":0,"pin":2,"freq":1000000,"duty":50,"res_bits":5}
&gt; gen off=1</code></pre>
  <p><code>res_bits</code> is the LEDC resolution, which shrinks as the frequency rises. Duty is quantised to 1/2<sup>res_bits</sup>, so exactly 50% is not achievable at high frequencies (with <code>res_bits=5</code> you get 15/32 = 46.9%).</p>

  <h2 id="api-example">A PowerShell helper</h2>
  <pre><code>$p = New-Object System.IO.Ports.SerialPort "COM6",115200,None,8,one
$p.DtrEnable = $false   # required: toggling drops the board into the bootloader
$p.RtsEnable = $false
$p.NewLine = "`n"
$p.Open()

function Send-Cmd([string]$c) {
  $p.WriteLine($c)
  $sb = New-Object System.Text.StringBuilder
  $deadline = (Get-Date).AddSeconds(15)
  while ((Get-Date) -lt $deadline) {
    $null = $sb.Append($p.ReadExisting())
    foreach ($ln in ($sb.ToString() -split "`n")) {
      $t = $ln.Trim()
      if ($t.StartsWith('{') -and $t.EndsWith('}')) { return $t | ConvertFrom-Json }
    }
    Start-Sleep -Milliseconds 15
  }
}

Send-Cmd 'ping'</code></pre>
  <p>The complete command reference is in <a href="API.md">docs/API.md</a>.</p>
""",
},
{
    "id": "arch",
    "section": "how",
    "title": "Architecture",
    "body": """
  <h1>Architecture</h1>
  <p class="lede">The acquisition engines, the trigger and the renderer, and the reasoning behind them.</p>

  <pre><code>main.cpp
  &#9492;&#9472; App                            application state machine + touch UI
       &#9500;&#9472; ISampler                  acquisition backend interface
       &#9474;    &#9500;&#9472; SamplerParlio        PARLIO RX + GDMA
       &#9474;    &#9492;&#9472; SamplerCpu           GPIO polling loop
       &#9500;&#9472; CaptureBuffer             PSRAM sample store + LOD pyramid
       &#9500;&#9472; findTrigger()             software trigger search
       &#9500;&#9472; measureChannels()          per-channel frequency / duty / edges
       &#9500;&#9472; runDecoder()               UART / I2C / SPI
       &#9500;&#9472; WaveformView               traces, ruler, annotations, cursors
       &#9492;&#9472; Exporter                   CSV / VCD / txt / BMP on microSD</code></pre>

  <h2 id="format">Sample format</h2>
  <p>One byte per sample, with bit N holding channel N. Packing eight channels into a byte means</p>
  <ul>
    <li>it matches PARLIO's <code>data_width = 8</code> exactly,</li>
    <li>the CPU engine's bit shuffle costs a handful of instructions,</li>
    <li>and the trigger search, the measurements and the decoders all become plain byte-array scans.</li>
  </ul>
  <p>Even at 8 MSa that is only 8 MB, which the Tab5's 32 MB of PSRAM absorbs comfortably.</p>

  <h2 id="parlio">The PARLIO engine</h2>
  <p>The ESP32-P4's PARLIO receiver latches all eight inputs on every edge of an internally divided clock and hands the bytes to GDMA. No CPU time is involved, so there is no jitter.</p>
  <p>The constraint that shapes everything is that the driver <b>refuses a DMA payload outside internal RAM</b>. So instead a 191 KiB ring lives in internal RAM and carries an <b>infinite transaction</b> (<code>partial_rx_en</code>). Because the descriptor chain is circular, the hardware never stops between blocks.</p>
  <p>How the data leaves that ring is what sets the achievable rate.</p>
  <table>
    <tr><th>Mode</th><th>Condition</th><th>Behaviour</th></tr>
    <tr><td><b>direct</b></td><td>depth ≤ 191 kSa</td><td>Nothing is copied while sampling. There is no real-time constraint at all and the rate is limited only by the peripheral</td></tr>
    <tr><td><b>stream</b></td><td>depth &gt; 191 kSa</td><td>Each finished descriptor is copied to PSRAM from the ISR, so the copy has to keep up with the sample rate</td></tr>
  </table>

  <h2 id="cpu">The CPU engine</h2>
  <p>It reads the GPIO input registers directly and packs the bits using a shift sequence expanded at compile time from the pin map in config.h. With the default map every channel is in bank 0, so that is a single register read.</p>
  <p>Pacing uses the RISC-V cycle counter rather than a delay loop: a deadline is computed per sample and the loop spins until it passes. When the requested rate is faster than the loop can go, the deadline is always in the past, the loop free-runs, and the <b>measured</b> rate is reported. That is what keeps the time axis honest.</p>
  <p>Interrupts are masked in chunks of about 2 ms. Masking for a multi-second capture would starve the tick and the watchdogs, so a small, reported gap at each chunk boundary is accepted instead.</p>

  <h2 id="trig-impl">The trigger</h2>
  <p>There is no hardware trigger, so the buffer is filled and <b>then</b> searched. A <code>TriggerConfig</code> folds down to four bitmasks (rise, fall, level-high, level-low), which makes the search loop a few instructions per sample.</p>
  <p>The search starts one pre-trigger length into the buffer, which guarantees the requested amount of pre-trigger data exists ahead of whatever it finds.</p>

  <h2 id="render">Rendering and flicker</h2>
  <p>The Tab5's MIPI-DSI panel has a single framebuffer (M5GFX configures it with <code>num_fbs = 1</code>) that is scanned out continuously. <b>Anything drawn straight to the display is visible the instant it lands — including the gap between clearing a region and repainting it.</b> That gap is the flicker.</p>
  <p>Three things address it.</p>
  <ul>
    <li><b>The plot is composed off-screen</b> into an <code>M5Canvas</code> in PSRAM (1184×454×2, about 1.03 MB) and pushed in one go, so no intermediate state ever reaches the panel.</li>
    <li><b>Column-at-a-time rendering.</b> Instead of clearing the whole plot and then painting channel by channel, each column's background, gridline, lane separators and all eight traces are drawn together. As a side effect the LOD pyramid is now consulted <b>once per column instead of eight times</b>, cutting the work per redraw by the same factor.</li>
    <li><b>The chrome updates incrementally.</b> Only buttons whose appearance changed are repainted, and a measurement value clears just the tail of its own field. No full-area <code>fillRect</code> remains anywhere.</li>
  </ul>
  <p>None of this competes with acquisition. Drawing only runs between captures — the loop is single threaded — so the push never contends with the DMA or the copy ISR for PSRAM bandwidth.</p>

  <h2 id="lod-impl">The LOD pyramid</h2>
  <p>When a capture completes, an (OR, AND) summary pyramid is built. Level 0 folds 64 samples into one entry and each level above halves again. It costs 2 bytes × (N/64) × 2 ≈ N/16 bytes, so about 512 KB even for 8 MSa.</p>
  <p>Zoomed in past one sample per pixel the pyramid is bypassed and the real samples are drawn as steps.</p>
""",
},
{
    "id": "perf",
    "section": "how",
    "title": "Measured performance",
    "body": """
  <h1>Measured performance</h1>
  <p class="lede">Numbers from real hardware: M5Stack Tab5, ESP32-P4 rev v1.3 at 360 MHz with 32 MB PSRAM.</p>

  <p>The built-in generator drove a 1 MHz square wave onto CH0 and the capture depth was 2 MSa.</p>
  <table>
    <tr><th>Requested</th><th class="num">Effective</th><th class="num">Captured</th><th class="num">1 MHz read as</th><th class="num">Error</th><th class="num">Copy ISR load</th></tr>
    <tr><td>20 MSa/s</td><td class="num">20.00</td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num">20%</td></tr>
    <tr><td>22.86 MSa/s</td><td class="num">22.86</td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num">22%</td></tr>
    <tr><td><b>26.67 MSa/s</b></td><td class="num"><b>26.67</b></td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num"><b>26%</b></td></tr>
    <tr><td>32 MSa/s</td><td class="num">32.00</td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num">31%</td></tr>
    <tr><td>40 MSa/s</td><td class="num">40.00</td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num">39%</td></tr>
    <tr><td>53.33 MSa/s</td><td class="num">53.33</td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num">52%</td></tr>
    <tr><td>80 MSa/s</td><td class="num">80.00</td><td class="num">2097152</td><td class="num">1.00 MHz</td><td class="num">0.00%</td><td class="num">77%</td></tr>
  </table>

  <div class="tip"><p><b>This beats the 24 MSa/s FX2LP analyzers outright.</b> At 26.67 MSa/s the copy ISR is only 26% busy, verified to a depth of 2 MSa, and the edge counts match theory exactly — nothing is being dropped.</p></div>

  <div class="warn"><p>80 MSa/s works but leaves little headroom at 77% ISR load, so <b>40 MSa/s or below</b> is the recommendation for routine use. Above 80% the Info panel reports <code>OVERRUN RISK</code>.</p></div>

  <h2 id="perf-input">Measured input frequency</h2>
  <p>Square waves from the built-in generator, captured at 80 MSa/s with a depth of 131072.</p>
  <table>
    <tr><th class="num">Generated</th><th class="num">Samples per period</th><th class="num">Measured</th><th class="num">Edges</th><th>Verdict</th></tr>
    <tr><td class="num">1.00 MHz</td><td class="num">80</td><td class="num">1.00 MHz</td><td class="num">3279</td><td>matches theory</td></tr>
    <tr><td class="num">1.95 MHz</td><td class="num">40</td><td class="num">1.95 MHz</td><td class="num">6379</td><td>matches</td></tr>
    <tr><td class="num">4.91 MHz</td><td class="num">16</td><td class="num">4.91 MHz</td><td class="num">16092</td><td>matches</td></tr>
    <tr><td class="num">9.74 MHz</td><td class="num">8</td><td class="num">9.74 MHz</td><td class="num">31908</td><td>matches</td></tr>
    <tr><td class="num">12.96 MHz</td><td class="num">6</td><td class="num">12.96 MHz</td><td class="num">42449</td><td>matches</td></tr>
    <tr><td class="num"><b>19.44 MHz</b></td><td class="num"><b>4.1</b></td><td class="num"><b>19.44 MHz</b></td><td class="num"><b>63695 / 63696</b></td><td>matched twice in a row</td></tr>
  </table>
  <p>The odd generated frequencies come from LEDC's fractional divider, not from any error in the analyzer — the same values reproduce run after run.</p>
  <div class="note"><p><b>Above 19.44 MHz is untested</b>, because the internal LEDC cannot generate it and an external source would be needed. It is not a limit of the peripheral.</p></div>

  <h2 id="perf-channels">Channel mapping verification</h2>
  <p>1 MHz was driven onto one channel at a time and captured at 20 MSa/s with a depth of 131072. The theoretical edge count is 13107.</p>
  <table>
    <tr><th>Driven</th><th>GPIO</th><th>Channels showing activity</th><th class="num">Measured</th><th class="num">Edges</th></tr>
    <tr><td>CH0</td><td>G2</td><td>CH0 only</td><td class="num">1.00 MHz</td><td class="num">13106</td></tr>
    <tr><td>CH1</td><td>G3</td><td>CH1 only</td><td class="num">1.00 MHz</td><td class="num">13107</td></tr>
    <tr><td>CH2</td><td>G4</td><td>CH2 only</td><td class="num">1.00 MHz</td><td class="num">13111</td></tr>
    <tr><td>CH3</td><td>G5</td><td>CH3 only</td><td class="num">1.00 MHz</td><td class="num">13111</td></tr>
    <tr><td>CH4</td><td>G16</td><td>CH4 only</td><td class="num">1.00 MHz</td><td class="num">13114</td></tr>
    <tr><td>CH5</td><td>G17</td><td>CH5 only</td><td class="num">1.00 MHz</td><td class="num">13107</td></tr>
    <tr><td>CH6</td><td>G18</td><td>CH6 only</td><td class="num">1.00 MHz</td><td class="num">13111</td></tr>
    <tr><td>CH7</td><td>G19</td><td>CH7 only</td><td class="num">1.00 MHz</td><td class="num">13111</td></tr>
  </table>
  <p>No channel swapping and no cross-talk. The trigger was verified on hardware too: <code>ch3=rise</code> with 25% pre-trigger landed at sample 32770 (just past the 32768 boundary), and <code>ch5=rise</code> on an idle line correctly returned <code>-1</code>.</p>

  <h2 id="perf-cpu">What the CPU engine can do</h2>
  <p>Asked for 5 MSa/s it delivered <b>2.811 MSa/s</b>, and read a 200 kHz signal as 200.06 kHz (0.03% error) — the reported rate is the measured one, so the time axis stays correct. That works out to roughly 128 cycles per sample, dominated by the PSRAM write.</p>
  <div class="note"><p><b>Treat 2.8 MSa/s as the CPU engine's ceiling.</b> It is a fallback for when PARLIO is unavailable, not something to use routinely.</p></div>

  <h2 id="perf-todo">Not yet verified</h2>
  <table>
    <tr><th>Item</th><th>Status</th></tr>
    <tr><td>microSD export</td><td><b>Untested.</b> The CSV, VCD and BMP writers have never been executed</td></tr>
    <tr><td>Touch input on hardware</td><td>Unconfirmed. UI interaction was only exercised with synthetic touches in the simulator</td></tr>
    <tr><td>Probing a real circuit</td><td>Everything so far was internal loopback; real wiring, grounding and signal integrity are unverified</td></tr>
    <tr><td>Decoding real bus traffic</td><td>Synthetic waveforms only, though the decoders are confirmed not to crash on garbage input</td></tr>
    <tr><td>Inputs above 19.44 MHz</td><td>Not measured — the internal generator cannot reach them</td></tr>
  </table>

  <h2 id="perf-naming">What to call it</h2>
  <table>
    <tr><th>Figure</th><th class="num">Value</th><th>Basis</th></tr>
    <tr><td><b>Sample rate</b></td><td class="num"><b>80 MSa/s</b>, 8 channels</td><td>Measured; edge counts match theory</td></tr>
    <tr><td>Recommended working rate</td><td class="num">40 MSa/s or below</td><td>80 MSa/s leaves the copy ISR 77% busy</td></tr>
    <tr><td>Minimum detectable pulse</td><td class="num">12.5 ns at 80 MSa/s</td><td>One sample period</td></tr>
    <tr><td>Verified input frequency</td><td class="num">19.44 MHz</td><td>Measured above; higher is untested</td></tr>
    <tr><td>Practical signal frequency</td><td class="num">8–20 MHz</td><td>Keeping 5–10× oversampling</td></tr>
  </table>
  <p>When a budget analyzer calls itself "24MHz", the 24 is its sample rate — by that convention this is an <b>80MHz</b> instrument. Because "80 MHz" invites the reading "can measure 80 MHz signals", <b>writing the unit out as <code>80 MSa/s</code> is recommended.</b></p>

  <h2 id="perf-notes">Notes on the measurements</h2>
  <ul>
    <li><b>The time axis uses the hardware divider value.</b> A wall-clock measurement stretches it by about 1% because arming and teardown are included, so that figure is shown for diagnostics only.</li>
    <li>The measurable signal frequency is not the sample rate. Reliable edge placement needs 5–10× oversampling, so 40 MSa/s is good for 4–8 MHz signals and 20 MSa/s for 2–4 MHz.</li>
    <li>The CPU engine's ceiling is set by loop speed and its measured rate is displayed. It is the fallback for when PARLIO is unavailable.</li>
  </ul>
""",
},
{
    "id": "sim",
    "section": "dev",
    "title": "Simulator",
    "body": """
  <h1>Browser simulator</h1>
  <p class="lede">Run the UI without hardware, and regenerate every screenshot in this manual.</p>

  <p>Every screenshot here is produced by <b>building the actual firmware UI code to WebAssembly</b>. These are not mock-ups: <code>WaveformView</code>, <code>App</code>, the decoders, the measurements and the LOD pyramid are byte-for-byte the same sources that run on the device.</p>

  <h2 id="sim-how">How it works</h2>
  <p>It builds on the M5GFX compatibility backend from <a href="https://github.com/airpocket-soundman/web-simulator-for-lvgl">web-simulator-for-lvgl</a>. That layer provides pixels, rectangles and lines but no text, so <code>sim/</code> supplies the rest.</p>
  <ul>
    <li><code>sim/sim_gfx.cpp</code> — text rendering, rounded rectangles and triangles. The font is Consolas rasterised by <code>sim/tools/make_font.py</code> at the same advance width as M5GFX's Font2/Font4, so labels never overflow their buttons.</li>
    <li><code>sim/shim/</code> — minimal stubs for M5Unified, Arduino, esp_heap_caps and esp_log.</li>
    <li><code>sim/sim_sampler.cpp</code> — an <code>ISampler</code> that synthesises a scene with I2C, UART, PWM and SPI all running at once.</li>
  </ul>
  <p>The UI never names <code>M5GFX</code> directly; it uses <code>LaGfx</code> from <code>include/gfx.h</code>, which is how the same sources compile for both targets.</p>

  <h2 id="sim-build">Building</h2>
  <p>Interactive HTML:</p>
  <pre><code>&amp; ..\\web-simulator-for-lvgl\\lvgl-sim.ps1 build .</code></pre>
  <p>That produces <code>build/lvgl-simulator/m5tab5-logic-analyzer.html</code>, which opens straight from the filesystem with no server.</p>

  <p>Regenerating the screenshots:</p>
  <pre><code>pwsh sim/tools/build_shots.ps1</code></pre>
  <p>The same sources are built as a headless Node program and driven through a scripted touch sequence, writing 13 images into <code>docs/img/</code>. <b>Because it is script-driven, the images are identical every time.</b></p>

  <h2 id="sim-found">Bugs the simulator caught</h2>
  <p>Taking these screenshots surfaced two UI bugs that were present on the device as well.</p>
  <ul>
    <li><b>Closing an overlay planted a cursor.</b> After a button handled the press, the release event fell through to the plot. Fixed by recording that the press was consumed.</li>
    <li><b>The frame after a tap drew stale button states.</b> The button array was built <i>before</i> input was dispatched. Fixed by rebuilding it just before drawing.</li>
  </ul>

  <div class="note"><p>The simulator is a UI preview, not a board emulator. PARLIO, microSD and the serial API do not work in it — the Save panel honestly reports that there is no card in a browser.</p></div>
""",
},
{
    "id": "trouble",
    "section": "dev",
    "title": "Troubleshooting",
    "body": """
  <h1>Troubleshooting</h1>
  <p class="lede">Start with the Info panel.</p>

  <figure>
    <img src="img/13-info.png" alt="The info overlay">
    <figcaption>The Info overlay reports the engine selection, the last capture mode, memory and the pin map.</figcaption>
  </figure>

  <table>
    <tr><th>Symptom</th><th>What to check</th></tr>
    <tr><td>Every channel sits high</td><td>The probes are floating and the internal pull-ups are holding them up. Is ground common with the circuit under test?</td></tr>
    <tr><td>The waveform looks ragged</td><td>The rate is too low. Raise it to 5–10× the signal; the minimum pulse width measurement is a good guide</td></tr>
    <tr><td>Stuck on <code>UNTRIG</code></td><td>The condition is too strict. Use Trigger → <b>Clear all</b> to see the raw waveform</td></tr>
    <tr><td>PARLIO is not selected</td><td>Info shows the reason. If internal RAM is short, reduce the depth</td></tr>
    <tr><td><code>OVERRUN RISK</code> appears</td><td>Lower the rate, or drop the depth to 191 kSa or less so it runs in direct mode</td></tr>
    <tr><td>Cannot write to the SD card</td><td>Check the error shown in the Save panel and use a FAT32 formatted card</td></tr>
    <tr><td>The rate is lower than expected</td><td>Check that ENG is not on <code>CPU</code>; that engine's ceiling is set by loop speed</td></tr>
    <tr><td>The API stops responding</td><td>Something toggled DTR/RTS and dropped the board into the bootloader. Recover with <code>esptool --after hard-reset chip-id</code></td></tr>
    <tr><td>PlatformIO fails to install its tools</td><td>Do not run it from Git Bash or MSYS; use PowerShell or cmd</td></tr>
  </table>
""",
},
]
