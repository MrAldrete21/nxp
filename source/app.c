/*
 * app.c - Lazo principal del nodo RX (FRDM-MCXA156).
 *
 * Proyecto 2 NXP-UAG. Paso A (bring-up de CAN). Esta version integra:
 *
 *   - SysTick @ 1 kHz como base de tiempo (modulo `tick`).
 *   - LPUART0 @ 115200 8N1 para debug por USB-VCP (modulo `uart_dbg`).
 *   - LEDs RGB onboard como indicador de vida y actividad (modulo `gpio_io`).
 *   - FlexCAN0 @ 500 kbps, frame ID 0x200, DLC 8, recepcion ISR-driven
 *     (modulo `can_rx`).
 *
 * El RX NO transmite por CAN. Solo recibe.
 *
 * Tareas del lazo (scheduler cooperativo, sin delays bloqueantes):
 *
 *   continuo    -> polleo can_rx_try_get; si hay frame, actualiza ultimo
 *                  estado y prende LED verde "RX activo".
 *   cada 500 ms -> togglea LED azul (vida). Si han pasado mas de 200 ms
 *                  desde el ultimo frame OK, apaga LED verde.
 *   cada 1000 ms-> log por UART con estado decodificado + contadores
 *                  ok/err + tiempo desde ultimo frame.
 *
 * Cada tarea se dispara comparando "ahora" contra "ultima vez que corrio";
 * cuando supera su periodo, corre. Ninguna tarea hace `delay()` ni espera
 * activamente, por lo que el lazo nunca pierde mas de unos pocos us de
 * latencia entre eventos.
 *
 * Salida esperada por UART (COM virtual del MCU-Link de la A156) a 115200 8N1
 * cuando todo funciona:
 *
 *   =======================================
 *    RX  FRDM-MCXA156  -  Protesis Robotica
 *   =======================================
 *   [INIT] modulos OK
 *          FlexCAN0 @ 500 kbps, ID 0x200, DLC 8, RX MB0
 *   ---------------------------------------
 *   RX | S= 90  E= 75  W= 45  age=  47 ms  rx[ok=    23 err=   0]
 *   RX | S= 90  E= 80  W= 45  age=  31 ms  rx[ok=    43 err=   0]
 *   ...
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "app.h"
#include "tick.h"
#include "uart_dbg.h"
#include "gpio_io.h"
#include "can_rx.h"

/* ---------------------------------------------------------------------------
 * Periodos del scheduler (en milisegundos)
 * ------------------------------------------------------------------------ */
#define APP_PERIOD_LED_MS           (500U)
#define APP_PERIOD_LOG_MS           (1000U)

/* Umbral de "frame fresco" para mantener LED verde encendido. Si pasa
 * mas tiempo que esto sin que llegue un frame nuevo, apagamos el verde.
 * El umbral coincide con el watchdog del proyecto (200 ms). */
#define APP_FRESH_THRESHOLD_MS      (200U)

/* ---------------------------------------------------------------------------
 * Punto de entrada de la aplicacion
 * ------------------------------------------------------------------------ */
void appMain(void)
{
    /* Init en orden: primero los servicios base (tick, UART, LEDs),
     * despues el periferico de comunicacion (CAN). El CAN va al final
     * porque imprime un mensaje de "listo" via UART. */
    tick_init();
    uart_dbg_init();
    gpio_io_init();

    uart_dbg_send_str("\r\n\r\n");
    uart_dbg_send_str("=======================================\r\n");
    uart_dbg_send_str(" RX  FRDM-MCXA156  -  Protesis Robotica \r\n");
    uart_dbg_send_str("=======================================\r\n");

    can_rx_init();

    uart_dbg_send_str("[INIT] modulos OK\r\n");
    uart_dbg_send_str("       FlexCAN0 @ 500 kbps, ID 0x200, DLC 8, RX MB0\r\n");
    uart_dbg_send_str("---------------------------------------\r\n");

    /* Ultimo estado decodificado del frame CAN. Mientras no llegue ningun
     * frame, queda en ceros. */
    can_rx_joints_t  sLastJoints  = { 0U, 0U, 0U };
    uint32_t         u32LastFrameMs = 0U;  /* timestamp del ultimo frame OK */
    bool             bHaveFrame   = false;  /* si ya recibimos al menos uno  */

    /* Tiempos de la ultima ejecucion de cada tarea. */
    uint32_t u32LastLedMs = 0U;
    uint32_t u32LastLogMs = 0U;

    for (;;)
    {
        const uint32_t u32NowMs = tick_get_ms();

        /* ----- Tarea 1: poll continuo del CAN.
         *
         * Llamamos en cada iteracion del lazo; el costo cuando no hay
         * frame nuevo es 1 lectura de bandera con interrupciones
         * deshabilitadas (sub-microsegundo). Cuando hay frame, se copia
         * el struct (12 bytes) y se enciende el LED verde de actividad. */
        can_rx_joints_t sFresh;
        uint32_t        u32FreshMs;
        if (can_rx_try_get(&sFresh, &u32FreshMs))
        {
            sLastJoints    = sFresh;
            u32LastFrameMs = u32FreshMs;
            bHaveFrame     = true;
            gpio_io_set_green(true);
        }

        /* ----- Tarea 2: latido del LED azul + decay del LED verde
         *               cada 500 ms.
         *
         * Si han pasado mas de APP_FRESH_THRESHOLD_MS sin nuevo frame,
         * apagamos el verde (la app esta "ciega"). El parpadeo del azul
         * sigue independiente, demuestra que el firmware esta vivo. */
        if ((u32NowMs - u32LastLedMs) >= APP_PERIOD_LED_MS)
        {
            u32LastLedMs = u32NowMs;
            gpio_io_toggle_blue();

            if (bHaveFrame &&
                ((u32NowMs - u32LastFrameMs) > APP_FRESH_THRESHOLD_MS))
            {
                gpio_io_set_green(false);
            }
        }

        /* ----- Tarea 3: log periodico cada 1 segundo.
         *
         * Imprime los angulos decodificados + edad del ultimo frame
         * (en ms) + contadores OK/ERR del driver CAN. Si todavia no
         * llego ningun frame, age se reporta como "----". */
        if ((u32NowMs - u32LastLogMs) >= APP_PERIOD_LOG_MS)
        {
            u32LastLogMs = u32NowMs;
            char acBuf[112];
            int  iLen;

            if (bHaveFrame)
            {
                uint32_t u32AgeMs = u32NowMs - u32LastFrameMs;
                iLen = snprintf(acBuf, sizeof(acBuf),
                                "RX | S=%3u  E=%3u  W=%3u  age=%4u ms  "
                                "rx[ok=%5u err=%3u]\r\n",
                                (unsigned)sLastJoints.u8Shoulder,
                                (unsigned)sLastJoints.u16Elbow,
                                (unsigned)sLastJoints.u8Wrist,
                                (unsigned)u32AgeMs,
                                (unsigned)can_rx_get_ok_count(),
                                (unsigned)can_rx_get_err_count());
            }
            else
            {
                iLen = snprintf(acBuf, sizeof(acBuf),
                                "RX | (sin frame todavia)  "
                                "rx[ok=%5u err=%3u]\r\n",
                                (unsigned)can_rx_get_ok_count(),
                                (unsigned)can_rx_get_err_count());
            }

            if (iLen > 0)
            {
                uart_dbg_send_str(acBuf);
            }
        }
    }
}
