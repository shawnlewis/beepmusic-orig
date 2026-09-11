#include "WProgram.h"
#include "usb_serial.h"

#include <stdbool.h>

#define COMMAND_PAUSE 0
#define COMMAND_RESUME 1
#define COMMAND_ADJUST_VOLUME 2
#define COMMAND_SKIP 3

#define UPDATE_AUDIO_STATE 0
#define UPDATE_VOLUME 1

#define AUDIO_STATE_PLAYING 0
#define AUDIO_STATE_PAUSED 1
#define AUDIO_STATE_WORKING 2
#define AUDIO_STATE_SHUFFLING 3

#define N_COLS 5
#define N_ROWS 5
#define N_LEDS 24

#define BUTTON 9

#define ENC_A 11
#define ENC_B 10
#define N_ENCODER_POS 24

#define WIFLY_POWER 2

char buf[1024];

int cols[N_COLS] = {20, 22, 5, 6, 21};  // anodes
int rows[N_ROWS] = {17, 16, 15, 14, 13};  // cathodes

#define STATE_DEFAULT 0
#define STATE_VOLUME 1
int state = STATE_DEFAULT;
int audio_state = -1;

int volume = 0;
int volume_state_start_time = 0;

bool playing = false;

int led_states[N_LEDS];

int last_update_us = 0;
int led = N_COLS - 1;
int row = 0;


#define SCB_AIRCR_VECTKEY_Pos              16                                             /*!< SCB AIRCR: VECTKEY Position */
#define SCB_AIRCR_VECTKEY_Msk              (0xFFFFUL << SCB_AIRCR_VECTKEY_Pos)            /*!< SCB AIRCR: VECTKEY Mask */
#define SCB_AIRCR_SYSRESETREQ_Pos           2                                             /*!< SCB AIRCR: SYSRESETREQ Position */
#define SCB_AIRCR_SYSRESETREQ_Msk          (1UL << SCB_AIRCR_SYSRESETREQ_Pos)             /*!< SCB AIRCR: SYSRESETREQ Mask */
void softReset() {
    Serial.println("Rebooting...");
    delay(500);
    SCB_AIRCR = 0x05fa0000 | SCB_AIRCR_SYSRESETREQ_Msk;
    while(1);
}

int pow(int base, int exp) {
    if (exp == 0) {
        return 1;
    }
    for (int i=0; i<exp-1; i++) {
        base *= base;
    }
    return base;
}

int duty_from_brightness(int x) {
    // 1024 * (x/1024)**3 without under/overflowing
    int duty = 1024;
    for (int i=0; i<3; i++) {
        duty = duty * x / 1024;
    }
    return duty;
}

void update_leds() {
    int us = micros();
    if (us - last_update_us < 1000) {
        return;
    }
    last_update_us = us;

    // turn previous row off.
    digitalWriteFast(rows[row], HIGH);

    int col;
    for (col=0; col<N_COLS; col++) {
        if (led_states[led]) {
            digitalWriteFast(cols[col], HIGH);
            int duty = duty_from_brightness(led_states[led]);
            analogWrite(cols[col], duty);
        } else {
            analogWrite(cols[col], 0);
        }
        led = (led + 1) % N_LEDS;
        if (led == 0) {
            break;
        }
    }
    row = (row + 1) % N_ROWS;

    // Turn current row on
    digitalWriteFast(rows[row], LOW);
}

int old_encoder_state = 0;
int encoder_armed = 0;
int encoder_dir = 0;

int read_encoder() {
    int state = digitalReadFast(ENC_A) << 1 | digitalReadFast(ENC_B);
    if (state == old_encoder_state) {
        return 0;
    }
    old_encoder_state = state;
    switch (state) {
        case 0x3:
            //Serial.println("Encoder: 11");
            if (encoder_armed) {
                encoder_armed = false;
                return encoder_dir;
            }
            break;
        case 0x2:
            //Serial.println("Encoder: 10");
            encoder_dir = 1;
            break;
        case 0x1:
            //Serial.println("Encoder: 01");
            encoder_dir = -1;
            break;
        case 0x0:
            //Serial.println("Encoder: 00");
            encoder_armed = true;
            break;
    }
    return 0;
}

bool read_button() {
    return digitalReadFast(BUTTON);
}

int encoder_pos = 0;
bool prev_button_down;

int last_spin_update;
int spin_pos;
int end_spin_pos;

int flip_endian(int in) {
    return (in << 24) | ((in << 8) & 0x00ff0000)
            | ((in >> 8) & 0x0000ff00) | (in >> 24);
}

void wifly_command(const char* command) {
    Serial.println("-----");

    sprintf(buf, "Issuing command: %s", command);
    Serial.println(buf);

    sprintf(buf, "%s\r\n", command);
    Serial3.print(buf);

    Serial.println("-----");
    Serial.println("");
}

void wifly_command_mode() {
    Serial.println("Waiting for command mode");
    delay(300);
    Serial3.print("$$$");
    delay(300);

    // These are needed to ensure we get into command mode for some cases.
    Serial3.println("");
    Serial3.println("");

    Serial.println("Sent command mode");
    while (Serial3.available()) {
        Serial.print((char)Serial3.read());
    }
    Serial.println("---");
}

// Waits for a string to come over wifly serial. If not found within 5 seconds
// the system reboots.
void wifly_wait_string(const char* string) {
    sprintf(buf, "Waiting for %s", string);
    Serial.println(buf);

    int start_time = millis();

    sprintf(buf, "Start time: %d", start_time);
    Serial.println(buf);

    int matched_idx = 0;
    int len = strlen(string);
    while (1) {
        while (!Serial3.available()) {
            if (millis() - start_time > 5000) {
                Serial.println("wifly_wait_string failed.");
                softReset();
            }
        }
        char c = Serial3.read();
        Serial.print(c);
        if (string[matched_idx] == c) {
            matched_idx++;
        } else {
            matched_idx = 0;
        }
        if (matched_idx == len) {
            break;
        }
    }
}

void wifly_consume_rest() {
    int last_ms = millis();
    int ms = millis();
    while (ms - last_ms < 100) {
        if (Serial3.available()) {
            last_ms = millis();
            Serial.print((char)Serial3.read());
        }
        ms = millis();
    }
}

void wifly_init() {
    Serial3.begin(115200);

    wifly_command_mode();
    analogWrite(cols[1], 1000);
    wifly_command("reboot");
    wifly_wait_string("*READY*");
    analogWrite(cols[2], 1000);
}


void wifly_connect_beep() {
    wifly_wait_string("Listen on ");
    wifly_consume_rest();

    wifly_command_mode();

    wifly_command("show i");
    wifly_wait_string("4.00");
    wifly_consume_rest();

    // tcp client
    wifly_command("set ip proto 8");
    wifly_wait_string("4.00");
    wifly_consume_rest();

    wifly_command("set com remote 0");
    wifly_wait_string("4.00");
    wifly_consume_rest();

    analogWrite(cols[3], 1000);
    wifly_command("open 192.168.2.106 32201");
    wifly_wait_string("*OPEN*");

    analogWrite(cols[4], 1000);
    delay(500);

    Serial.println("---");
    Serial.println("---");
    Serial.println("---");
    //wifly_consume_rest();
}

void send_command(int command, int arg) {
    char send_buf[8];
    command = flip_endian(command);
    memcpy(send_buf, &command, 4);
    arg = flip_endian(arg);
    memcpy(send_buf+4, &arg, 4);

    Serial3.write((const uint8_t*) send_buf, 8);
}

void pause() {
    Serial.println("pausing");
    send_command(COMMAND_PAUSE, 0);
}

void resume() {
    Serial.println("resuming");
    send_command(COMMAND_RESUME, 0);
}

void skip() {
    Serial.println("skipping");
    send_command(COMMAND_SKIP, 0);
}

void adjust_volume(int volume) {
    send_command(COMMAND_ADJUST_VOLUME, volume * 41);
}

int audio_state_start_time = 0;

void paused_state_init() {
}

void playing_state_init() {
}

void on_update(int update, int arg) {
    sprintf(buf, "got update: %d %d", update, arg);
    Serial.println(buf);
    if (update == UPDATE_AUDIO_STATE) {
        audio_state = arg;
        audio_state_start_time = millis();
        if (audio_state == AUDIO_STATE_PAUSED) {
            playing = false;
            paused_state_init();
        } else if (audio_state == AUDIO_STATE_PLAYING) {
            playing = true;
            playing_state_init();
        }
    } else if (update == UPDATE_VOLUME) {
        volume = arg / 41;
        state = STATE_VOLUME;
        volume_state_start_time = millis();
    }
}

void setup_mode() {
    digitalWriteFast(rows[1], LOW);

    Serial.println("==========");
    Serial.println("Setup mode");
    Serial.println("==========");
    Serial.println("");
    wifly_init();

    analogWrite(cols[1], 1000);

    while (1) {
        if (Serial.available()) {
            Serial3.write(Serial.read());
        }
        if (Serial3.available()) {
            Serial.write(Serial3.read());
        }
    }
}

extern "C" int main(void) {
    // Setup

    Serial.begin(9600);

    pinMode(WIFLY_POWER, OUTPUT);

    Serial.println("hello");
    delay(2000);
    Serial.println("Starting");

    pinMode(ENC_A, INPUT);
    pinMode(ENC_B, INPUT);
    pinMode(BUTTON, INPUT);

    for (int i=0; i<N_COLS; i++) {
        pinMode(cols[i], OUTPUT);
        digitalWriteFast(cols[i], LOW);
        analogWriteFrequency(cols[i], 46875);
        analogWriteResolution(10);
    }
    for (int i=0; i<N_ROWS; i++) {
        pinMode(rows[i], OUTPUT);
        digitalWriteFast(rows[i], HIGH);
    }

    // show light 0, 0 for beginning of boot sequence.
    analogWrite(cols[0], 1000);
    digitalWriteFast(rows[0], LOW);

    // check button state
    if (read_button()) {
        delay(50);
        if (read_button()) {
            setup_mode();
        }
    }

    wifly_init();
    wifly_connect_beep();

    // main loop

    int ms;

    char updates[8];
    int update_idx = 0;

    int button_presses = 0;
    bool button_down;
    bool prev_button_down;
    int prev_button_debounce_time;

    int last_button_press = -1;

    int spin_pos = 0;
    int last_spin_time = 0;

    //int prev_millis = 0;
    //int j = 0;
    //while (1) {
    //    int m = millis();
    //    if (m - prev_millis > 5) {
    //        prev_millis = m;
    //        j = (j + 1) % 1024;
    //        for (int i=0; i<N_LEDS; i++) {
    //            led_states[i] = j;
    //        }
    //    }
    //    update_leds();
    //}

	while (1) {
        ms = millis();

        if (Serial3.available()) {
            updates[update_idx] = Serial3.read();
            if (updates[update_idx] == '*') {
                Serial.println("Connection received invalid value, potential close");
                softReset();
            }
            Serial.print(updates[update_idx]);
            update_idx++;
            if (update_idx == 8) {
                // Got a full update.
                update_idx = 0;

                int command;
                memcpy(&command, updates, 4);
                command = flip_endian(command);

                int args;
                memcpy(&args, updates+4, 4);
                args = flip_endian(args);

                on_update(command, args);
            }
        }

        bool button_reading = read_button();
        if (button_reading != prev_button_down) {
            prev_button_debounce_time = millis();
        }
        prev_button_down = button_reading;
        if (ms - prev_button_debounce_time > 50) {
            if (button_reading != button_down) {
                button_down = button_reading;
                if (button_down) {
                    sprintf(buf, "button pressed %d", button_presses);
                    if (last_button_press != -1
                            && ms - last_button_press < 300) {
                        skip();
                        last_button_press = -1;
                    } else {
                        last_button_press = ms;
                    }
                } else {
                    sprintf(buf, "button released %d", button_presses);
                    Serial.println(buf);
                    button_presses++;
                }
            }
        }

        if (last_button_press != -1 && ms - last_button_press > 300) {
            if (playing) {
                playing = false;
                pause();
            } else {
                playing = true;
                resume();
            }
            last_button_press = -1;
        }

        int enc = read_encoder();
        if (enc == 1) {
            encoder_pos= 1;
            if (encoder_pos == N_ENCODER_POS) {
                encoder_pos = 0;
            }
            adjust_volume(1);
        } else if (enc == -1) {
            encoder_pos -= 1;
            if (encoder_pos == -1) {
                encoder_pos = N_ENCODER_POS - 1;
            }
            adjust_volume(-1);
            //Serial.println("CW");
        }

        for (int i=0; i<N_LEDS; i++) {
            led_states[i] = 0;
        }
        switch (state) {
            case STATE_DEFAULT:
                switch (audio_state) {
                    case AUDIO_STATE_PLAYING:
                        break;
                    case AUDIO_STATE_PAUSED: {
                        int period = 5000;
                        int half = period / 2;
                        for (int i=0; i<N_LEDS; i++) {
                            int step = (ms - audio_state_start_time) % period;
                            if (step < half) {
                                led_states[i] = 1024 * step / half;
                            } else {
                                led_states[i] = (1024 * (period - step) / half);
                            }
                        }
                        break;
                    }
                    case AUDIO_STATE_WORKING:
                        if ((ms - last_spin_time) > 32) {
                        //if ((ms - last_spin_time) > 200) {
                            last_spin_time = ms;
                            spin_pos++;
                        }
                        //led_states[spin_pos % N_LEDS] = 64;
                        //led_states[(spin_pos + 1) % N_LEDS] = 128;
                        //led_states[(spin_pos + 2) % N_LEDS] = 256;
                        led_states[(spin_pos + 3) % N_LEDS] = 768;
                        led_states[(spin_pos + 4) % N_LEDS] = 1024;

                        //led_states[(spin_pos + 8) % N_LEDS] = 64;
                        //led_states[(spin_pos + 9) % N_LEDS] = 128;
                        //led_states[(spin_pos + 10) % N_LEDS] = 256;
                        led_states[(spin_pos + 11) % N_LEDS] = 768;
                        led_states[(spin_pos + 12) % N_LEDS] = 1024;

                        //led_states[(spin_pos + 16) % N_LEDS] = 64;
                        //led_states[(spin_pos + 17) % N_LEDS] = 128;
                        //led_states[(spin_pos + 18) % N_LEDS] = 256;
                        led_states[(spin_pos + 19) % N_LEDS] = 768;
                        led_states[(spin_pos + 20) % N_LEDS] = 1024;
                        break;
                }
                break;
            case STATE_VOLUME:
                for (int i=0; i<volume; i++) {
                    int b = 1024;
                    int delta = ms - volume_state_start_time;
                    if (delta > 1000) {
                        b = (2000 - delta) * 1024 / 1000;
                    }

                    led_states[i] = b;
                }
                if (ms - volume_state_start_time > 2000) {
                    state = DEFAULT;
                    // reset state timer
                    audio_state_start_time = ms;
                }
                break;
        }

        update_leds();
	}
}

