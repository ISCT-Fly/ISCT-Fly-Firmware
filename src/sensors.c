/* Sensor tests: I2C bus, BMP388 barometer, VL53L1X time-of-flight, BMI088 IMU. */
#include "app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ================================================================ I2C bus */

void test_i2c_bus(void)
{
    /* The input data register is readable in alternate-function mode too. */
    bool scl = HAL_GPIO_ReadPin(I2C_PORT, I2C_SCL_PIN) == GPIO_PIN_SET;
    bool sda = HAL_GPIO_ReadPin(I2C_PORT, I2C_SDA_PIN) == GPIO_PIN_SET;
    if (!scl || !sda) {
        report(R_FAIL, "I2C idle", "SCL=%d SDA=%d, both must be high (pull-ups R18/R19, short to GND?)", scl, sda);
        return;
    }
    report(R_PASS, "I2C idle", "SCL=1 SDA=1");

    char list[96] = "";
    size_t o = 0;
    for (uint16_t a = 0x08; a < 0x78; a++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1), 2, 3) == HAL_OK && o + 6 < sizeof(list)) {
            o += (size_t)snprintf(list + o, sizeof(list) - o, "0x%02X ", a);
        }
    }
    report(R_INFO, "I2C scan", "%s", o ? list : "no device answered");
}

/* ================================================================ BMP388 */

#define BMP_REG_CHIP_ID  0x00
#define BMP_REG_DATA     0x04
#define BMP_REG_INT_CTRL 0x19
#define BMP_REG_PWR_CTRL 0x1B
#define BMP_REG_OSR      0x1C
#define BMP_REG_ODR      0x1D
#define BMP_REG_CONFIG   0x1F
#define BMP_REG_CALIB    0x31
#define BMP_REG_CMD      0x7E

static struct {
    bool valid;
    double t1, t2, t3;
    double p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11;
} bmp_cal;

static bool bmp_rd(uint8_t reg, uint8_t *d, uint16_t n)
{
    return HAL_I2C_Mem_Read(&hi2c1, BMP388_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, d, n, 50) == HAL_OK;
}

static bool bmp_wr(uint8_t reg, uint8_t v)
{
    return HAL_I2C_Mem_Write(&hi2c1, BMP388_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, &v, 1, 50) == HAL_OK;
}

/* Reset, load the trimming coefficients and start 50 Hz continuous conversion with a
 * data-ready pulse on INT (push-pull, active high). */
static bool bmp_setup(void)
{
    uint8_t c[21];

    bmp_cal.valid = false;
    if (!bmp_wr(BMP_REG_CMD, 0xB6)) {
        return false;
    }
    HAL_Delay(10);
    if (!bmp_rd(BMP_REG_CALIB, c, sizeof(c))) {
        return false;
    }
    /* Floating-point conversion of the coefficients, BMP388 datasheet section 9.1 */
    bmp_cal.t1 = (double)(uint16_t)(c[0] | (c[1] << 8)) * 256.0;
    bmp_cal.t2 = (double)(uint16_t)(c[2] | (c[3] << 8)) / 1073741824.0;
    bmp_cal.t3 = (double)(int8_t)c[4] / 281474976710656.0;
    bmp_cal.p1 = ((double)(int16_t)(c[5] | (c[6] << 8)) - 16384.0) / 1048576.0;
    bmp_cal.p2 = ((double)(int16_t)(c[7] | (c[8] << 8)) - 16384.0) / 536870912.0;
    bmp_cal.p3 = (double)(int8_t)c[9] / 4294967296.0;
    bmp_cal.p4 = (double)(int8_t)c[10] / 137438953472.0;
    bmp_cal.p5 = (double)(uint16_t)(c[11] | (c[12] << 8)) * 8.0;
    bmp_cal.p6 = (double)(uint16_t)(c[13] | (c[14] << 8)) / 64.0;
    bmp_cal.p7 = (double)(int8_t)c[15] / 256.0;
    bmp_cal.p8 = (double)(int8_t)c[16] / 32768.0;
    bmp_cal.p9 = (double)(int16_t)(c[17] | (c[18] << 8)) / 281474976710656.0;
    bmp_cal.p10 = (double)(int8_t)c[19] / 281474976710656.0;
    bmp_cal.p11 = (double)(int8_t)c[20] / 36893488147419103232.0;

    bool ok = bmp_wr(BMP_REG_OSR, 0x02)        /* pressure x4, temperature x1 */
              && bmp_wr(BMP_REG_ODR, 0x02)     /* 50 Hz */
              && bmp_wr(BMP_REG_CONFIG, 0x00)  /* IIR off */
              && bmp_wr(BMP_REG_INT_CTRL, 0x42) /* drdy on INT, active high, push-pull */
              && bmp_wr(BMP_REG_PWR_CTRL, 0x33); /* pressure + temperature, normal mode */
    bmp_cal.valid = ok;
    return ok;
}

bool baro_read(float *press_pa, float *temp_c)
{
    uint8_t d[6];

    if (!bmp_cal.valid) {
        if (!bmp_setup()) {
            return false;
        }
        HAL_Delay(60);
    }
    if (!bmp_rd(BMP_REG_DATA, d, sizeof(d))) {
        bmp_cal.valid = false;
        return false;
    }
    double up = (double)((uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16));
    double ut = (double)((uint32_t)d[3] | ((uint32_t)d[4] << 8) | ((uint32_t)d[5] << 16));

    double pd1 = ut - bmp_cal.t1;
    double t = pd1 * bmp_cal.t2 + pd1 * pd1 * bmp_cal.t3;

    double o1 = bmp_cal.p5 + bmp_cal.p6 * t + bmp_cal.p7 * t * t + bmp_cal.p8 * t * t * t;
    double o2 = up * (bmp_cal.p1 + bmp_cal.p2 * t + bmp_cal.p3 * t * t + bmp_cal.p4 * t * t * t);
    double o3 = up * up * (bmp_cal.p9 + bmp_cal.p10 * t) + up * up * up * bmp_cal.p11;

    *temp_c = (float)t;
    *press_pa = (float)(o1 + o2 + o3);
    return true;
}

void test_baro(void)
{
    uint8_t id = 0;
    float p = 0, t = 0;

    if (!bmp_rd(BMP_REG_CHIP_ID, &id, 1)) {
        report(R_FAIL, "BMP388", "no answer at I2C 0x%02X", BMP388_ADDR);
        return;
    }
    if (id != 0x50 && id != 0x60) {
        report(R_FAIL, "BMP388", "chip id 0x%02X, expected 0x50", id);
        return;
    }
    if (!bmp_setup()) {
        report(R_FAIL, "BMP388", "chip id 0x%02X ok, but configuration writes failed", id);
        return;
    }
    HAL_Delay(100);
    uint32_t c0 = g_irq_count[IRQ_BARO];
    HAL_Delay(500);
    uint32_t edges = g_irq_count[IRQ_BARO] - c0;

    if (!baro_read(&p, &t)) {
        report(R_FAIL, "BMP388", "data read failed");
        return;
    }
    bool sane = p > 30000.0f && p < 110000.0f && t > -20.0f && t < 70.0f;
    report(sane ? R_PASS : R_FAIL, "BMP388", "id=0x%02X%s  P=%.1f hPa  T=%.1f C", id,
           id == 0x60 ? " (BMP390)" : "", (double)(p / 100.0f), (double)t);
    /* 50 Hz data-ready for 0.5 s -> about 25 pulses on PC15 */
    report(edges >= 15 && edges <= 35 ? R_PASS : R_FAIL, "BARO_INT", "%lu pulses in 0.5 s (expect ~25) on PC15",
           (unsigned long)edges);
}

/* ================================================================ VL53L1X */

static bool tof_rd(uint16_t reg, uint8_t *d, uint16_t n)
{
    return HAL_I2C_Mem_Read(&hi2c1, VL53L1X_ADDR << 1, reg, I2C_MEMADD_SIZE_16BIT, d, n, 50) == HAL_OK;
}

static bool tof_present(void)
{
    return HAL_I2C_IsDeviceReady(&hi2c1, VL53L1X_ADDR << 1, 2, 5) == HAL_OK;
}

void test_tof(void)
{
    uint8_t id[2] = {0, 0};
    uint8_t boot = 0;

    HAL_GPIO_WritePin(TOF_XSHUT_PORT, TOF_XSHUT_PIN, GPIO_PIN_SET);
    HAL_Delay(5);
    if (!tof_present() || !tof_rd(0x010F, id, 2)) {
        report(R_FAIL, "VL53L1X", "no answer at I2C 0x%02X", VL53L1X_ADDR);
        return;
    }
    tof_rd(0x00E5, &boot, 1); /* FIRMWARE__SYSTEM_STATUS, bit0 = booted */
    bool id_ok = id[0] == 0xEA && id[1] == 0xCC;
    report(id_ok ? R_PASS : R_FAIL, "VL53L1X", "model id 0x%02X%02X (expect 0xEACC), boot=%u", id[0], id[1],
           boot & 1u);

    /* XSHUT line: the sensor must drop off the bus when PC5 is low and come back when released. */
    HAL_GPIO_WritePin(TOF_XSHUT_PORT, TOF_XSHUT_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);
    bool gone = !tof_present();
    HAL_GPIO_WritePin(TOF_XSHUT_PORT, TOF_XSHUT_PIN, GPIO_PIN_SET);
    HAL_Delay(5);
    bool back = tof_present();
    report(gone && back ? R_PASS : R_FAIL, "TOF_XSHUT", "low: %s, high: %s", gone ? "sensor off" : "STILL ANSWERS",
           back ? "sensor back" : "NO ANSWER");

    bool int_high = HAL_GPIO_ReadPin(TOF_INT_PORT, TOF_INT_PIN) == GPIO_PIN_SET;
    report(int_high ? R_PASS : R_WARN, "TOF_INT", "PB0 idle level %d (expect 1, 10k pull-up); ranging not exercised",
           int_high);
}

/* ================================================================ BMI088 */

#define ACC_REG_CHIP_ID   0x00
#define ACC_REG_DATA      0x12
#define ACC_REG_INT_STAT1 0x1D
#define ACC_REG_TEMP      0x22
#define ACC_REG_CONF      0x40
#define ACC_REG_RANGE     0x41
#define ACC_REG_INT1_IO   0x53
#define ACC_REG_INT_MAP   0x58
#define ACC_REG_PWR_CONF  0x7C
#define ACC_REG_PWR_CTRL  0x7D
#define ACC_REG_SOFTRESET 0x7E

#define GYR_REG_CHIP_ID   0x00
#define GYR_REG_DATA      0x02
#define GYR_REG_RANGE     0x0F
#define GYR_REG_BW        0x10
#define GYR_REG_SOFTRESET 0x14
#define GYR_REG_INT_CTRL  0x15
#define GYR_REG_INT_IO    0x16
#define GYR_REG_INT_MAP   0x18

static bool imu_ready;

static void imu_xfer(GPIO_TypeDef *port, uint16_t pin, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi_imu, (uint8_t *)tx, rx, n, 20);
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    delay_us(5);
}

/* The accelerometer sends one dummy byte before the data. */
static void acc_rd(uint8_t reg, uint8_t *d, uint16_t n)
{
    uint8_t tx[10] = {0}, rx[10];
    tx[0] = reg | 0x80;
    imu_xfer(ACC_CS_PORT, ACC_CS_PIN, tx, rx, (uint16_t)(n + 2));
    memcpy(d, rx + 2, n);
}

static void acc_wr(uint8_t reg, uint8_t v)
{
    uint8_t tx[2] = {reg, v}, rx[2];
    imu_xfer(ACC_CS_PORT, ACC_CS_PIN, tx, rx, 2);
    HAL_Delay(2);
}

static void gyr_rd(uint8_t reg, uint8_t *d, uint16_t n)
{
    uint8_t tx[10] = {0}, rx[10];
    tx[0] = reg | 0x80;
    imu_xfer(GYRO_CS_PORT, GYRO_CS_PIN, tx, rx, (uint16_t)(n + 1));
    memcpy(d, rx + 1, n);
}

static void gyr_wr(uint8_t reg, uint8_t v)
{
    uint8_t tx[2] = {reg, v}, rx[2];
    imu_xfer(GYRO_CS_PORT, GYRO_CS_PIN, tx, rx, 2);
    HAL_Delay(1);
}

/* Reset both halves and start them: accel +-6 g / 100 Hz with data-ready on INT1,
 * gyro +-2000 dps / 1 kHz with data-ready on INT3.  Returns bit0 = accel ok, bit1 = gyro ok. */
static unsigned imu_setup(uint8_t *acc_id, uint8_t *gyr_id)
{
    uint8_t v = 0;
    unsigned ok = 0;

    acc_wr(ACC_REG_SOFTRESET, 0xB6);
    gyr_wr(GYR_REG_SOFTRESET, 0xB6);
    HAL_Delay(50);
    acc_rd(ACC_REG_CHIP_ID, &v, 1); /* first access after reset only switches the accel to SPI */
    acc_rd(ACC_REG_CHIP_ID, acc_id, 1);
    gyr_rd(GYR_REG_CHIP_ID, gyr_id, 1);

    if (*acc_id == 0x1E) {
        acc_wr(ACC_REG_PWR_CONF, 0x00); /* leave suspend */
        HAL_Delay(5);
        acc_wr(ACC_REG_PWR_CTRL, 0x04); /* accelerometer on */
        HAL_Delay(50);
        acc_wr(ACC_REG_RANGE, 0x01);    /* +-6 g */
        acc_wr(ACC_REG_CONF, 0xA8);     /* normal filter, 100 Hz */
        acc_wr(ACC_REG_INT1_IO, 0x0A);  /* INT1 output, push-pull, active high */
        acc_wr(ACC_REG_INT_MAP, 0x04);  /* data ready -> INT1 */
        acc_rd(ACC_REG_PWR_CTRL, &v, 1);
        if (v == 0x04) {
            ok |= 1u; /* read-back proves MOSI as well as MISO */
        }
    }
    if (*gyr_id == 0x0F) {
        gyr_wr(GYR_REG_RANGE, 0x00);    /* +-2000 dps */
        gyr_wr(GYR_REG_BW, 0x02);       /* 1 kHz output rate */
        gyr_wr(GYR_REG_INT_IO, 0x01);   /* INT3 push-pull, active high */
        gyr_wr(GYR_REG_INT_MAP, 0x01);  /* data ready -> INT3 */
        gyr_wr(GYR_REG_INT_CTRL, 0x80); /* enable data-ready interrupt */
        gyr_rd(GYR_REG_BW, &v, 1);
        if ((v & 0x0F) == 0x02) {
            ok |= 2u;
        }
    }
    imu_ready = (ok == 3u);
    return ok;
}

static void imu_raw(float acc_g[3], float gyro_dps[3], float *temp_c)
{
    uint8_t d[6];

    acc_rd(ACC_REG_DATA, d, 6);
    for (int i = 0; i < 3; i++) {
        acc_g[i] = (float)(int16_t)(d[2 * i] | (d[2 * i + 1] << 8)) * 6.0f / 32768.0f;
    }
    gyr_rd(GYR_REG_DATA, d, 6);
    for (int i = 0; i < 3; i++) {
        gyro_dps[i] = (float)(int16_t)(d[2 * i] | (d[2 * i + 1] << 8)) * 2000.0f / 32768.0f;
    }
    acc_rd(ACC_REG_TEMP, d, 2);
    int t11 = (d[0] << 3) | (d[1] >> 5);
    if (t11 > 1023) {
        t11 -= 2048;
    }
    *temp_c = (float)t11 * 0.125f + 23.0f;
}

bool imu_read(float acc_g[3], float gyro_dps[3], float *temp_c)
{
    if (!imu_ready) {
        uint8_t a, g;
        if (imu_setup(&a, &g) != 3u) {
            return false;
        }
        HAL_Delay(100);
    }
    imu_raw(acc_g, gyro_dps, temp_c);
    return true;
}

void test_imu(void)
{
    uint8_t acc_id = 0, gyr_id = 0;
    unsigned ok = imu_setup(&acc_id, &gyr_id);

    if (!(ok & 1u)) {
        report(R_FAIL, "BMI088 accel", "chip id 0x%02X (expect 0x1E)%s", acc_id,
               acc_id == 0x1E ? ", register write did not stick (MOSI/PB5?)" : "");
    }
    if (!(ok & 2u)) {
        report(R_FAIL, "BMI088 gyro", "chip id 0x%02X (expect 0x0F)%s", gyr_id,
               gyr_id == 0x0F ? ", register write did not stick (MOSI/PB5?)" : "");
    }
    if (ok != 3u) {
        return;
    }
    HAL_Delay(100);

    /* Average while counting data-ready edges for 0.5 s.  Reading the accel data and its
     * interrupt status inside the window guarantees the INT1 line is re-armed. */
    float acc[3] = {0}, gyr[3] = {0}, temp = 0;
    float a[3], g[3];
    uint8_t st;
    int n = 0;
    uint32_t c_acc = g_irq_count[IRQ_ACC], c_gyr = g_irq_count[IRQ_GYRO];
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < 500u) {
        imu_raw(a, g, &temp);
        acc_rd(ACC_REG_INT_STAT1, &st, 1);
        for (int i = 0; i < 3; i++) {
            acc[i] += a[i];
            gyr[i] += g[i];
        }
        n++;
        HAL_Delay(2);
    }
    uint32_t e_acc = g_irq_count[IRQ_ACC] - c_acc;
    uint32_t e_gyr = g_irq_count[IRQ_GYRO] - c_gyr;
    for (int i = 0; i < 3; i++) {
        acc[i] /= (float)n;
        gyr[i] /= (float)n;
    }
    float norm = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
    float gmax = fmaxf(fabsf(gyr[0]), fmaxf(fabsf(gyr[1]), fabsf(gyr[2])));

    report(fabsf(norm - 1.0f) < 0.15f ? R_PASS : R_WARN, "BMI088 accel",
           "id=0x1E  a=[%+.3f %+.3f %+.3f] g  |a|=%.3f g%s  T=%.1f C", (double)acc[0], (double)acc[1],
           (double)acc[2], (double)norm, fabsf(norm - 1.0f) < 0.15f ? "" : " (expect 1.0 at rest)", (double)temp);
    report(gmax < 10.0f ? R_PASS : R_WARN, "BMI088 gyro", "id=0x0F  w=[%+.2f %+.2f %+.2f] dps%s", (double)gyr[0],
           (double)gyr[1], (double)gyr[2], gmax < 10.0f ? "" : " (board moving?)");
    report(e_acc >= 35 && e_acc <= 65 ? R_PASS : R_FAIL, "ACC_INT", "%lu pulses in 0.5 s (expect ~50) on PB12",
           (unsigned long)e_acc);
    report(e_gyr >= 350 && e_gyr <= 650 ? R_PASS : R_FAIL, "GYRO_INT", "%lu pulses in 0.5 s (expect ~500) on PB7",
           (unsigned long)e_gyr);
}
