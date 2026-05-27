/*
 * uart_dbg.h - Salida de debug por LPUART0 hacia el MCU-Link VCP (FRDM-MCXA156).
 *
 * Wrapper minimo sobre `fsl_lpuart.h` del SDK. NO se usa printf de la
 * stdlib para la salida (eso enchufa newlib y consume RAM/Flash);
 * snprintf SI se usa para formato hacia buffer local.
 *
 * Conexion fisica en la FRDM-MCXA156:
 *   - LPUART0 TX = P0_3  (ALT2)
 *   - LPUART0 RX = P0_2  (ALT2)
 *   - Ambas conectadas internamente al puente USB-Serial del MCU-Link.
 *
 * Configuracion: 115200 baud, 8 data bits, sin paridad, 1 stop bit, sin
 * control de flujo (8N1).
 *
 * NOTA: a diferencia del TX (MCXN947), en la MCXA156 el LPUART0 NO esta
 * detras de un bloque FlexComm. Es directo. Por tanto la secuencia de
 * inicializacion es mas corta (no hay LP_FLEXCOMM_Init).
 */

#ifndef UART_DBG_H_
#define UART_DBG_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Inicializa LPUART0 a 115200 8N1.
 *
 * Hace TODO en este modulo (no depende de BOARD_InitDebugConsole):
 *   - Habilita clock del LPUART0 (kCLOCK_DivLPUART0)
 *   - Conecta FRO12M como fuente del LPUART0
 *   - Saca al periferico de reset
 *   - Configura pin mux P0_2 (RX) y P0_3 (TX) a ALT2
 *   - Llama a LPUART_Init con baudrate 115200 y reloj 12 MHz.
 */
void uart_dbg_init(void);

/*!
 * @brief Transmite una cadena terminada en NUL por LPUART0 (bloqueante,
 *        pero muy rapido a la baudrate fijada: ~1.5 ms por linea de
 *        16 caracteres).
 *
 * @param pcStr  Puntero a cadena C terminada en '\\0'.
 */
void uart_dbg_send_str(const char *pcStr);

/*!
 * @brief Transmite un buffer arbitrario de bytes.
 *
 * @param pu8Data Puntero al primer byte.
 * @param szLen   Cantidad de bytes a enviar.
 */
void uart_dbg_send(const uint8_t *pu8Data, size_t szLen);

#ifdef __cplusplus
}
#endif

#endif /* UART_DBG_H_ */
