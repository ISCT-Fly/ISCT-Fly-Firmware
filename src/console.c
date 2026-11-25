/* Text console on USART3 (PC10 TX / PC11 RX, 115200 8N1) and on the USB CDC port.
 * Output goes to both, input is accepted from either. */
#include "app.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RXQ_SIZE 256u
static volatile uint8_t rxq[RXQ_SIZE];
static volatile uint16_t rx_head, rx_tail;

/* Copy of the last self-test report, replayed when a terminal connects after boot. */
#define CAP_SIZE 8192u
static char cap[CAP_SIZE];
static size_t cap_len;
static bool cap_on;
static bool echo_on = true;

void con_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    g.Pin = GPIO_PIN_10;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_11;
    g.Pull = GPIO_PULLUP; /* RX pad is normally left open */
    HAL_GPIO_Init(GPIOC, &g);

    USART3->CR1 = 0;
    USART3->CR2 = 0;
    USART3->CR3 = 0;
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + 115200U / 2U) / 115200U;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void USART3_IRQHandler(void)
{
    uint32_t sr = USART3->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        uint8_t d = (uint8_t)USART3->DR; /* reading DR also clears the error flags */
        if ((sr & USART_SR_RXNE) && !(sr & (USART_SR_FE | USART_SR_NE))) {
            con_rx_push(d);
        }
    }
}

void con_rx_push(uint8_t b)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % RXQ_SIZE);
    if (next != rx_tail) {
        rxq[rx_head] = b;
        rx_head = next;
    }
}

int con_getc(void)
{
    if (rx_head == rx_tail) {
        return -1;
    }
    uint8_t b = rxq[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % RXQ_SIZE);
    return b;
}

bool con_key_pressed(void)
{
    bool any = false;
    while (con_getc() >= 0) {
        any = true;
    }
    return any;
}

static void uart_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t guard = 400000U; /* ~10 ms worst case, a character takes 87 us */
        while (!(USART3->SR & USART_SR_TXE) && --guard) {
        }
        USART3->DR = (uint8_t)s[i];
    }
}

static void raw_write(const char *s, size_t n)
{
    if (n == 0) {
        return;
    }
    cdc_write((const uint8_t *)s, n);
    uart_write(s, n);
    if (cap_on) {
        size_t room = CAP_SIZE - cap_len;
        if (n > room) {
            n = room;
        }
        memcpy(cap + cap_len, s, n);
        cap_len += n;
    }
}

void con_printf(const char *fmt, ...)
{
    char buf[256];
    char out[2 * sizeof(buf)];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            out[o++] = '\r';
        }
        out[o++] = buf[i];
    }
    raw_write(out, o);
}

void con_capture(bool on)
{
    if (on) {
        cap_len = 0;
    }
    cap_on = on;
}

void con_set_echo(bool on)
{
    echo_on = on;
}

bool con_echo(void)
{
    return echo_on;
}

void con_prompt(void)
{
    if (echo_on) {
        raw_write("> ", 2);
    }
}

void con_replay(void)
{
    bool was = cap_on;
    cap_on = false;
    raw_write(cap, cap_len);
    cap_on = was;
}

const char *con_readline(void)
{
    static char line[96];
    static size_t len;
    static bool last_cr;
    int c;

    while ((c = con_getc()) >= 0) {
        if (c == '\n' && last_cr) {
            last_cr = false;
            continue;
        }
        last_cr = (c == '\r');
        if (c == '\r' || c == '\n') {
            if (echo_on) {
                raw_write("\r\n", 2);
            }
            line[len] = '\0';
            len = 0;
            return line;
        }
        if (c == 0x08 || c == 0x7F) {
            if (len > 0) {
                len--;
                if (echo_on) {
                    raw_write("\b \b", 3);
                }
            }
        } else if (c >= 0x20 && c < 0x7F && len < sizeof(line) - 1) {
            line[len++] = (char)c;
            if (echo_on) {
                char ch = (char)c;
                raw_write(&ch, 1);
            }
        }
    }
    return NULL;
}
