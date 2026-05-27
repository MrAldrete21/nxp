/*
 * can_rx.h - Driver de recepcion CAN para el nodo RX (FRDM-MCXA156).
 *
 * Este modulo encapsula la inicializacion y recepcion por FlexCAN0 a
 * 500 kbps, frame estandar (11-bit ID), DLC 8. Esta hecho a partir del
 * ejemplo `flexcan_interrupt_transfer` del MCUXpresso SDK para A156,
 * recortando todo lo que tenia que ver con TX/Wakeup/CAN-FD/menu
 * interactivo, por lo cual reutiliza las APIs del driver `fsl_flexcan.h`
 * tal cual:
 *   FLEXCAN_Init, FLEXCAN_TransferCreateHandle, FLEXCAN_SetRxMbGlobalMask,
 *   FLEXCAN_SetRxMbConfig, FLEXCAN_TransferReceiveNonBlocking.
 *
 * Layout del frame que esperamos del TX (definido en el proyecto):
 *   byte0      = Hombro   (u8, 0..180)
 *   byte1      = Codo HI  (u16 big-endian -> codo = byte1<<8 | byte2, 0..150)
 *   byte2      = Codo LO
 *   byte3      = Muneca   (u8, 0..90)
 *   bytes 4..7 = 0 (reservados)
 *
 * El modulo dispone de:
 *   - can_rx_init():            arranca el periferico y deja el MB armado
 *                                en modo no-bloqueante.
 *   - can_rx_try_get():         non-blocking poll. Devuelve true si hubo
 *                                un frame nuevo desde la ultima vez que
 *                                se consulto, y entrega ya decodificado
 *                                el triple (S, E, W) y el tick (ms) en
 *                                el que se recibio.
 *   - can_rx_get_ok_count()     contadores de diagnostico (rx OK).
 *   - can_rx_get_err_count()    contadores de diagnostico (rx error).
 *
 * El receptor se rearma automaticamente en cada callback de RxIdle, asi
 * que el lazo de aplicacion NO tiene que llamar a nada para mantener
 * funcionando la cola. Solo polleea can_rx_try_get cuando le interese
 * obtener el ultimo dato.
 */

#ifndef CAN_RX_H_
#define CAN_RX_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Estructura con los 3 angulos decodificados del frame CAN.
 *        Coincide en tipos con `joints_t` del TX (ver tx_mcxn947/source/joints.h).
 */
typedef struct
{
    uint8_t  u8Shoulder;   /* Hombro,  0..180 grados                       */
    uint16_t u16Elbow;     /* Codo,    0..150 grados (codificado en 2 bytes)*/
    uint8_t  u8Wrist;      /* Muneca,  0..90  grados                       */
} can_rx_joints_t;

/*!
 * @brief Inicializa FlexCAN0 a 500 kbps en modo receptor.
 *
 * Pasos internos (ver can_rx.c para detalles):
 *  1. Habilita reloj del PORT1 y configura P1_12 (RX) / P1_13 (TX) como
 *     CAN0_RXD/CAN0_TXD (ALT11). El TX se configura aunque el modulo no
 *     transmita, porque el periferico fisico necesita el par RX/TX para
 *     que el controlador pueda generar ACK al frame que recibe.
 *  2. Conecta FRO_HF_DIV (96 MHz) al FLEXCAN0 y lo saca de reset.
 *  3. FLEXCAN_GetDefaultConfig -> sobrescribe bitRate=500000 -> pide al
 *     SDK que calcule el timing optimo (USE_IMPROVED_TIMING_CONFIG).
 *  4. FLEXCAN_Init.
 *  5. Registra callback ISR-driven con FLEXCAN_TransferCreateHandle.
 *  6. Aplica mascara global FLEXCAN_RX_MB_STD_MASK(0x7FF, 0, 0) -> filtro
 *     exacto: solo aceptan frames con el ID que armemos en MB0.
 *  7. Configura MB0 como RX estandar tipo data con ID 0x200, y lo arma
 *     con FLEXCAN_TransferReceiveNonBlocking. Desde aqui en adelante,
 *     cada frame entrante dispara el callback en contexto de ISR.
 */
void can_rx_init(void);

/*!
 * @brief Consulta si hubo frame nuevo desde la ultima llamada.
 *
 * Si hubo, llena `*psJoints` con los 3 angulos decodificados y limpia la
 * bandera interna. Si no, devuelve false sin tocar el destino.
 *
 * @param  psJoints       Puntero a la struct donde escribir los angulos.
 * @param  pu32TimestampMs Puede ser NULL. Si no, se escribe el valor de
 *                         tick_get_ms() en el instante en que el callback
 *                         marco el frame como completo.
 * @return true si habia un frame nuevo (y se entrego); false en otro caso.
 */
bool can_rx_try_get(can_rx_joints_t *psJoints, uint32_t *pu32TimestampMs);

/*!
 * @brief Frames OK recibidos desde el boot. El callback lo incrementa
 *        cada vez que ve kStatus_FLEXCAN_RxIdle sobre el MB de RX.
 */
uint32_t can_rx_get_ok_count(void);

/*!
 * @brief Errores reportados por el callback (kStatus_FLEXCAN_ErrorStatus,
 *        overruns, BusOff, etc.). Util para diagnostico desde el log.
 */
uint32_t can_rx_get_err_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_RX_H_ */
