#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/uart.h>
#include <hardware/pwm.h>
#include <pico/stdlib.h>
#include <pico/stdio_usb.h>
#include "pdm_microphone.pio.h"
#include <stdio.h>
#include <string.h>

const uint MOTOR_LEFT       = 26;
const uint ULTRASOUND_TRIG  = 27;
const uint ULTRASOUND_ECHO  = 28;
const uint LED1             = 29;
const uint LED2             = 6;
const uint LED3             = 7;
const uint SPEAKER          = 0;
const uint SERVO            = 1;
const uint MOTOR_RIGHT      = 2;
const uint MIC_DATA         = 4;
const uint MIC_CLOCK        = 3;
const uint SERVO_PWM_WRAP   = 39062;
static constexpr size_t PDM_DECIMATION = 64;
static constexpr size_t PDM_WORDS_PER_SAMPLE = PDM_DECIMATION / 32;
static constexpr size_t PDM_WORD_COUNT = EI_CLASSIFIER_RAW_SAMPLE_COUNT * PDM_WORDS_PER_SAMPLE;

union AudioCaptureBuffer {
    uint32_t pdm_words[PDM_WORD_COUNT];
    int16_t pcm_samples[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
};

static AudioCaptureBuffer audio_buffer;
static size_t audio_slice_offset = 0;
static bool is_driving = false;

bool convert_pdm_to_pcm();
void play_rickroll();

#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659

int melody[] = {
  NOTE_D4, NOTE_E4, NOTE_G4, NOTE_E4, NOTE_B4, NOTE_B4, NOTE_A4,
  NOTE_D4, NOTE_E4, NOTE_G4, NOTE_E4, NOTE_A4, NOTE_A4, NOTE_G4,
  NOTE_D4, NOTE_E4, NOTE_G4, NOTE_E4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_A4, NOTE_G4
};

int noteDurations[] = {
  8, 8, 8, 8, 4, 4, 2,
  8, 8, 8, 8, 4, 4, 2,
  8, 8, 8, 8, 4, 8, 8, 8, 2
};

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    if (offset + length > EI_CLASSIFIER_SLICE_SIZE) {
        return -1;
    }
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = static_cast<float>(audio_buffer.pcm_samples[audio_slice_offset + offset + i]) / 32768.0f;
    }
    return 0;
}

bool capture_pdm_audio(PIO pio, uint sm, uint dma_channel) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_clkdiv_restart(pio, sm);

    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, true);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm, false));
    dma_channel_configure(
        dma_channel,
        &dma_config,
        audio_buffer.pdm_words,
        &pio->rxf[sm],
        PDM_WORD_COUNT,
        false
    );

    dma_start_channel_mask(1u << dma_channel);
    pio_sm_set_enabled(pio, sm, true);
    dma_channel_wait_for_finish_blocking(dma_channel);
    pio_sm_set_enabled(pio, sm, false);

    return convert_pdm_to_pcm();
}

bool convert_pdm_to_pcm() {
    uint32_t integrator1 = 0;
    uint32_t integrator2 = 0;
    uint32_t integrator3 = 0;
    uint32_t previous_integrator3 = 0;
    uint32_t previous_comb1 = 0;
    uint32_t previous_comb2 = 0;
    size_t pcm_index = 0;
    size_t decimation_count = 0;

    for (size_t word_index = 0; word_index < PDM_WORD_COUNT; word_index++) {
        uint32_t word = audio_buffer.pdm_words[word_index];
        for (int bit_index = 0; bit_index < 32; bit_index++) {
            int32_t bit = ((word >> bit_index) & 1u) ? 1 : -1;
            integrator1 += static_cast<uint32_t>(bit);
            integrator2 += integrator1;
            integrator3 += integrator2;

            if (++decimation_count == PDM_DECIMATION) {
                uint32_t comb1 = integrator3 - previous_integrator3;
                previous_integrator3 = integrator3;
                uint32_t comb2 = comb1 - previous_comb1;
                previous_comb1 = comb1;
                uint32_t comb3 = comb2 - previous_comb2;
                previous_comb2 = comb2;

                int64_t scaled = static_cast<int64_t>(static_cast<int32_t>(comb3)) * 32767 / 262144;
                if (scaled > 32767) scaled = 32767;
                if (scaled < -32768) scaled = -32768;
                audio_buffer.pcm_samples[pcm_index++] = static_cast<int16_t>(scaled);
                decimation_count = 0;
            }
        }
    }

    return pcm_index == EI_CLASSIFIER_RAW_SAMPLE_COUNT;
}

void handle_classifier_result(const ei_impulse_result_t& result) {
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        float confidence = result.classification[i].value;
        const char* word = ei_classifier_inferencing_categories[i];
        printf("  %s: %.5f\n", word, confidence);

        if (confidence < 0.85f) {
            continue;
        }
        if (strcmp(word, "drive") == 0) {
            is_driving = true;
            gpio_put(LED1, 1);
        } else if (strcmp(word, "stop") == 0) {
            is_driving = false;
            gpio_put(MOTOR_LEFT, 0);
            gpio_put(MOTOR_RIGHT, 0);
            gpio_put(LED1, 0);
        } else if (strcmp(word, "music") == 0) {
            play_rickroll();
        } else if (strcmp(word, "lights") == 0) {
            gpio_put(LED1, 1);
            gpio_put(LED2, 1);
            gpio_put(LED3, 1);
            sleep_ms(500);
            gpio_put(LED1, 0);
            gpio_put(LED2, 0);
            gpio_put(LED3, 0);
        }
    }
}

bool initialize_microphone(PIO pio, uint sm, uint offset) {
    pio_gpio_init(pio, MIC_CLOCK);
    pio_gpio_init(pio, MIC_DATA);
    pio_sm_set_consecutive_pindirs(pio, sm, MIC_CLOCK, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, MIC_DATA, 1, false);

    pio_sm_config config = pdm_microphone_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, MIC_CLOCK);
    sm_config_set_in_pins(&config, MIC_DATA);
    sm_config_set_in_shift(&config, true, true, 32);
    constexpr float PIO_CYCLES_PER_BIT = 4.0f + (1.0f / 32.0f);
    float divider = static_cast<float>(clock_get_hz(clk_sys)) /
        (EI_CLASSIFIER_FREQUENCY * PDM_DECIMATION * PIO_CYCLES_PER_BIT);
    sm_config_set_clkdiv(&config, divider);
    pio_sm_init(pio, sm, offset, &config);
    pio_sm_clear_fifos(pio, sm);
    return true;
}

void play_tone(uint pin, uint frequency, uint duration_ms) {
    if (frequency == 0) {
        sleep_ms(duration_ms);
        return;
    }
    uint period_us = 1000000 / frequency;
    uint pulse_us = period_us / 2;
    uint32_t cycles = (duration_ms * 1000) / period_us;
    
    for (uint32_t i = 0; i < cycles; i++) {
        gpio_put(pin, 1);
        sleep_us(pulse_us);
        gpio_put(pin, 0);
        sleep_us(pulse_us);
    }
}

void play_rickroll() {
    int totalNotes = sizeof(melody) / sizeof(melody[0]);
    for (int thisNote = 0; thisNote < totalNotes; thisNote++) {
        int noteDuration = 1000 / noteDurations[thisNote];
        play_tone(SPEAKER, melody[thisNote], noteDuration);
        sleep_ms(noteDuration * 0.30);
    }
}

float read_distance_cm() {
    gpio_put(ULTRASOUND_TRIG, 0);
    sleep_us(2);
    gpio_put(ULTRASOUND_TRIG, 1);
    sleep_us(10);
    gpio_put(ULTRASOUND_TRIG, 0);

    absolute_time_t timeout = make_timeout_time_us(30000);
    while (gpio_get(ULTRASOUND_ECHO) == 0) {
        if (time_reached(timeout)) {
            return -1.0f;
        }
        tight_loop_contents();
    }

    absolute_time_t start_time = get_absolute_time();
    timeout = make_timeout_time_us(30000);
    while (gpio_get(ULTRASOUND_ECHO) == 1) {
        if (time_reached(timeout)) {
            return -1.0f;
        }
        tight_loop_contents();
    }
    absolute_time_t end_time = get_absolute_time();

    int64_t pulse_duration = absolute_time_diff_us(start_time, end_time);
    float distance = (float)pulse_duration / 58.0f;
    return distance;
}

void set_servo_angle(uint pin, float angle) {
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;
    float pulse_width_ms = 0.5f + (angle / 180.0f) * 2.0f;
    uint16_t level = static_cast<uint16_t>((pulse_width_ms / 20.0f) * (SERVO_PWM_WRAP + 1));
    pwm_set_gpio_level(pin, level);
}

int main() {
    stdio_usb_init();

    gpio_init(MOTOR_LEFT);
    gpio_set_dir(MOTOR_LEFT, GPIO_OUT);
    gpio_init(MOTOR_RIGHT);
    gpio_set_dir(MOTOR_RIGHT, GPIO_OUT);

    gpio_init(LED1); gpio_set_dir(LED1, GPIO_OUT);
    gpio_init(LED2); gpio_set_dir(LED2, GPIO_OUT);
    gpio_init(LED3); gpio_set_dir(LED3, GPIO_OUT);

    gpio_init(SPEAKER);
    gpio_set_dir(SPEAKER, GPIO_OUT);
    gpio_init(ULTRASOUND_TRIG);
    gpio_set_dir(ULTRASOUND_TRIG, GPIO_OUT);
    gpio_init(ULTRASOUND_ECHO);
    gpio_set_dir(ULTRASOUND_ECHO, GPIO_IN);

    gpio_set_function(SERVO, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(SERVO);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 64.0f);
    pwm_config_set_wrap(&config, SERVO_PWM_WRAP);
    pwm_init(slice_num, &config, true);

    PIO microphone_pio = pio0;
    uint microphone_sm = pio_claim_unused_sm(microphone_pio, true);
    uint microphone_offset = pio_add_program(microphone_pio, &pdm_microphone_program);
    initialize_microphone(microphone_pio, microphone_sm, microphone_offset);
    uint microphone_dma = dma_claim_unused_channel(true);
    run_classifier_init();
    printf("PDM microphone ready: %u Hz, GPIO data %u, clock %u\n",
        EI_CLASSIFIER_FREQUENCY, MIC_DATA, MIC_CLOCK);
    ei_impulse_result_t result = {nullptr};

    while (true) {
        if (!capture_pdm_audio(microphone_pio, microphone_sm, microphone_dma)) {
            gpio_put(MOTOR_LEFT, 0);
            gpio_put(MOTOR_RIGHT, 0);
            printf("PDM conversion failed; motors stopped.\n");
            continue;
        }

        for (size_t slice = 0; slice < EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW; slice++) {
            audio_slice_offset = slice * EI_CLASSIFIER_SLICE_SIZE;
            signal_t audio_signal;
            audio_signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
            audio_signal.get_data = &raw_feature_get_data;

            EI_IMPULSE_ERROR res = run_classifier_continuous(&audio_signal, &result, false);
            if (res != EI_IMPULSE_OK) {
                printf("Classifier error: %d\n", res);
                continue;
            }

            printf("Timing: DSP %d ms, inference %d ms\n", result.timing.dsp, result.timing.classification);
            handle_classifier_result(result);
        }

        if (is_driving) {
            float distance_center = read_distance_cm();

            if (distance_center < 0.0f) {
                gpio_put(MOTOR_LEFT, 0);
                gpio_put(MOTOR_RIGHT, 0);
            } else if (distance_center > 20.0f) {
                gpio_put(MOTOR_LEFT, 1);
                gpio_put(MOTOR_RIGHT, 1);
            } else {
                gpio_put(MOTOR_LEFT, 0);
                gpio_put(MOTOR_RIGHT, 0);

                set_servo_angle(SERVO, 150.0f);
                sleep_ms(400);
                float left_dist = read_distance_cm();

                set_servo_angle(SERVO, 30.0f);
                sleep_ms(500);
                float right_dist = read_distance_cm();

                set_servo_angle(SERVO, 90.0f);
                sleep_ms(300);

                if (left_dist < 0.0f || right_dist < 0.0f) {
                    gpio_put(MOTOR_LEFT, 0);
                    gpio_put(MOTOR_RIGHT, 0);
                } else if (left_dist > right_dist) {
                    gpio_put(MOTOR_LEFT, 0);
                    gpio_put(MOTOR_RIGHT, 1);
                    sleep_ms(600);
                } else {
                    gpio_put(MOTOR_LEFT, 1);
                    gpio_put(MOTOR_RIGHT, 0);
                    sleep_ms(600);
                }
                
                gpio_put(MOTOR_LEFT, 0);
                gpio_put(MOTOR_RIGHT, 0);
            }
        }

    }
}
