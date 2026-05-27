/*
 * can_tx.c - Implementacion del wrapper FlexCAN0 para el TX (FRDM-MCXA156).
 *
 * Este archivo es una version recortada y reorganizada del ejemplo del SDK
 * `boards/frdmmcxa156/driver_examples/flexcan/interrupt_transfer/
 *  flexcan_interrupt_transfer.c`.
 *
 * Cambios respecto al ejemplo del SDK:
 *  - Quitamos el menu interactivo "elija nodo A o B" (no aplica: el TX
 *    siempre es transmisor).
 *  - Quitamos toda la rama de RX (al receptor lo manejara la MCXN947).
 *    Igual configuramos el pin CAN0_RXD porque el transceiver fisico necesita
 *    el par TX/RX para que el controlador pueda recibir el ACK del bus.
 *  - Quitamos la rama #if USE_CANFD (este proyecto es CAN clasico).
 *  - Quitamos la rama #if USE_PHY_TJA1152 (la FRDM-MCXA156 trae TJA1057,
 *    que NO necesita configuracion previa).
 *  - Sacamos las llamadas a CLOCK_AttachClk / RESET_ReleasePeripheralReset
 *    / PORT_SetPinConfig a este modulo (en el ejemplo viven en hardware_init.c
 *    y pin_mux.c). Asi el modulo es autosuficiente.
 *  - Convertimos el ciclo `while(!txComplete)` bloqueante en una bandera
 *    consultable (can_tx_is_idle). Asi la capa de aplicacion no se queda
 *    pegada esperando a que termine el TX.
 *
 * APIs del SDK reutilizadas TAL CUAL (driver fsl_flexcan.h):
 *  - flexcan_config_t, flexcan_timing_config_t, flexcan_frame_t,
 *    flexcan_mb_transfer_t, flexcan_handle_t
 *  - FLEXCAN_GetDefaultConfig
 *  - FLEXCAN_CalculateImprovedTimingValues
 *  - FLEXCAN_Init
 *  - FLEXCAN_TransferCreateHandle
 *  - FLEXCAN_SetTxMbConfig
 *  - FLEXCAN_TransferSendNonBlocking
 *  - Macros: FLEXCAN_ID_STD, kFLEXCAN_FrameFormatStandard,
 *            kFLEXCAN_FrameTypeData, kStatus_FLEXCAN_TxIdle, etc.
 */

#include "can_tx.h"

#include <string.h>      /* memset */

#include "fsl_flexcan.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
#include "fsl_common.h"

/* ---------------------------------------------------------------------------
 * Constantes de configuracion (analogas a las macros del app.h del ejemplo)
 * ------------------------------------------------------------------------ */
/* Instancia: usamos CAN0 porque es la que el SDK conecta al transceiver
 * TJA1057 onboard de la FRDM-MCXA156 (conector J22). */
#define CAN_TX_PERIPH               CAN0

/* Solo necesitamos UN message buffer para TX. En el ejemplo usaban MB1
 * para TX y MB0 para RX. Mantenemos MB1 para coincidir con el ejemplo. */
#define CAN_TX_MB_NUM               (1U)

/* Bitrate del bus en bps. El proyecto 2 fija 500 kbps. */
#define CAN_TX_BITRATE_BPS          (500000U)

/* Frecuencia del reloj del modulo FLEXCAN0. CLOCK_GetFlexcanClkFreq() en
 * MCXA156 NO recibe argumento (a diferencia de MCXN947 que pide la
 * instancia). Devuelve el reloj efectivo despues de CLOCK_AttachClk +
 * CLOCK_SetClockDiv. Con kFRO_HF_DIV_to_FLEXCAN0 a 1:1 da 96 MHz. */
#define CAN_TX_CLOCK_FREQ_HZ        CLOCK_GetFlexcanClkFreq()

/* Pin mux para CAN0 en FRDM-MCXA156:
 *   - PORT1.12 = CAN0_RXD (ALT11)
 *   - PORT1.13 = CAN0_TXD (ALT11)
 * Ambos van directo al transceiver TJA1057 onboard (conector J22). */
#define CAN_TX_PORT                 PORT1
#define CAN_TX_PIN_RXD              (12U)
#define CAN_TX_PIN_TXD              (13U)
#define CAN_TX_PIN_ALT              kPORT_MuxAlt11

/* ---------------------------------------------------------------------------
 * Variables internas del modulo
 * ------------------------------------------------------------------------ */

/* Handle del driver de transferencias FlexCAN (lo escribe el driver). */
static flexcan_handle_t g_flexcanHandle;

/* Estructura del frame que se transmite. Se reutiliza entre llamadas a
 * can_tx_send porque FLEXCAN_TransferSendNonBlocking se queda con un
 * puntero hasta que termina, asi que el buffer debe ser estable. */
static flexcan_frame_t g_txFrame;

/* Estructura del transfer (lo mismo: la pasamos por puntero al driver). */
static flexcan_mb_transfer_t g_txXfer;

/* Bandera "ultimo TX completado". `volatile` porque el callback se ejecuta
 * en contexto de ISR y la capa de app la lee desde main thread. */
static volatile bool g_bTxIdle = true;

/* Contadores de diagnostico. Actualizados solo por el callback. */
static volatile uint32_t g_u32TxOkCount  = 0U;
static volatile uint32_t g_u32TxErrCount = 0U;

/* ---------------------------------------------------------------------------
 * Callback del driver FlexCAN
 *
 * El SDK lo declara con la macro FLEXCAN_CALLBACK(name) que expande a
 * `void name(CAN_Type *base, flexcan_handle_t *handle, status_t status,
 *           uint32_t result, void *userData)`.
 *
 * Lo invoca el driver desde la ISR del FlexCAN cuando ocurre una transicion
 * de estado (TxIdle, RxIdle, ErrorStatus, etc.). Aqui solo nos interesa el
 * TxIdle sobre nuestro MB.
 * ------------------------------------------------------------------------ */
static FLEXCAN_CALLBACK(can_tx_callback)
{
    (void)base;
    (void)handle;
    (void)userData;

    switch (status)
    {
        case kStatus_FLEXCAN_TxIdle:
        {
            /* `result` lleva el indice del message buffer que provoco el
             * evento. Solo declaramos completo si es NUESTRO MB. */
            if (result == CAN_TX_MB_NUM)
            {
                g_bTxIdle = true;
                g_u32TxOkCount++;
            }
            break;
        }
        case kStatus_FLEXCAN_ErrorStatus:
        {
            /* El bus reporto algo: bit error, stuff error, ACK error, etc.
             * En este proyecto basta con contar. Una version mas robusta
             * inspeccionaria `result` que tiene los flags de ESR1. */
            g_u32TxErrCount++;
            /* Liberamos el TX para no quedarnos colgados si el error fue
             * que nadie respondio con ACK (caso comun en bench sin RX). */
            g_bTxIdle = true;
            break;
        }
        default:
            /* Otros estados (RxIdle, WakeUp) no aplican al TX. */
            break;
    }
}

/* ---------------------------------------------------------------------------
 * Configuracion del reloj del periferico FLEXCAN0
 *
 * Esto vive en hardware_init.c del ejemplo SDK (BOARD_InitHardware).
 * Lo replicamos aqui para que el modulo sea autosuficiente.
 * ------------------------------------------------------------------------ */
static void can_tx_init_clock(void)
{
    /* Asegurarnos que el FRO_HF_DIV esta a 1:1 (lo mismo que FRO_HF,
       96 MHz por default). BOARD_InitBootClocks() del ejemplo led_blinky
       ya hace esto, pero por seguridad lo dejamos explicito. */
    CLOCK_SetClockDiv(kCLOCK_DivFRO_HF_DIV, 1U);

    /* Divisor del FLEXCAN0 a 1:1 (clock directo). */
    CLOCK_SetClockDiv(kCLOCK_DivFLEXCAN0, 1U);

    /* Conecta el FRO_HF_DIV (96 MHz) como fuente del FLEXCAN0. Es la
       opcion estandar del SDK para alcanzar 500 kbps con timing decente. */
    CLOCK_AttachClk(kFRO_HF_DIV_to_FLEXCAN0);

    /* Saca al FLEXCAN0 de reset. */
    RESET_ReleasePeripheralReset(kFLEXCAN0_RST_SHIFT_RSTn);
}

/* ---------------------------------------------------------------------------
 * Configuracion de los pines CAN0_TXD/RXD en PORT1
 *
 * El ejemplo del SDK hace esto en pin_mux.c via Config Tools. Lo replicamos
 * a mano aqui para que el modulo sea autosuficiente.
 * ------------------------------------------------------------------------ */
static void can_tx_init_pins(void)
{
    /* PORT1 necesita su clock habilitado para poder escribirle a sus
     * registros PCR (Pin Control Register). */
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);

    /* Configuracion comun para ambos pines: pull deshabilitado (el
     * transceiver maneja la linea), slew rate fast (CAN cambia rapido),
     * drive strength low/normal, mux a ALT11 (CAN0_RXD/TXD), input
     * buffer enable.
     *
     * port_pin_config_t en MCXA156 tiene 11 campos (uno extra "drive
     * strength 1"). Usamos init posicional. */
    const port_pin_config_t kCanPortCfg =
    {
        kPORT_PullDisable,            /* pullSelect          */
        kPORT_LowPullResistor,        /* pullValueSelect     */
        kPORT_FastSlewRate,           /* slewRate            */
        kPORT_PassiveFilterDisable,   /* passiveFilterEnable */
        kPORT_OpenDrainDisable,       /* openDrainEnable     */
        kPORT_LowDriveStrength,       /* driveStrength0      */
        kPORT_NormalDriveStrength,    /* driveStrength1      */
        CAN_TX_PIN_ALT,               /* mux = ALT11 (CAN0)  */
        kPORT_InputBufferEnable,      /* inputBuffer         */
        kPORT_InputNormal,            /* invertInput         */
        kPORT_UnlockRegister,         /* lockRegister        */
    };

    PORT_SetPinConfig(CAN_TX_PORT, CAN_TX_PIN_RXD, &kCanPortCfg);
    PORT_SetPinConfig(CAN_TX_PORT, CAN_TX_PIN_TXD, &kCanPortCfg);
}

/* ---------------------------------------------------------------------------
 * can_tx_init - Configura el modulo FlexCAN0
 *
 * Esta es practicamente la misma secuencia que el `main()` del ejemplo
 * `flexcan_interrupt_transfer.c`, pero limitada al lado TX.
 * ------------------------------------------------------------------------ */
void can_tx_init(void)
{
    /* (a) Reloj del periferico (FRO_HF_DIV @ 96 MHz). */
    can_tx_init_clock();

    /* (b) Pin mux de los pines CAN0. */
    can_tx_init_pins();

    /* (1) Llenamos la struct de config con defaults del driver.
     *     FLEXCAN_GetDefaultConfig setea:
     *       - clkSrc                = kFLEXCAN_ClkSrc0
     *       - bitRate               = 1000000U  (lo sobrescribimos)
     *       - maxMbNum              = 16
     *       - enableLoopBack        = false
     *       - enableSelfWakeup      = false
     *       - enableIndividMask     = false
     *       - disableSelfReception  = false
     *       - enableListenOnlyMode  = false
     *       - enableDoze            = false
     */
    flexcan_config_t flexcanConfig;
    FLEXCAN_GetDefaultConfig(&flexcanConfig);

    /* (2) Bitrate del proyecto. */
    flexcanConfig.bitRate = CAN_TX_BITRATE_BPS;

    /* (3) Pedimos al SDK que calcule los segmentos optimos (propSeg, phaseSeg1,
     *     phaseSeg2, RJW) para nuestro reloj + bitrate. */
    flexcan_timing_config_t timingConfig;
    memset(&timingConfig, 0, sizeof(timingConfig));
    if (FLEXCAN_CalculateImprovedTimingValues(CAN_TX_PERIPH,
                                              flexcanConfig.bitRate,
                                              CAN_TX_CLOCK_FREQ_HZ,
                                              &timingConfig))
    {
        memcpy(&(flexcanConfig.timingConfig),
               &timingConfig,
               sizeof(flexcan_timing_config_t));
    }

    /* (4) Inicializa el periferico: programa CTRL1, MCR, etc.
     *     El tercer argumento es el reloj del modulo (no del nucleo). */
    FLEXCAN_Init(CAN_TX_PERIPH, &flexcanConfig, CAN_TX_CLOCK_FREQ_HZ);

    /* (5) Registra nuestro callback. NULL en el ultimo argumento porque no
     *     usamos userData (el callback ya tiene acceso a las globales del
     *     modulo). */
    FLEXCAN_TransferCreateHandle(CAN_TX_PERIPH, &g_flexcanHandle,
                                 can_tx_callback, NULL);

    /* (6) Configura el message buffer de TX. enable=true significa "marca
     *     este MB como TX inactivo (Tx Inactive)" para que el TransferSend
     *     posterior lo arme cuando le toque. */
    FLEXCAN_SetTxMbConfig(CAN_TX_PERIPH, CAN_TX_MB_NUM, true);

    /* Estado inicial: listos para transmitir. */
    g_bTxIdle = true;
}

/* ---------------------------------------------------------------------------
 * can_tx_send - Lanza un frame CAN sin bloquear
 * ------------------------------------------------------------------------ */
bool can_tx_send(uint32_t u32StdId, const uint8_t *pu8Data, uint8_t u8Len)
{
    /* Si el TX anterior aun esta en vuelo, descartamos. La aplicacion
     * decidira si reintentar o ignorar (en el lazo de 50 ms, ignorar
     * 1 frame es invisible). */
    if (!g_bTxIdle)
    {
        return false;
    }
    if ((pu8Data == NULL) || (u8Len > 8U))
    {
        return false;
    }

    /* Marcamos ocupado ANTES de iniciar el transfer; el callback la
     * limpiara cuando termine. */
    g_bTxIdle = false;

    /* Armado del frame. Estos campos vienen del struct `flexcan_frame_t`
     * definido en fsl_flexcan.h.
     *   - id     : 11 bits de identificador desplazados a la posicion
     *              que espera el hardware (lo hace la macro FLEXCAN_ID_STD).
     *   - format : estandar (11 bits) vs extendido (29 bits). Usamos estandar.
     *   - type   : frame de datos (vs. remoto). */
    g_txFrame.id     = FLEXCAN_ID_STD(u32StdId);
    g_txFrame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
    g_txFrame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
    g_txFrame.length = u8Len;

    /* Copia del payload. El struct expone los 8 bytes como dataByte0..7
     * dentro de una union con dataWord0/dataWord1. Es seguro asignar
     * byte por byte. Los bytes que no se usan (len < 8) tambien se
     * limpian para no transmitir basura. */
    g_txFrame.dataByte0 = (u8Len > 0U) ? pu8Data[0] : 0U;
    g_txFrame.dataByte1 = (u8Len > 1U) ? pu8Data[1] : 0U;
    g_txFrame.dataByte2 = (u8Len > 2U) ? pu8Data[2] : 0U;
    g_txFrame.dataByte3 = (u8Len > 3U) ? pu8Data[3] : 0U;
    g_txFrame.dataByte4 = (u8Len > 4U) ? pu8Data[4] : 0U;
    g_txFrame.dataByte5 = (u8Len > 5U) ? pu8Data[5] : 0U;
    g_txFrame.dataByte6 = (u8Len > 6U) ? pu8Data[6] : 0U;
    g_txFrame.dataByte7 = (u8Len > 7U) ? pu8Data[7] : 0U;

    /* El transfer le dice al driver que MB usar y donde esta el frame. */
    g_txXfer.mbIdx = (uint8_t)CAN_TX_MB_NUM;
    g_txXfer.frame = &g_txFrame;

    /* Lanza el TX. La funcion regresa enseguida; el bus se transmite por
     * hardware y el callback nos avisa cuando termino. */
    status_t s = FLEXCAN_TransferSendNonBlocking(CAN_TX_PERIPH,
                                                 &g_flexcanHandle,
                                                 &g_txXfer);
    if (s != kStatus_Success)
    {
        /* Algo salio mal de entrada (p.ej. el MB ya esta activo). Liberamos
         * el flag para que el siguiente intento pueda proceder. */
        g_bTxIdle = true;
        g_u32TxErrCount++;
        return false;
    }
    return true;
}

bool can_tx_is_idle(void)
{
    return g_bTxIdle;
}

uint32_t can_tx_get_ok_count(void)
{
    return g_u32TxOkCount;
}

uint32_t can_tx_get_err_count(void)
{
    return g_u32TxErrCount;
}
