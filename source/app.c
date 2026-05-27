/*
 * app.c - Lazo principal del nodo TX (FRDM-MCXA156).
 *
 * Proyecto 2 NXP-UAG. ESCENARIO ACTUAL: la MCXA156 es ahora el TRANSMISOR
 * de la red CAN (los botones SW2/SW3 controlan los angulos) y la MCXN947
 * es el RECEPTOR (decodifica y emite CSV al viewer 3D PyVista).
 *
 * Esta version integra todo el flujo del transmisor:
 *
 *   - SysTick @ 1 kHz como base de tiempo (modulo `tick`).
 *   - LPUART0 @ 115200 8N1 para debug por USB-VCP (modulo `uart_dbg`).
 *   - LEDs RGB onboard como indicador de vida y actividad (modulo `gpio_io`).
 *   - SW2/SW3 con debounce + long-press (modulo `buttons`).
 *   - Estado de las 3 articulaciones en RAM (modulo `joints`).
 *   - FlexCAN0 @ 500 kbps, frame ID 0x200, DLC 8 (modulo `can_tx`).
 *
 * Tareas del lazo (scheduler cooperativo, sin delays bloqueantes):
 *
 *   cada 5 ms   -> escanear botones; aplicar evento al estado de joints.
 *   cada 50 ms  -> empaquetar frame CAN, transmitir.
 *   cada 500 ms -> togglear LED azul (vida).
 *   cada 1000 ms-> log por UART con estado de joints + contadores.
 *
 * Cada tarea se dispara comparando "ahora" contra "ultima vez que corrio";
 * cuando supera su periodo, corre. Ninguna tarea hace `delay()` ni espera
 * activamente, por lo que el lazo nunca pierde mas de unos pocos us de
 * latencia entre eventos.
 *
 * Mapeo de botones (revision actual):
 *   SW2 click corto -> CICLAR articulacion (Hombro->Codo->Muneca->...)
 *   SW3 click corto -> CONTRAER  = +5 grados (mas flexion) en la seleccionada
 *   SW3 long-press  -> RETRAER   = -5 grados (mas extension) en la seleccionada
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "app.h"
#include "tick.h"
#include "uart_dbg.h"
#include "gpio_io.h"
#include "buttons.h"
#include "joints.h"
#include "can_tx.h"

/* ---------------------------------------------------------------------------
 * Periodos del scheduler (en milisegundos)
 * ------------------------------------------------------------------------ */
#define APP_PERIOD_BUTTONS_MS       (5U)
#define APP_PERIOD_CAN_TX_MS        (50U)
#define APP_PERIOD_LED_MS           (500U)
#define APP_PERIOD_LOG_MS           (1000U)

/* CAN ID estandar del proyecto (frame de los 3 angulos). */
#define APP_CAN_FRAME_ID            (0x200U)

/* ---------------------------------------------------------------------------
 * Helpers de logging
 * ------------------------------------------------------------------------ */
static const char *joint_name(joint_id_t eId)
{
    switch (eId)
    {
        case JOINT_SHOULDER: return "Hombro";
        case JOINT_ELBOW:    return "Codo";
        case JOINT_WRIST:    return "Muneca";
        default:             return "?";
    }
}

/* Maneja un evento de boton aplicandolo al estado de joints y dando feedback
 * por UART. Esta funcion es lo unico que "ata" botones con joints. */
static void app_handle_button_event(button_event_t eEvt)
{
    if (eEvt == BUTTON_EVENT_NONE)
    {
        return;
    }
    switch (eEvt)
    {
        case BUTTON_EVENT_SW2_SHORT:
            /* SW2 click corto: ciclar a la siguiente articulacion
             * (Hombro -> Codo -> Muneca -> Hombro -> ...). */
            joints_cycle_selection();
            {
                char acBuf[48];
                int iLen = snprintf(acBuf, sizeof(acBuf),
                                    "[BTN] SW2 -> sel=%s\r\n",
                                    joint_name(joints_get_selected()));
                if (iLen > 0)
                {
                    uart_dbg_send_str(acBuf);
                }
            }
            break;
        case BUTTON_EVENT_SW3_SHORT:
            /* SW3 click corto: CONTRAER = +5 grados (mas flexion). */
            joints_adjust(+5);
            uart_dbg_send_str("[BTN] SW3 short -> contraer (+5 deg)\r\n");
            break;
        case BUTTON_EVENT_SW3_LONG:
            /* SW3 long-press: RETRAER = -5 grados (mas extension).
             * Nota: se dispara una vez por presion al cruzar los 600 ms;
             * para seguir retrayendo hay que soltar y volver a mantener. */
            joints_adjust(-5);
            uart_dbg_send_str("[BTN] SW3 LONG -> retraer (-5 deg)\r\n");
            break;
        default:
            break;
    }
}

/* ---------------------------------------------------------------------------
 * Punto de entrada de la aplicacion
 * ------------------------------------------------------------------------ */
void appMain(void)
{
    /* Init en orden: primero los servicios base (tick, UART, LEDs), despues
     * la logica (joints, botones, CAN). El TX de CAN va al final porque
     * imprime "ready" por UART. */
    tick_init();
    uart_dbg_init();
    gpio_io_init();

    uart_dbg_send_str("\r\n\r\n");
    uart_dbg_send_str("=======================================\r\n");
    uart_dbg_send_str(" TX  FRDM-MCXA156  -  Protesis Robotica \r\n");
    uart_dbg_send_str("=======================================\r\n");

    buttons_init();
    joints_init();
    can_tx_init();

    uart_dbg_send_str("[INIT] modulos OK\r\n");
    uart_dbg_send_str("       FlexCAN0 @ 500 kbps, ID 0x200, DLC 8, periodo 50 ms\r\n");
    uart_dbg_send_str("       UI: SW2=+5  SW3=-5  SW3-long=ciclar articulacion\r\n");
    uart_dbg_send_str("---------------------------------------\r\n");

    /* Tiempos de la ultima ejecucion de cada tarea. */
    uint32_t u32LastButtonsMs = 0U;
    uint32_t u32LastCanTxMs   = 0U;
    uint32_t u32LastLedMs     = 0U;
    uint32_t u32LastLogMs     = 0U;

    /* Contadores informativos. */
    uint32_t u32CanAttempts   = 0U;
    uint32_t u32CanDropped    = 0U;

    for (;;)
    {
        const uint32_t u32NowMs = tick_get_ms();

        /* ----- Tarea 1: escaneo de botones cada 5 ms ----- */
        if ((u32NowMs - u32LastButtonsMs) >= APP_PERIOD_BUTTONS_MS)
        {
            u32LastButtonsMs = u32NowMs;
            button_event_t eEvt = buttons_scan(u32NowMs);
            app_handle_button_event(eEvt);
        }

        /* ----- Tarea 2: TX por CAN cada 50 ms ----- */
        if ((u32NowMs - u32LastCanTxMs) >= APP_PERIOD_CAN_TX_MS)
        {
            u32LastCanTxMs = u32NowMs;
            u32CanAttempts++;
            uint8_t au8Frame[JOINTS_FRAME_LEN];
            joints_pack_frame(au8Frame);
            if (!can_tx_send(APP_CAN_FRAME_ID, au8Frame, JOINTS_FRAME_LEN))
            {
                /* El TX anterior aun no termino o hubo error. Lo contamos
                 * para detectar problemas de bus desde el log. */
                u32CanDropped++;
            }
        }

        /* ----- Tarea 3: latido del LED azul cada 500 ms ----- */
        if ((u32NowMs - u32LastLedMs) >= APP_PERIOD_LED_MS)
        {
            u32LastLedMs = u32NowMs;
            gpio_io_toggle_blue();
        }

        /* ----- Tarea 4: log periodico por UART cada 1 segundo ----- */
        if ((u32NowMs - u32LastLogMs) >= APP_PERIOD_LOG_MS)
        {
            u32LastLogMs = u32NowMs;
            joints_t s = joints_get();
            char acBuf[112];
            int iLen = snprintf(acBuf, sizeof(acBuf),
                                "TX | S=%3u  E=%3u  W=%3u  sel=%s  "
                                "can[ok=%u err=%u drop=%u]\r\n",
                                (unsigned)s.u8Shoulder,
                                (unsigned)s.u16Elbow,
                                (unsigned)s.u8Wrist,
                                joint_name(joints_get_selected()),
                                (unsigned)can_tx_get_ok_count(),
                                (unsigned)can_tx_get_err_count(),
                                (unsigned)u32CanDropped);
            (void)u32CanAttempts;
            if (iLen > 0)
            {
                uart_dbg_send_str(acBuf);
            }
        }
    }
}
