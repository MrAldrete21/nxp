/*
 * tick.c - Implementacion del reloj de sistema usando SysTick del Cortex-M33.
 *
 * SysTick es un timer de 24 bits dedicado en el SCS del nucleo ARM. Lo
 * configuramos para disparar una interrupcion cada 1 ms incrementando un
 * contador volatil de 32 bits.
 *
 * Registros tocados (SCS, todos via CMSIS):
 *   - SysTick->LOAD : valor de recarga = SystemCoreClock / 1000 - 1
 *   - SysTick->VAL  : se limpia para arrancar desde 0
 *   - SysTick->CTRL : ENABLE | TICKINT | CLKSOURCE (reloj del nucleo)
 *
 * En FRDM-MCXA156, BOARD_InitBootClocks() llama a BOARD_BootClockFRO96M(),
 * lo que deja SystemCoreClock = 96 MHz. Por tanto LOAD = 96000 - 1 = 95999,
 * que cabe holgadamente en los 24 bits del SysTick (max 16 777 215).
 */

#include "tick.h"

#include "fsl_common.h"   /* SDK_ISR_EXIT_BARRIER */

/* Forward declaration del weak handler que provee el startup del SDK; al
   definirlo aqui lo sustituimos. */
void SysTick_Handler(void);

/* Contador monotonico de milisegundos. volatile porque se actualiza en ISR. */
static volatile uint32_t g_u32TickMs = 0U;

void tick_init(void)
{
    /* Actualizar SystemCoreClock al valor configurado por BOARD_InitBootClocks. */
    SystemCoreClockUpdate();

    /* Configurar SysTick para 1 kHz (un tick por milisegundo).
       SysTick_Config() de CMSIS configura LOAD, VAL y CTRL en una sola
       llamada. Devuelve 0 si tuvo exito. */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        /* No deberia pasar: SystemCoreClock/1000 = 96000 para FRO96M, cabe
           en 24 bits. En caso patologico, atrapar aqui. */
        for (;;)
        {
            __NOP();
        }
    }

    /* SysTick_Config ya habilita la interrupcion via NVIC. */
}

uint32_t tick_get_ms(void)
{
    /* Lectura atomica en Cortex-M (acceso de 32 bits alineado es atomico). */
    return g_u32TickMs;
}

/*!
 * @brief ISR de SysTick. Solo incrementa el contador y sale.
 */
void SysTick_Handler(void)
{
    g_u32TickMs++;
    SDK_ISR_EXIT_BARRIER;
}
