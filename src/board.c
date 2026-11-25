/* Board bring-up: clock tree, GPIO and every peripheral the tests use. */
#include "app.h"
#include <string.h>

SPI_HandleTypeDef hspi_rf;
SPI_HandleTypeDef hspi_imu;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim_servo;
TIM_HandleTypeDef htim_motor;
TIM_HandleTypeDef htim_enc;
ADC_HandleTypeDef hadc1;
bool g_hse_ok;
volatile uint32_t g_irq_count[IRQ_N];

/* Factory calibration values (measured at VDDA = 3.3 V) */
#define VREFINT_CAL (*(const uint16_t *)0x1FFF7A2AU)
#define TS_CAL1     (*(const uint16_t *)0x1FFF7A2CU) /* 30 degC */
#define TS_CAL2     (*(const uint16_t *)0x1FFF7A2EU) /* 110 degC */

static void fatal(void)
{
    /* Nothing else can be trusted here: blink red with a raw loop. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = LED_R_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LED_PORT, &g);
    for (;;) {
        HAL_GPIO_TogglePin(LED_PORT, LED_R_PIN);
        for (volatile uint32_t i = 0; i < 400000; i++) {
        }
    }
}

/* SYSCLK 168 MHz, 48 MHz for USB/SDIO from PLLQ.  Falls back to the internal RC when the
 * 8 MHz crystal does not start, so the rest of the board can still be tested. */
static void clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    osc.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        g_hse_ok = true;
    } else {
        g_hse_ok = false;
        memset(&osc, 0, sizeof(osc));
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSE;
        osc.HSEState = RCC_HSE_OFF;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState = RCC_PLL_ON;
        osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
        osc.PLL.PLLM = 16;
        osc.PLL.PLLN = 336;
        osc.PLL.PLLP = RCC_PLLP_DIV2;
        osc.PLL.PLLQ = 7;
        osc.PLL.PLLR = 2;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
            fatal();
        }
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4; /* 42 MHz, timers 84 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV2; /* 84 MHz, timers 168 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        fatal();
    }
    __HAL_RCC_CLK48_CONFIG(RCC_CLK48CLKSOURCE_PLLQ);
    __HAL_RCC_SDIO_CONFIG(RCC_SDIOCLKSOURCE_CLK48);
}

void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

static void pin_cfg(GPIO_TypeDef *port, uint32_t pins, uint32_t mode, uint32_t pull, uint32_t af)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pins;
    g.Mode = mode;
    g.Pull = pull;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = af;
    HAL_GPIO_Init(port, &g);
}

static void spi_setup(SPI_HandleTypeDef *h, SPI_TypeDef *inst, uint32_t cpol, uint32_t cpha, uint32_t presc)
{
    h->Instance = inst;
    h->Init.Mode = SPI_MODE_MASTER;
    h->Init.Direction = SPI_DIRECTION_2LINES;
    h->Init.DataSize = SPI_DATASIZE_8BIT;
    h->Init.CLKPolarity = cpol;
    h->Init.CLKPhase = cpha;
    h->Init.NSS = SPI_NSS_SOFT;
    h->Init.BaudRatePrescaler = presc;
    h->Init.FirstBit = SPI_FIRSTBIT_MSB;
    h->Init.TIMode = SPI_TIMODE_DISABLE;
    h->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    h->Init.CRCPolynomial = 7;
    if (HAL_SPI_Init(h) != HAL_OK) {
        fatal();
    }
}

/* A slave that was interrupted mid-byte by a reset can hold SDA low; clock it free. */
static void i2c_bus_release(void)
{
    pin_cfg(I2C_PORT, I2C_SCL_PIN | I2C_SDA_PIN, GPIO_MODE_INPUT, GPIO_NOPULL, 0);
    HAL_Delay(1);
    if (HAL_GPIO_ReadPin(I2C_PORT, I2C_SDA_PIN) == GPIO_PIN_SET) {
        return;
    }
    HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    pin_cfg(I2C_PORT, I2C_SCL_PIN, GPIO_MODE_OUTPUT_OD, GPIO_NOPULL, 0);
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        delay_us(10);
        HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        delay_us(10);
    }
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* LEDs */
    HAL_GPIO_WritePin(LED_PORT, LED_G_PIN | LED_B_PIN | LED_R_PIN, GPIO_PIN_RESET);
    pin_cfg(LED_PORT, LED_G_PIN | LED_B_PIN | LED_R_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);

    /* Chip selects idle high, radio CE low, ToF enabled */
    HAL_GPIO_WritePin(RF_CSN_PORT, RF_CSN_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(RF_CE_PORT, RF_CE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ACC_CS_PORT, ACC_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GYRO_CS_PORT, GYRO_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TOF_XSHUT_PORT, TOF_XSHUT_PIN, GPIO_PIN_SET);
    pin_cfg(RF_CSN_PORT, RF_CSN_PIN | RF_CE_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    pin_cfg(ACC_CS_PORT, ACC_CS_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    pin_cfg(GYRO_CS_PORT, GYRO_CS_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0); /* PA15: releases JTDI */
    pin_cfg(TOF_XSHUT_PORT, TOF_XSHUT_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);

    /* Status inputs.  CHG_DET and VBUS_DET can sit above VDD/at mid level: no internal pulls. */
    pin_cfg(CHG_DET_PORT, CHG_DET_PIN, GPIO_MODE_INPUT, GPIO_NOPULL, 0);
    pin_cfg(VBUS_DET_PORT, VBUS_DET_PIN, GPIO_MODE_INPUT, GPIO_NOPULL, 0);
    pin_cfg(GPIOA, GPIO_PIN_10, GPIO_MODE_INPUT, GPIO_NOPULL, 0); /* only a 10k pull-up on board */

    /* Sensor interrupt lines -> edge counters */
    pin_cfg(TOF_INT_PORT, TOF_INT_PIN, GPIO_MODE_IT_FALLING, GPIO_NOPULL, 0);
    pin_cfg(RF_IRQ_PORT, RF_IRQ_PIN, GPIO_MODE_IT_FALLING, GPIO_PULLUP, 0);
    pin_cfg(GYRO_INT_PORT, GYRO_INT_PIN, GPIO_MODE_IT_RISING, GPIO_PULLDOWN, 0);
    pin_cfg(ACC_INT_PORT, ACC_INT_PIN, GPIO_MODE_IT_RISING, GPIO_PULLDOWN, 0);
    pin_cfg(BARO_INT_PORT, BARO_INT_PIN, GPIO_MODE_IT_RISING, GPIO_PULLDOWN, 0);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
    HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    /* SPI1 -> nRF24L01+ (MISO pulled down so a missing chip reads 0x00) */
    pin_cfg(GPIOA, GPIO_PIN_5 | GPIO_PIN_7, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_AF5_SPI1);
    pin_cfg(GPIOA, GPIO_PIN_6, GPIO_MODE_AF_PP, GPIO_PULLDOWN, GPIO_AF5_SPI1);
    /* SPI3 -> BMI088 (PB3/PB4 release JTDO/NJTRST; MISO pulled up: both SDO pins float when idle) */
    pin_cfg(GPIOB, GPIO_PIN_3 | GPIO_PIN_5, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_AF6_SPI3);
    pin_cfg(GPIOB, GPIO_PIN_4, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF6_SPI3);

    /* I2C1 */
    i2c_bus_release();
    pin_cfg(I2C_PORT, I2C_SCL_PIN | I2C_SDA_PIN, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_AF4_I2C1);

    /* Servos TIM2_CH1..3, motor TIM1_CH1, encoder TIM3_CH1/2 (no pull-ups on board) */
    pin_cfg(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_AF1_TIM2);
    pin_cfg(GPIOA, GPIO_PIN_8, GPIO_MODE_AF_PP, GPIO_PULLDOWN, GPIO_AF1_TIM1);
    pin_cfg(GPIOC, GPIO_PIN_6 | GPIO_PIN_7, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF2_TIM3);

    /* Battery sense */
    pin_cfg(GPIOC, GPIO_PIN_0, GPIO_MODE_ANALOG, GPIO_NOPULL, 0);
}

static void timers_init(void)
{
    TIM_OC_InitTypeDef oc = {0};
    uint32_t tclk1 = HAL_RCC_GetPCLK1Freq() * 2U; /* APB prescaler != 1 -> timer clock = 2 x PCLK */
    uint32_t tclk2 = HAL_RCC_GetPCLK2Freq() * 2U;

    /* Servos: 1 us resolution, 20 ms period.  Compare 0 = line stays low (no pulses). */
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim_servo.Instance = TIM2;
    htim_servo.Init.Prescaler = tclk1 / 1000000U - 1U;
    htim_servo.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_servo.Init.Period = 20000U - 1U;
    htim_servo.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_servo.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim_servo) != HAL_OK) {
        fatal();
    }
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim_servo, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim_servo, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim_servo, &oc, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim_servo, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim_servo, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim_servo, TIM_CHANNEL_3);

    /* Motor: 20 kHz, duty 0 at start (gate also has a 10k pull-down on the board) */
    __HAL_RCC_TIM1_CLK_ENABLE();
    htim_motor.Instance = TIM1;
    htim_motor.Init.Prescaler = 0;
    htim_motor.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_motor.Init.Period = tclk2 / MOTOR_PWM_HZ - 1U;
    htim_motor.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_motor.Init.RepetitionCounter = 0;
    htim_motor.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim_motor) != HAL_OK) {
        fatal();
    }
    HAL_TIM_PWM_ConfigChannel(&htim_motor, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim_motor, TIM_CHANNEL_1);

    /* Quadrature encoder, x4 counting */
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM_Encoder_InitTypeDef enc = {0};
    htim_enc.Instance = TIM3;
    htim_enc.Init.Prescaler = 0;
    htim_enc.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_enc.Init.Period = 0xFFFF;
    htim_enc.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_enc.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    enc.EncoderMode = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity = TIM_ICPOLARITY_RISING;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter = 6;
    enc.IC2Polarity = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter = 6;
    if (HAL_TIM_Encoder_Init(&htim_enc, &enc) != HAL_OK) {
        fatal();
    }
    HAL_TIM_Encoder_Start(&htim_enc, TIM_CHANNEL_ALL);
}

static void adc_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4; /* 21 MHz */
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        fatal();
    }
}

void board_init(void)
{
    HAL_Init();
    clock_init();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    gpio_init();

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_SPI3_CLK_ENABLE();
    /* nRF24L01+: mode 0, 84 MHz / 16 = 5.25 MHz.  BMI088: mode 3, 42 MHz / 8 = 5.25 MHz. */
    spi_setup(&hspi_rf, SPI1, SPI_POLARITY_LOW, SPI_PHASE_1EDGE, SPI_BAUDRATEPRESCALER_16);
    spi_setup(&hspi_imu, SPI3, SPI_POLARITY_HIGH, SPI_PHASE_2EDGE, SPI_BAUDRATEPRESCALER_8);

    __HAL_RCC_I2C1_CLK_ENABLE();
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        fatal();
    }

    timers_init();
    adc_init();
}

/* ---------------------------------------------------------------- LEDs */

void led_write(uint8_t mask, bool on)
{
    uint16_t pins = 0;
    if (mask & LED_G) pins |= LED_G_PIN;
    if (mask & LED_B) pins |= LED_B_PIN;
    if (mask & LED_R) pins |= LED_R_PIN;
    if (pins) {
        HAL_GPIO_WritePin(LED_PORT, pins, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void led_set(uint8_t mask)
{
    led_write(mask, true);
    led_write((uint8_t)~mask & (LED_G | LED_B | LED_R), false);
}

/* ---------------------------------------------------------------- ADC */

uint16_t adc_read_avg(uint32_t channel)
{
    ADC_ChannelConfTypeDef c = {0};
    uint32_t sum = 0;
    int n = 0;

    c.Channel = channel;
    c.Rank = 1;
    c.SamplingTime = ADC_SAMPLETIME_480CYCLES; /* 23 us: fine for the 50k divider and VREFINT */
    if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) {
        return 0;
    }
    for (int i = 0; i < 17; i++) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
            uint32_t v = HAL_ADC_GetValue(&hadc1);
            if (i > 0) { /* first sample after a channel change is thrown away */
                sum += v;
                n++;
            }
        }
    }
    HAL_ADC_Stop(&hadc1);
    return n ? (uint16_t)(sum / (uint32_t)n) : 0;
}

float adc_vdda(void)
{
    uint16_t raw = adc_read_avg(ADC_CHANNEL_VREFINT);
    if (raw == 0) {
        return 0.0f;
    }
    return 3.3f * (float)VREFINT_CAL / (float)raw;
}

float adc_vsys(float vdda)
{
    return (float)adc_read_avg(BAT_SENSE_ADC_CH) * vdda / 4095.0f * 2.0f;
}

float adc_mcu_temp(float vdda)
{
    float raw = (float)adc_read_avg(ADC_CHANNEL_TEMPSENSOR) * vdda / 3.3f;
    return (raw - (float)TS_CAL1) * 80.0f / (float)(TS_CAL2 - TS_CAL1) + 30.0f;
}

/* ---------------------------------------------------------------- actuators */

void servo_set_us(int idx, uint16_t us)
{
    static const uint32_t ch[SERVO_COUNT] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3};
    if (idx < 0 || idx >= SERVO_COUNT) {
        return;
    }
    if (us > 2500) {
        us = 2500;
    }
    __HAL_TIM_SET_COMPARE(&htim_servo, ch[idx], us);
}

void motor_set_permille(uint16_t pm)
{
    if (pm > 1000) {
        pm = 1000;
    }
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim_motor);
    __HAL_TIM_SET_COMPARE(&htim_motor, TIM_CHANNEL_1, (arr + 1U) * pm / 1000U);
}

uint16_t servo_get_us(int idx)
{
    static const uint32_t ch[SERVO_COUNT] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3};
    if (idx < 0 || idx >= SERVO_COUNT) {
        return 0;
    }
    return (uint16_t)__HAL_TIM_GET_COMPARE(&htim_servo, ch[idx]);
}

uint16_t motor_get_permille(void)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim_motor);
    uint32_t ccr = __HAL_TIM_GET_COMPARE(&htim_motor, TIM_CHANNEL_1);
    return (uint16_t)((ccr * 1000U + arr / 2U) / (arr + 1U));
}

int16_t enc_count(void)
{
    return (int16_t)__HAL_TIM_GET_COUNTER(&htim_enc);
}

/* ---------------------------------------------------------------- interrupt counters */

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    switch (pin) {
    case TOF_INT_PIN:  g_irq_count[IRQ_TOF]++;  break;
    case RF_IRQ_PIN:   g_irq_count[IRQ_RF]++;   break;
    case GYRO_INT_PIN: g_irq_count[IRQ_GYRO]++; break;
    case ACC_INT_PIN:  g_irq_count[IRQ_ACC]++;  break;
    case BARO_INT_PIN: g_irq_count[IRQ_BARO]++; break;
    default: break;
    }
}
