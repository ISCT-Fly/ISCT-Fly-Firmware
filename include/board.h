/* FlapFC V2 controller board (STM32F446RET6, LQFP64) - pin map and board-level API.
 * Pin assignment of the 30 x 30 mm board (EasyEDA project Kero-Hardware, board
 * ControllerBoard_30x30_4L_backup), checked against its netlist backup backup_30x30_sch.json.
 * The smaller board variants in that project use different pins. */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- LEDs, active high, 1.5k series ---- */
#define LED_PORT        GPIOB
#define LED_G_PIN       GPIO_PIN_1   /* LED2 green */
#define LED_B_PIN       GPIO_PIN_2   /* LED3 blue (PB2 is also BOOT1) */
#define LED_R_PIN       GPIO_PIN_10  /* LED4 red */
#define LED_G           0x01u
#define LED_B           0x02u
#define LED_R           0x04u

/* ---- nRF24L01+ on SPI1 (PA5 SCK, PA6 MISO, PA7 MOSI) ---- */
#define RF_CE_PORT      GPIOA
#define RF_CE_PIN       GPIO_PIN_3
#define RF_CSN_PORT     GPIOA
#define RF_CSN_PIN      GPIO_PIN_4
#define RF_IRQ_PORT     GPIOC
#define RF_IRQ_PIN      GPIO_PIN_4   /* active low, EXTI4 */

/* ---- BMI088 on SPI3 (PB3 SCK, PB4 MISO, PB5 MOSI) ---- */
#define ACC_CS_PORT     GPIOB
#define ACC_CS_PIN      GPIO_PIN_6   /* CSB1 */
#define GYRO_CS_PORT    GPIOA
#define GYRO_CS_PIN     GPIO_PIN_15  /* CSB2 */
#define ACC_INT_PORT    GPIOB
#define ACC_INT_PIN     GPIO_PIN_12  /* INT1, EXTI12 */
#define GYRO_INT_PORT   GPIOB
#define GYRO_INT_PIN    GPIO_PIN_7   /* INT3, EXTI7 */

/* ---- I2C1 (PB8 SCL, PB9 SDA, 2.2k pull-ups): BMP388 + VL53L1X ---- */
#define I2C_PORT        GPIOB
#define I2C_SCL_PIN     GPIO_PIN_8
#define I2C_SDA_PIN     GPIO_PIN_9
#define BMP388_ADDR     0x76u
#define VL53L1X_ADDR    0x29u
#define BARO_INT_PORT   GPIOC
#define BARO_INT_PIN    GPIO_PIN_15  /* EXTI15 */
#define TOF_XSHUT_PORT  GPIOC
#define TOF_XSHUT_PIN   GPIO_PIN_5   /* low = sensor off, 10k pull-up on board */
#define TOF_INT_PORT    GPIOB
#define TOF_INT_PIN     GPIO_PIN_0   /* GPIO1, 10k pull-up, EXTI0 */

/* ---- Power monitoring ---- */
#define BAT_SENSE_ADC_CH  ADC_CHANNEL_10 /* PC0 = VSYS / 2 (100k/100k) */
#define CHG_DET_PORT    GPIOC
#define CHG_DET_PIN     GPIO_PIN_2   /* MCP73831 STAT through 10k: low = charging */
#define VBUS_DET_PORT   GPIOA
#define VBUS_DET_PIN    GPIO_PIN_9   /* VBUS / 2 */

/* ---- Actuators ----
 * Servos: PA0/PA1/PA2 = TIM2_CH1..3 (50 Hz).  Motor: PA8 = TIM1_CH1 (20 kHz, high = motor on).
 * Encoder: PC6/PC7 = TIM3_CH1/CH2 (quadrature). */
#define SERVO_COUNT     3
#define MOTOR_PWM_HZ    20000u

/* ---- Debug UART: USART3 PC10 TX / PC11 RX, 115200 8N1 ---- */
/* ---- SDIO 1-bit: PC8 D0, PC12 CK, PD2 CMD ---- */
/* ---- USB FS device: PA11 DM, PA12 DP ---- */

enum { IRQ_TOF, IRQ_RF, IRQ_GYRO, IRQ_ACC, IRQ_BARO, IRQ_N };

extern SPI_HandleTypeDef hspi_rf;
extern SPI_HandleTypeDef hspi_imu;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim_servo;
extern TIM_HandleTypeDef htim_motor;
extern TIM_HandleTypeDef htim_enc;
extern ADC_HandleTypeDef hadc1;
extern bool g_hse_ok;                        /* false: 8 MHz crystal did not start, running on HSI */
extern volatile uint32_t g_irq_count[IRQ_N]; /* edge counters of the sensor interrupt lines */

void board_init(void);
void delay_us(uint32_t us);
void led_set(uint8_t mask);
void led_write(uint8_t mask, bool on);

uint16_t adc_read_avg(uint32_t channel);
float adc_vdda(void);       /* V, from the factory-calibrated internal reference */
float adc_vsys(float vdda); /* V, battery voltage after the power switch */
float adc_mcu_temp(float vdda);

void servo_set_us(int idx, uint16_t us); /* idx 0..2, us = 0 stops the pulse train */
void motor_set_permille(uint16_t pm);    /* 0..1000 */
uint16_t servo_get_us(int idx);          /* what the timer is really outputting */
uint16_t motor_get_permille(void);
int16_t enc_count(void);

#endif /* BOARD_H */
