/*
 * joints.h - Estado de las tres articulaciones de la protesis + empaquetado
 *            del frame CAN.
 *
 * El nodo TX (en este proyecto: FRDM-MCXA156) mantiene los angulos actuales
 * de hombro, codo y muneca en RAM. Los botones modifican estos valores;
 * cada 50 ms la capa de aplicacion llama a `joints_pack_frame` para obtener
 * los 8 bytes que se mandan por CAN al receptor (FRDM-MCXN947).
 *
 * Mapa del frame CAN (ID = 0x200, DLC = 8):
 *
 *   byte 0   : shoulder (u8, 0..180 grados)
 *   byte 1   : elbow HIGH (u16 big-endian, byte alto)
 *   byte 2   : elbow LOW
 *   byte 3   : wrist (u8, 0..90 grados)
 *   byte 4-7 : reservado (0x00)
 *
 * Nota: aunque el rango de elbow (0..150) cabe en u8, lo declaramos como
 * u16 big-endian para dejar espacio a extensiones (resolucion decimal,
 * rango mayor en revisiones futuras). El receptor recompone el u16 con
 * `(byte1 << 8) | byte2`.
 *
 * Este modulo es codigo C puro (no depende del SDK), asi que se puede
 * compartir literalmente entre los dos nodos si fuese necesario.
 */

#ifndef JOINTS_H_
#define JOINTS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tamanio del payload CAN del proyecto. */
#define JOINTS_FRAME_LEN        (8U)

/* Identidad de cada articulacion. Sirve para `joints_select` y para
 * `joints_adjust`. */
typedef enum
{
    JOINT_SHOULDER = 0,
    JOINT_ELBOW    = 1,
    JOINT_WRIST    = 2,
    JOINT_COUNT    = 3,  /* sentinel: cantidad de articulaciones */
} joint_id_t;

/* Estructura del estado interno. Se expone solo a traves de getters para
 * mantener el invariante de rango valido. */
typedef struct
{
    uint8_t  u8Shoulder;   /* 0 .. 180 */
    uint16_t u16Elbow;     /* 0 .. 150 */
    uint8_t  u8Wrist;      /* 0 .. 90  */
} joints_t;

/*!
 * @brief Inicializa el estado a una pose neutra:
 *        shoulder=90, elbow=75, wrist=45, articulacion seleccionada = SHOULDER.
 */
void joints_init(void);

/*!
 * @brief Devuelve una copia inmutable del estado actual.
 */
joints_t joints_get(void);

/*!
 * @brief Devuelve cual articulacion esta actualmente seleccionada
 *        (la que modificaran los botones "+" y "-").
 */
joint_id_t joints_get_selected(void);

/*!
 * @brief Avanza la articulacion seleccionada al siguiente en orden ciclico:
 *        SHOULDER -> ELBOW -> WRIST -> SHOULDER ...
 */
void joints_cycle_selection(void);

/*!
 * @brief Modifica la articulacion actualmente seleccionada en `iDeltaDeg`
 *        grados (positivo o negativo). Saturacion al rango permitido de
 *        cada articulacion. NO produce wrap-around.
 *
 * @param iDeltaDeg Delta en grados (tipicamente +/- 5).
 */
void joints_adjust(int16_t iDeltaDeg);

/*!
 * @brief Construye los 8 bytes del frame CAN siguiendo el layout del
 *        Proyecto 2. Llena `pu8Out[0..7]` segun:
 *
 *          pu8Out[0] = shoulder
 *          pu8Out[1] = (elbow >> 8) & 0xFF      // HIGH
 *          pu8Out[2] = elbow & 0xFF             // LOW
 *          pu8Out[3] = wrist
 *          pu8Out[4..7] = 0
 *
 * @param pu8Out Buffer de salida (al menos 8 bytes).
 */
void joints_pack_frame(uint8_t *pu8Out);

#ifdef __cplusplus
}
#endif

#endif /* JOINTS_H_ */
