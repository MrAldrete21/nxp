/*
 * main.c - Punto de entrada del firmware del nodo RX (FRDM-MCXA156).
 *
 * Proyecto 2 NXP-UAG: Red CAN para Coordinacion de Protesis Robotica Modular.
 *
 * Este archivo reemplaza al `led_blinky.c` que importa el SDK Wizard como
 * base. La logica real vive en app.c; aqui solo se llama al boot de la
 * HAL y se arranca el ciclo principal.
 */

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

/* La variante led_blinky_peripheral del SDK genera tambien peripherals.h
 * (creado por MCUXpresso Config Tools). La variante driver_examples/led_blinky
 * "basica" NO lo trae. Lo incluimos con guard para que el codigo compile
 * sin cambios en cualquiera de las dos variantes. */
#if defined(__has_include)
#  if __has_include("peripherals.h")
#    include "peripherals.h"
#    define APP_HAS_PERIPHERALS_H  1
#  endif
#endif

#include "app.h"

int main(void)
{
    /* Inicializacion mandatoria generada por MCUXpresso Config Tools.
     * BOARD_InitBootPins: configura los pines definidos en pin_mux.c.
     * En el led_blinky base solo configura el LED rojo; los pines de
     * LPUART0, CAN0 y los otros LEDs los configuran los modulos
     * respectivos (uart_dbg, can_rx, gpio_io) en su propia init. */
    BOARD_InitBootPins();

    /* BOARD_InitBootClocks: arranca el arbol de clocks del MCU.
     * Por default en el SDK FRDM-MCXA156 v26.3.0 llama a
     * BOARD_BootClockFRO96M, que deja SystemCoreClock = 96 MHz y
     * FRO_HF a 96 MHz (necesario para que CAN0 alcance 500 kbps con
     * buen timing). */
    BOARD_InitBootClocks();

#if defined(APP_HAS_PERIPHERALS_H)
    /* BOARD_InitBootPeripherals: solo existe en la variante _peripheral.
     * Por default es no-op si no hay perifericos configurados en Config
     * Tools, pero la llamamos para mantener simetria con el TX. */
    BOARD_InitBootPeripherals();
#endif

    /* NOTA: NO llamamos a BOARD_InitDebugConsole() porque enchufa el
     * DbgConsole + newlib + PRINTF, que no usamos en este proyecto.
     * uart_dbg_init() (llamado desde appMain) hace la secuencia
     * directa con LPUART_Init. */

    /* Cede el control a la capa de aplicacion (lazo infinito). */
    appMain();

    /* Nunca regresa. */
    return 0;
}
