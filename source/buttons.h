/*
 * buttons.h - Lectura de SW2/SW3 con debounce de software y deteccion de
 *             long-press en SW3 (FRDM-MCXA156).
 *
 * Esquema fisico (FRDM-MCXA156):
 *   SW2 = P1_7  -> GPIO1 pin 7  - boton "+" o "incrementar"
 *   SW3 = P0_6  -> GPIO0 pin 6  - boton multifuncion:
 *                                   click corto = "-" o "decrementar"
 *                                   long-press  = ciclar articulacion
 *
 * IMPORTANTE: en MCXA156 los dos botones estan en BLOQUES DISTINTOS de GPIO
 * (SW2 en GPIO1, SW3 en GPIO0). Esto es diferente del MCXN947, donde ambos
 * estaban en GPIO0. Por eso el modulo configura clocks y resets de PORT0,
 * PORT1, GPIO0 y GPIO1.
 *
 * Ambos botones son ACTIVE-LOW (cerrados a GND), con pull-up interno
 * habilitado. Por eso `presionado = pin lee 0`.
 *
 * Diseno: muestreo periodico cada 5 ms desde la capa de aplicacion (NO
 * usamos interrupcion). Esto es mas simple que el ejemplo del SDK
 * `gpio_input_interrupt` y suficiente para botones humanos (~200 ms de
 * presion tipica, 40 muestras por presion).
 *
 * Debounce: una transicion solo se acepta cuando se ve el mismo nivel en
 * 4 muestras consecutivas (20 ms estables) - filtra el "chatter" mecanico.
 *
 * Long-press SW3: si el boton se mantiene presionado >= 600 ms se emite
 * BUTTON_EVENT_SW3_LONG una sola vez. Si se libera antes, se emite
 * BUTTON_EVENT_SW3_SHORT al soltar. SW2 solo emite eventos en el release.
 */

#ifndef BUTTONS_H_
#define BUTTONS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tipos de evento que el modulo puede devolver al caller. */
typedef enum
{
    BUTTON_EVENT_NONE = 0,    /* nada nuevo este scan */
    BUTTON_EVENT_SW2_SHORT,   /* click corto en SW2 -> "+5 grados" */
    BUTTON_EVENT_SW3_SHORT,   /* click corto en SW3 -> "-5 grados" */
    BUTTON_EVENT_SW3_LONG,    /* long-press SW3 -> ciclar articulacion */
} button_event_t;

/*!
 * @brief Configura PORT0+PORT1 (clocks + pin mux + pull-up) y GPIO0+GPIO1
 *        (input) para SW3 y SW2 respectivamente. Llamar UNA vez en init.
 */
void buttons_init(void);

/*!
 * @brief Muestrea ambos botones, corre su state machine de debounce/long-press,
 *        y devuelve el evento que se haya generado en este scan (o NONE).
 *
 *        Debe llamarse periodicamente, idealmente cada 5 ms (precision del
 *        debounce y del long-press se basan en esta cadencia).
 *
 * @param u32NowMs Timestamp actual en ms (devuelto por `tick_get_ms()`).
 * @return button_event_t Evento generado o BUTTON_EVENT_NONE.
 */
button_event_t buttons_scan(uint32_t u32NowMs);

#ifdef __cplusplus
}
#endif

#endif /* BUTTONS_H_ */
