#include <Adafruit_GFX.h>
#include <mbed.h>

constexpr int W = 64, H = 32, ROWS = H / 2;
constexpr int PLANES = 3;          // brightness levels per channel (3 = 8 levels, 512 colours)
constexpr uint32_t BASE_US = 40;   // shortest on-time for one row; lower = faster refresh, more CPU

// Pin numbers set by the Adafruit RGB Matrix Shield
const int PIN_RGB[6]  = {2, 3, 4, 5, 6, 7};   // R1 G1 B1 R2 G2 B2
const int PIN_ADDR[4] = {A0, A1, A2, A3};     // A B C D
const int PIN_CLK = 8, PIN_OE = 9, PIN_LAT = 10;

mbed::DigitalOut *rgb[6], *addr[4], *clk, *oe, *lat;
mbed::Timeout refreshTimer;

GFXcanvas16 canvas(W, H);                     // draw here with any Adafruit_GFX call
uint8_t fb[2][PLANES][ROWS][W];               // two buffers: one shown, one being filled
volatile uint8_t front = 0;
int curRow = 0, curPlane = 0;

mbed::DigitalOut *mk(int pin, int v = 0) { return new mbed::DigitalOut(digitalPinToPinName(pin), v); }

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void refreshISR() {
  const uint8_t *line = fb[front][curPlane][curRow];
  for (int x = 0; x < W; x++) {               // send one row (top + bottom half) to the panel
    uint8_t b = line[x];
    for (int i = 0; i < 6; i++) rgb[i]->write((b >> i) & 1);
    clk->write(1); clk->write(0);
  }
  oe->write(1);                               // turn LEDs off
  for (int i = 0; i < 4; i++) addr[i]->write((curRow >> i) & 1);
  lat->write(1); lat->write(0);               // show the row just sent
  oe->write(0);                               // LEDs back on
  refreshTimer.attach(&refreshISR, std::chrono::microseconds(BASE_US << curPlane));
  if (++curPlane == PLANES) { curPlane = 0; curRow = (curRow + 1) % ROWS; }
}

uint8_t planeBits(uint16_t c, int p) {
  uint8_t r = (c >> 11) >> (5 - PLANES), g = ((c >> 5) & 0x3F) >> (6 - PLANES), b = (c & 0x1F) >> (5 - PLANES);
  return ((r >> p) & 1) | (((g >> p) & 1) << 1) | (((b >> p) & 1) << 2);
}

void show() {                                 // copy the canvas to the panel
  uint8_t back = front ^ 1;
  for (int p = 0; p < PLANES; p++)
    for (int y = 0; y < ROWS; y++)
      for (int x = 0; x < W; x++)
        fb[back][p][y][x] = planeBits(canvas.getPixel(x, y), p) | (planeBits(canvas.getPixel(x, y + ROWS), p) << 3);
  front = back;
}

void setup() {
  for (int i = 0; i < 6; i++) rgb[i] = mk(PIN_RGB[i]);
  for (int i = 0; i < 4; i++) addr[i] = mk(PIN_ADDR[i]);
  clk = mk(PIN_CLK); lat = mk(PIN_LAT); oe = mk(PIN_OE, 1);
  memset(fb, 0, sizeof(fb));
  refreshISR();                               // starts the self-repeating refresh
}

void loop() {
  static int x = W;
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, rgb565(0, 0, 255));
  canvas.fillCircle(8, 16, 5, rgb565(255, 0, 0));
  canvas.fillTriangle(50, 22, 56, 10, 62, 22, rgb565(0, 255, 0));
  canvas.setTextColor(rgb565(255, 255, 0));
  canvas.setTextWrap(false);
  canvas.setCursor(x, 12);
  canvas.print("Hello Giga!");
  show();
  if (--x < -66) x = W;
  delay(40);                                  // fine: refresh runs on a timer interrupt, so delay() won't freeze the display
}
