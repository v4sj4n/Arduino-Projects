**Security System**

**Objectives**
- **Goal**: Project and implement an electronic security system based on an Arduino Uno.
- **Specifics**: Use a 4x4 keypad for input, a 16x2 I²C LCD for output, a confirmation button, and a passive buzzer for feedback.

**Theoretical Analysis**
- **Keypad Scanning**: A 4x4 matrix keypad uses 8 pins (4 rows, 4 columns). The MCU drives rows and reads columns (with pull-ups) to detect key presses; N=(P/2)^2 gives 16 keys for P=8.
- **I²C LCD**: Uses SDA/SCL (A4/A5) and address 0x27 to reduce pin usage compared to parallel interfacing.
- **Input Pull-Up Button**: Confirmation button configured with `INPUT_PULLUP` (active LOW). Unpressed = HIGH (internal pull-up), pressed = LOW; software debouncing applied (delay(50)).
- **Passive Buzzer**: Driven with square waves (tone()) to generate feedback tones. Example frequencies: keypress blip ~2–3kHz; success tones 2000→3000Hz; error tones ~150–200Hz.

**Circuit Implementation & Pin Configuration**
- **LCD (I²C)**: SDA -> SDA/A4, SCL -> SCL/A5 (I²C backpack, address 0x27).
- **4x4 Keypad**:
  - **Rows (R0–R3)**: D9, D8, D7, D6
  - **Cols (C0–C3)**: D5, D4, D3, D2
- **Controls & Outputs**:
  - **Enter/Confirm Button**: D11, `INPUT_PULLUP` (active LOW)
  - **Passive Buzzer**: D12

**Software Logic & Security Algorithms**
- **State Machine**: System follows a state-machine architecture for Setup, Idle/Enter Code, Locked, and Access states.
- **EEPROM Persistence**: Magic byte (0xBB) at address 0 determines whether a password is configured. Lock state and remaining penalty time are saved so a power cycle cannot bypass lockout.
- **Exponential Backoff**: After 3 failed attempts the system locks with an initial penalty (30s). Subsequent failures double the penalty (Tnew = Told × 2) to mitigate brute-force attacks.
- **Acoustic Feedback Protocol**: Distinct tones for input masking (keypress blip), success (rising arpeggio), and error (descending tone).

**Experimental Results**
- **Setup Mode**: Entered a new code (e.g., "1234") + confirm -> code saved to EEPROM; display shows "Saved to MEM". Status: PASS
- **Valid Access**: Enter correct code + confirm -> "ACCESS GRANTED" and success tone. Status: PASS
- **Invalid Access**: Wrong code + confirm -> "ACCESS DENIED" and error tone. Status: PASS
- **Security Lockout**: 3 consecutive wrong codes -> system locks; timer on LCD; keypad disabled. Status: PASS
- **Power Loss Test**: Power cut during lockout -> on reboot system shows "SYSTEM RELOCKED" and resumes timer. Status: PASS
- **Timeout**: Key pressed then 8s idle -> input buffer clears; LCD resets to "ENTER CODE". Status: PASS

**Conclusions**
- **Outcome**: Project met the objectives: keypad input, I²C LCD output, confirmation button, and passive buzzer integration.
- **Strengths**: EEPROM persistence prevents simple power-cycle attacks; exponential backoff increases security against brute-force attempts; auditory feedback improves usability.
- **Overall**: The system is robust, secure against basic attacks, and user-friendly.

---

For the project source and firmware refer to the repository files (index.ino and related documentation).
# Arduino-Projects