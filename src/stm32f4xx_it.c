/* Exception and interrupt handlers (USART3 lives in console.c). */
#include "app.h"
#include "usbd_desc.h"

static void fault_blink(void)
{
    /* Fast red blink = CPU fault.  Raw register access only. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    GPIOB->MODER = (GPIOB->MODER & ~(3U << (10 * 2))) | (1U << (10 * 2));
    for (;;) {
        GPIOB->ODR ^= LED_R_PIN;
        for (volatile uint32_t i = 0; i < 1500000U; i++) {
        }
    }
}

void NMI_Handler(void)        { fault_blink(); }
void HardFault_Handler(void)  { fault_blink(); }
void MemManage_Handler(void)  { fault_blink(); }
void BusFault_Handler(void)   { fault_blink(); }
void UsageFault_Handler(void) { fault_blink(); }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(TOF_INT_PIN);
}

void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(RF_IRQ_PIN);
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GYRO_INT_PIN);
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(ACC_INT_PIN);
    HAL_GPIO_EXTI_IRQHandler(BARO_INT_PIN);
}

void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&g_hpcd);
}
