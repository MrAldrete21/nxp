/*
 * uart_dbg.c - Implementacion del wrapper de salida por LPUART0 (FRDM-MCXA156).
 *
 * Detalles del LPUART en MCXA156:
 *   - El LPUART0 es un periferico independiente (NO esta detras de FlexComm
 *     como en MCXN947). Por eso no hace falta LP_FLEXCOMM_Init.
 *   - El reloj se asigna con CLOCK_AttachClk(kFRO12M_to_LPUART0).
 *   - El divisor se setea con CLOCK_SetClockDiv(kCLOCK_DivLPUART0, 1).
 *   - Hay que sacar al periferico de reset con RESET_PeripheralReset()
 *     antes de configurarlo.
 *
 * Comparacion lado a lado con el TX (MCXN947):
 *
 *   TX (N947)                                  RX (A156)
 *   ---------                                  ---------
 *   CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1)  CLOCK_SetClockDiv(kCLOCK_DivLPUART0, 1)
 *   CLOCK_AttachClk(kFRO12M_to_FLEXCOMM4)      CLOCK_AttachClk(kFRO12M_to_LPUART0)
 *   RESET_ClearPeripheralReset(kFC4_RST...)    RESET_PeripheralReset(kLPUART0_RST_SHIFT_RSTn)
 *   LP_FLEXCOMM_Init(4, ...LPUART)             (no aplica)
 *   PORT1 P1_8/P1_9 ALT2                       PORT0 P0_2/P0_3 ALT2
 *   LPUART_Init(LPUART4, ...)                  LPUART_Init(LPUART0, ...)
 *
 * No se manejan interrupciones de RX por ahora; el debug es solo de salida.
 */

#include "uart_dbg.h"

#include "fsl_lpuart.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
#include "board.h"     /* BOARD_DEBUG_UART_BASEADDR, BOARD_DEBUG_UART_CLK_ATTACH, etc. */

/* Direccion base del LPUART de debug. La macro la provee board.h del SDK,
 * que la define como (uint32_t) LPUART0 — hay que castearla al tipo. */
#define UART_DBG_BASEADDR    ((LPUART_Type *)BOARD_DEBUG_UART_BASEADDR)

/* Baudrate del bring-up. */
#define UART_DBG_BAUD        (115200U)

/* Reloj efectivo del modulo. board.h del A156 define BOARD_DEBUG_UART_CLK_FREQ
 * = 12000000 (12 MHz, FRO12M conectado por CLOCK_AttachClk). */
#define UART_DBG_CLK_FREQ    (BOARD_DEBUG_UART_CLK_FREQ)

/*!
 * @brief Configura los pines P0_2 (RX) y P0_3 (TX) como LPUART0 (ALT2).
 *
 * Estos pines NO los configura el pin_mux.c del led_blinky base, asi que
 * los configuramos aqui para que el modulo sea autosuficiente y no
 * dependa de regenerar pin_mux.c con MCUXpresso Config Tools.
 */
static void uart_dbg_init_pins(void)
{
    /* Habilitar reloj de PORT0 (necesario para escribir sus PCRs).
       En MCXA156 el clock gate del PORT lleva prefijo "Gate". */
    CLOCK_EnableClock(kCLOCK_GatePORT0);

    /* Sacar al PORT0 de reset. */
    RESET_ReleasePeripheralReset(kPORT0_RST_SHIFT_RSTn);

    /* Configuracion comun para ambos pines, replica la del pin_mux.c
       del ejemplo SDK lpuart_interrupt_transfer para A156:
         - pull-up enable, low pull resistor   (la linea idle queda en alto)
         - slew rate fast                       (UART cambia rapido)
         - filtro pasivo deshabilitado
         - open-drain off, push-pull
         - drive strength low / normal
         - MUX = ALT2 (LPUART0_RXD / TXD)
         - input buffer enable, no invert
         - lock register off.

       Estructura port_pin_config_t en MCXA156 tiene 11 campos (un campo
       extra "drive strength 1" entre driveStrength y mux respecto al
       MCXN947); usamos init posicional siguiendo el SDK. */
    const port_pin_config_t kUartPortCfg =
    {
        kPORT_PullUp,                 /* pullSelect          */
        kPORT_LowPullResistor,        /* pullValueSelect     */
        kPORT_FastSlewRate,           /* slewRate            */
        kPORT_PassiveFilterDisable,   /* passiveFilterEnable */
        kPORT_OpenDrainDisable,       /* openDrainEnable     */
        kPORT_LowDriveStrength,       /* driveStrength0      */
        kPORT_NormalDriveStrength,    /* driveStrength1 (MCXA156: campo extra) */
        kPORT_MuxAlt2,                /* mux = LPUART0       */
        kPORT_InputBufferEnable,      /* inputBuffer         */
        kPORT_InputNormal,            /* invertInput         */
        kPORT_UnlockRegister,         /* lockRegister        */
    };

    PORT_SetPinConfig(PORT0, 2U, &kUartPortCfg);  /* P0_2 = LPUART0_RXD */
    PORT_SetPinConfig(PORT0, 3U, &kUartPortCfg);  /* P0_3 = LPUART0_TXD */
}

/*
 * Secuencia exacta de inicializacion, copiada del flujo canonico de
 * BOARD_InitDebugConsole() en `board.c` del ejemplo SDK A156, pero
 * sustituyendo DbgConsole_Init por LPUART_Init directo (no queremos
 * que el firmware traiga newlib + PRINTF).
 */
void uart_dbg_init(void)
{
    /* (1) Setea el divisor del LPUART0 a 1 (clock directo, sin divisor).
           En MCXA156 la macro es CLOCK_SetClockDiv (NO CLOCK_SetClkDiv
           como en MCXN947) y el simbolo del divisor es kCLOCK_DivLPUART0
           (NO kCLOCK_DivFlexcom4Clk). */
    CLOCK_SetClockDiv(kCLOCK_DivLPUART0, 1U);

    /* (2) Conecta FRO12M al LPUART0 como fuente del periferico.
           BOARD_DEBUG_UART_CLK_ATTACH expande a kFRO12M_to_LPUART0. */
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    /* (3) Saca al LPUART0 de reset (al boot esta en reset).
           BOARD_DEBUG_UART_RST expande a kLPUART0_RST_SHIFT_RSTn. */
    RESET_PeripheralReset(BOARD_DEBUG_UART_RST);

    /* (4) Rutea P0_2/P0_3 al LPUART0 (ALT2). */
    uart_dbg_init_pins();

    /* (5) Init del LPUART propiamente dicho: baudrate, paridad, FIFO, etc.
           NOTA: en MCXA156 NO se llama LP_FLEXCOMM_Init: el LPUART es
           directo (no esta detras de FlexComm como en MCXN947). */
    lpuart_config_t config;
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = UART_DBG_BAUD;
    config.enableTx     = true;
    config.enableRx     = true;

    (void)LPUART_Init(UART_DBG_BASEADDR, &config, UART_DBG_CLK_FREQ);
}

void uart_dbg_send(const uint8_t *pu8Data, size_t szLen)
{
    if ((pu8Data == NULL) || (szLen == 0U))
    {
        return;
    }
    /* Bloquea solo hasta que el ultimo byte salga del shift register.
       A 115200 8N1, cada byte tarda ~87 us; una linea de 32 chars ~ 2.8 ms. */
    LPUART_WriteBlocking(UART_DBG_BASEADDR, pu8Data, szLen);
}

void uart_dbg_send_str(const char *pcStr)
{
    if (pcStr == NULL)
    {
        return;
    }
    size_t szLen = 0U;
    while (pcStr[szLen] != '\0')
    {
        szLen++;
    }
    uart_dbg_send((const uint8_t *)pcStr, szLen);
}
