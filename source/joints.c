/*
 * joints.c - Implementacion del estado de articulaciones.
 *
 * No depende del SDK; es C puro. La logica vive aqui para mantener el modulo
 * de UI (botones) y el de transmision (CAN) ignorantes de los rangos
 * concretos de cada articulacion.
 */

#include "joints.h"

#include <string.h>     /* memset */

/* Limites por articulacion (de la spec del Proyecto 2). */
#define JOINT_SHOULDER_MAX_DEG   (180U)
#define JOINT_ELBOW_MAX_DEG      (150U)
#define JOINT_WRIST_MAX_DEG      (90U)

/* Valores iniciales (pose neutra). */
#define JOINT_SHOULDER_INIT      (90U)
#define JOINT_ELBOW_INIT         (75U)
#define JOINT_WRIST_INIT         (45U)

/* Estado privado del modulo. */
static joints_t   g_joints;
static joint_id_t g_selected;

/* ---------------------------------------------------------------------------
 * Helper: satura un valor a [iMin, iMax]. Usamos int32 internamente para
 * evitar wrap-around al sumar valores negativos. Lo devuelve como u16
 * (suficiente para todos los rangos del proyecto).
 * ------------------------------------------------------------------------ */
static uint16_t clamp_u16(int32_t iVal, int32_t iMin, int32_t iMax)
{
    if (iVal < iMin) { return (uint16_t)iMin; }
    if (iVal > iMax) { return (uint16_t)iMax; }
    return (uint16_t)iVal;
}

/* ---------------------------------------------------------------------------
 * API publica
 * ------------------------------------------------------------------------ */

void joints_init(void)
{
    g_joints.u8Shoulder = JOINT_SHOULDER_INIT;
    g_joints.u16Elbow   = JOINT_ELBOW_INIT;
    g_joints.u8Wrist    = JOINT_WRIST_INIT;
    g_selected          = JOINT_SHOULDER;
}

joints_t joints_get(void)
{
    return g_joints;   /* copia por valor */
}

joint_id_t joints_get_selected(void)
{
    return g_selected;
}

void joints_cycle_selection(void)
{
    /* Avance ciclico: 0->1->2->0. */
    g_selected = (joint_id_t)(((int)g_selected + 1) % (int)JOINT_COUNT);
}

void joints_adjust(int16_t iDeltaDeg)
{
    /* Aplicamos el delta a la articulacion seleccionada saturando al rango
     * declarado en la spec. Usamos int32 intermedio para no perder signo. */
    switch (g_selected)
    {
        case JOINT_SHOULDER:
        {
            int32_t iNew = (int32_t)g_joints.u8Shoulder + iDeltaDeg;
            g_joints.u8Shoulder =
                (uint8_t)clamp_u16(iNew, 0, JOINT_SHOULDER_MAX_DEG);
            break;
        }
        case JOINT_ELBOW:
        {
            int32_t iNew = (int32_t)g_joints.u16Elbow + iDeltaDeg;
            g_joints.u16Elbow =
                clamp_u16(iNew, 0, JOINT_ELBOW_MAX_DEG);
            break;
        }
        case JOINT_WRIST:
        {
            int32_t iNew = (int32_t)g_joints.u8Wrist + iDeltaDeg;
            g_joints.u8Wrist =
                (uint8_t)clamp_u16(iNew, 0, JOINT_WRIST_MAX_DEG);
            break;
        }
        default:
            /* No-op para valores fuera del enum (no deberia pasar). */
            break;
    }
}

void joints_pack_frame(uint8_t *pu8Out)
{
    if (pu8Out == NULL)
    {
        return;
    }
    /* Limpiamos primero para que los bytes reservados queden en 0
     * deterministicamente (la spec lo exige). */
    memset(pu8Out, 0, JOINTS_FRAME_LEN);

    pu8Out[0] = g_joints.u8Shoulder;
    /* Big-endian: byte alto antes que el bajo. Lo recompone el receptor
     * como (byte1 << 8) | byte2. */
    pu8Out[1] = (uint8_t)((g_joints.u16Elbow >> 8) & 0xFFU);
    pu8Out[2] = (uint8_t)(g_joints.u16Elbow & 0xFFU);
    pu8Out[3] = g_joints.u8Wrist;
    /* pu8Out[4..7] ya quedaron en 0 por el memset previo. */
}
