#include <Arduino_AdvancedAnalog.h>
#include <Arduino_USBHostMbed5.h>
#include <FATFileSystem.h>
#include <DigitalOut.h>
#include <WavReader.h>

// ---------------------------------------------------------------------------
// USB Host Objects (They automatically share the underlying USBHost singleton)
// ---------------------------------------------------------------------------
USBHostMSD msd;
mbed::FATFileSystem usb("USB");
USBHostKeyboard keyboard;
USBHostMouse mouse;

// ---------------------------------------------------------------------------
// Audio / I2S Objects
// ---------------------------------------------------------------------------
WavReader wav;
// WS, CK, SDI, SDO, MCK -> LRC, BCLK, NA, DIN, NA
AdvancedI2S i2s(PI_0, PI_1, NC, PI_3, NC); // D69, D70, na, D72, na
#define N_SAMPLES (512)

int file = 0;
String files[] = {"BlastProcessStereofix"};

// ---------------------------------------------------------------------------
// Non-Blocking State Tracking
// ---------------------------------------------------------------------------
bool msdMounted = false;
bool audioPlaying = false;
bool kbConnected = false;
bool mouseConnected = false;
unsigned long lastUsbCheck = 0;

// Volatile variables for lock-free ISR-to-loop communication
volatile bool newKeyEvent = false;
volatile int lastKey = 0;

volatile bool newMouseEvent = false;
volatile uint8_t lastButtons = 0;
volatile int8_t lastMouseX = 0;
volatile int8_t lastMouseY = 0;
volatile int8_t lastMouseZ = 0;

// ---------------------------------------------------------------------------
// Interrupt Callbacks (Keep ultra-short! NO Serial.print here)
// ---------------------------------------------------------------------------
void onKeyPress(int key) {
  lastKey = key;
  newKeyEvent = true;
}

void onMouseEvent(uint8_t buttons, int8_t x, int8_t y, int8_t z) {
  lastButtons = buttons;
  lastMouseX = x;
  lastMouseY = y;
  lastMouseZ = z;
  newMouseEvent = true;
}

// ---------------------------------------------------------------------------
// Helper: Mount and Start Audio
// ---------------------------------------------------------------------------
void startAudioPlayback() {
  Serial.println("Mounting USB device...");
  int const rc_mount = usb.mount(&msd);
  if (rc_mount) {
    Serial.print("Error mounting USB device: ");
    Serial.println(rc_mount);
    return;
  }
  msdMounted = true;

  Serial.println("Opening audio file...");
  String f2 = (("/USB/Vihaan/BoomBoxMedia/" + files[file]) + ".wav");
  if (!wav.begin(f2.c_str(), N_SAMPLES, 1, true)) {
    Serial.print("Error opening audio file: ");
    Serial.println(f2);
    return;
  }

  Serial.print("Sample rate: ");
  Serial.println(wav.sample_rate());
  Serial.print("Channels: ");
  Serial.println(wav.channels());
  Serial.print("Resolution: ");
  Serial.println(wav.resolution());
  Serial.print("Sample count: ");
  Serial.println(wav.sample_count());

  if (!i2s.begin(AN_I2S_MODE_OUT, wav.sample_rate(), N_SAMPLES, 64)) {
    Serial.println("Failed to start I2S");
    return;
  }

  Serial.println("Playing audio file...");
  audioPlaying = true;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200); // Bumped to 115200 so Serial prints don't block I2S

  // Enable power for HOST USB connector
  pinMode(PA_15, OUTPUT);
  digitalWrite(PA_15, HIGH);
  delay(500); // Brief settle time for the USB Hub power rail

  Serial.println("USB Host Hub Power Enabled. Waiting for devices...");
}

// ---------------------------------------------------------------------------
// Main Non-Blocking Loop
// ---------------------------------------------------------------------------
void loop() {
  // 1. HIGH PRIORITY: Feed the I2S DMA Queue
  if (audioPlaying && i2s.available() && wav.available()) {
    SampleBuffer i2s_buf = i2s.dequeue();
    SampleBuffer pcm_buf = wav.read();

    for (size_t i = 0; i < N_SAMPLES * wav.channels(); i++) {
      if (wav.channels() == 2) {
        i2s_buf[i] = pcm_buf[i];
      } else {
        i2s_buf[(i * 2) + 0] = pcm_buf[i];
        i2s_buf[(i * 2) + 1] = pcm_buf[i];
      }
    }

    i2s.write(i2s_buf);
    pcm_buf.release();
  }

  // 2. MEDIUM PRIORITY: Process Keyboard & Mouse Events
  if (newKeyEvent) {
    newKeyEvent = false;
    char k = (char)lastKey;
    Serial.print("Key Pressed: ");
    Serial.print(k);
    Serial.print(" (ASCII: ");
    Serial.print(lastKey);
    Serial.println(")");
  }

  if (newMouseEvent) {
    newMouseEvent = false;
    Serial.print("Mouse -> Buttons: ");
    Serial.print(lastButtons);
    Serial.print(" | dX: ");
    Serial.print(lastMouseX);
    Serial.print(" | dY: ");
    Serial.print(lastMouseY);
    Serial.print(" | Wheel: ");
    Serial.println(lastMouseZ);
  }

  // 3. LOW PRIORITY: Non-Blocking USB Hub Device Polling (Every 500ms)
  if (millis() - lastUsbCheck >= 500) {
    lastUsbCheck = millis();

    // Check USB Stick (MSD)
    if (!msd.connected()) {
      if (msd.connect()) {
        Serial.println("USB Stick connected via Hub!");
        if (!msdMounted) {
          startAudioPlayback();
        }
      }
    }

    // Check Keyboard
    if (!keyboard.connected()) {
      kbConnected = false;
      if (keyboard.connect()) {
        Serial.println("Keyboard connected via Hub!");
        keyboard.attach(onKeyPress);
        kbConnected = true;
      }
    }

    // Check Mouse
    if (!mouse.connected()) {
      mouseConnected = false;
      if (mouse.connect()) {
        Serial.println("Mouse connected via Hub!");
        mouse.attachEvent(onMouseEvent);
        mouseConnected = true;
      }
    }
  }
}