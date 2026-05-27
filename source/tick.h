/*
 * tick.h - Reloj de sistema basado en SysTick (1 kHz, base 1 ms).
 *
 * Provee un contador monotonico en milisegundos accesible por sondeo desde
 * el lazo principal. Util para todos los timing cooperativos (parpadeo de
 * LED, periodo de scan, watchdog SW por timeout, etc.).
 *
 * Por que SysTick y no LPTMR/PIT: para el bring-up es el camino mas simple
 * (esta en el nucleo Cortex-M33, no necesita configurar reloj de periferia).
 * Si en pasos posteriores hace falta el LPTMR para low-power, se puede
 * sustituir manteniendo la misma API publica.
 *
 * Este modulo es IDENTICO al del TX (FRDM-MCXN947). SysTick es parte del
 * SCS del nucleo ARM y la API CMSIS es la misma en ambas tarjetas.
 */

#ifndef TICK_H_
#define TICK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Configura SysTick para disparar cada 1 ms. Debe llamarse antes
 *        de cualquier consulta a `tick_get_ms()`.
 */
void tick_init(void);

/*!
 * @brief Devuelve los milisegundos transcurridos desde `tick_init()`.
 *        Wrap-around natural a ~49 dias (uint32_t).
 *
 * @return Contador en milisegundos.
 */
uint32_t tick_get_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* TICK_H_ */
