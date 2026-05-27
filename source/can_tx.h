/*
 * can_tx.h - Driver de transmision CAN para el nodo TX (FRDM-MCXA156).
 *
 * Este modulo encapsula la inicializacion y transmision por FlexCAN0 a
 * 500 kbps, frame estandar (11-bit ID), DLC 8. Esta hecho a partir del
 * ejemplo `flexcan_interrupt_transfer` del MCUXpresso SDK para A156
 * (recortando todo lo que tenia que ver con RX/Wakeup/CAN-FD), por lo
 * cual reutiliza las APIs del driver `fsl_flexcan.h` tal cual:
 * FLEXCAN_Init, FLEXCAN_TransferCreateHandle,
 * FLEXCAN_TransferSendNonBlocking, etc.
 *
 * El frame fisico se manda con periodo 50 ms desde la capa de aplicacion;
 * este modulo solo se encarga de la mecanica de FlexCAN.
 */

#ifndef CAN_TX_H_
#define CAN_TX_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Inicializa FlexCAN0 a 500 kbps, frame estandar, sin loopback.
 *
 * Pasos internos (ver can_tx.c para detalles):
 *  1. Configura el reloj del FLEXCAN0 (kFRO_HF_DIV_to_FLEXCAN0 a 96 MHz)
 *     y lo saca de reset.
 *  2. Habilita el reloj del PORT1 y configura P1_12 (RX) / P1_13 (TX) como
 *     CAN0_RXD/CAN0_TXD (ALT11).
 *  3. Llama a FLEXCAN_GetDefaultConfig para llenar la estructura de config
 *     con valores razonables (clock source 0, sin self-wakeup, ni listen-only,
 *     etc.).
 *  4. Sobrescribe bitRate = 500000 (no se toca timingConfig manualmente:
 *     pedimos al driver que calcule los segments con
 *     FLEXCAN_CalculateImprovedTimingValues).
 *  5. Llama a FLEXCAN_Init con el reloj efectivo del modulo
 *     (CLOCK_GetFlexcanClkFreq()).
 *  6. Registra un callback ISR-driven con FLEXCAN_TransferCreateHandle.
 *  7. Configura el message buffer de TX (kFLEXCAN_FrameFormatStandard,
 *     enable=true).
 */
void can_tx_init(void);

/*!
 * @brief Transmite un frame CAN no-bloqueante.
 *
 * @param u32StdId  ID estandar de 11 bits (se enmascara con FLEXCAN_ID_STD).
 * @param pu8Data   Puntero al payload (hasta 8 bytes).
 * @param u8Len     Longitud del payload en bytes (DLC), 0..8.
 *
 * @return true si el frame se envio a la cola del FlexCAN. false si el TX
 *         anterior aun esta en vuelo (caller deberia reintentar despues
 *         o reportar drop).
 *
 * La funcion regresa inmediatamente. El callback registrado en init marca
 * una bandera interna cuando el byte final sale al bus. Ver `can_tx_is_idle`.
 */
bool can_tx_send(uint32_t u32StdId, const uint8_t *pu8Data, uint8_t u8Len);

/*!
 * @brief Reporta si el transmisor esta listo para aceptar otro frame.
 *
 * @return true si el ultimo TX termino; false si todavia esta en vuelo.
 */
bool can_tx_is_idle(void);

/*!
 * @brief Devuelve el numero de frames transmitidos exitosamente (contador
 *        que el callback incrementa cada vez que ve kStatus_FLEXCAN_TxIdle
 *        sobre el message buffer de TX). Util para logging y diagnostico.
 */
uint32_t can_tx_get_ok_count(void);

/*!
 * @brief Devuelve el numero de errores reportados por el callback
 *        (kStatus_FLEXCAN_ErrorStatus, overruns, BusOff, etc).
 */
uint32_t can_tx_get_err_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_TX_H_ */
