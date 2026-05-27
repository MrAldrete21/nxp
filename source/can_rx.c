/*
 * can_rx.c - Implementacion del wrapper FlexCAN0 para el nodo RX (MCXA156).
 *
 * Este archivo es una version recortada y reorganizada del ejemplo del SDK
 * `boards/frdmmcxa156/driver_examples/flexcan/interrupt_transfer/
 *  flexcan_interrupt_transfer.c`, limitada al lado de recepcion.
 *
 * Cambios respecto al ejemplo del SDK:
 *  - Quitamos el menu interactivo "elija nodo A o B" y los GETCHAR/PUTCHAR.
 *  - Quitamos toda la rama de TX en main (al transmisor lo manejara la
 *    MCXN947). De todos modos hay que configurar el pin CAN0_TXD: el
 *    transceiver fisico necesita el par TX/RX para poder generar el ACK
 *    al frame entrante.
 *  - Quitamos la rama #if USE_CANFD (este proyecto es CAN clasico).
 *  - Quitamos la rama #if USE_PHY_TJA1152 (la FRDM-MCXA156 trae TJA1057,
 *    que NO necesita configuracion previa).
 *  - Sacamos las llamadas a CLOCK_AttachClk / RESET_ReleasePeripheralReset
 *    / PORT_SetPinConfig a este modulo (en el ejemplo viven en
 *    hardware_init.c y pin_mux.c). Asi el modulo es autosuficiente y no
 *    requiere editar pin_mux.c del proyecto base led_blinky.
 *  - Convertimos la espera bloqueante `while(!rxComplete)` en una bandera
 *    consultable + rearmado automatico del MB en el callback.
 *
 * APIs del SDK reutilizadas TAL CUAL (driver fsl_flexcan.h):
 *  - flexcan_config_t, flexcan_timing_config_t, flexcan_frame_t,
 *    flexcan_mb_transfer_t, flexcan_rx_mb_config_t, flexcan_handle_t
 *  - FLEXCAN_GetDefaultConfig
 *  - FLEXCAN_CalculateImprovedTimingValues
 *  - FLEXCAN_Init
 *  - FLEXCAN_TransferCreateHandle
 *  - FLEXCAN_SetRxMbGlobalMask
 *  - FLEXCAN_SetRxMbConfig
 *  - FLEXCAN_TransferReceiveNonBlocking
 *  - Macros: FLEXCAN_ID_STD, FLEXCAN_RX_MB_STD_MASK,
 *            kFLEXCAN_FrameFormatStandard, kFLEXCAN_FrameTypeData,
 *            kStatus_FLEXCAN_RxIdle, kStatus_FLEXCAN_ErrorStatus, etc.
 */

#include "can_rx.h"

#include <string.h>      /* memset, memcpy */

#include "fsl_flexcan.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
#include "fsl_common.h"

#include "tick.h"        /* tick_get_ms() para timestamp del callback */

/* ---------------------------------------------------------------------------
 * Constantes de configuracion
 * ------------------------------------------------------------------------ */

/* Instancia: usamos CAN0 porque es la unica que el SDK conecta al
 * transceiver TJA1057 onboard de la FRDM-MCXA156. */
#define CAN_RX_PERIPH               CAN0

/* Numero del message buffer dedicado a recepcion. En el ejemplo del SDK
 * usan MB0 para RX y MB1 para TX; mantenemos MB0 para coincidir. */
#define CAN_RX_MB_NUM               (0U)

/* Bitrate del bus en bps. El proyecto 2 fija 500 kbps. */
#define CAN_RX_BITRATE_BPS          (500000U)

/* ID estandar del frame del proyecto (3 angulos de las articulaciones). */
#define CAN_RX_FRAME_ID             (0x200U)

/* Frecuencia del reloj del modulo FLEXCAN0. La obtenemos via el SDK
 * leyendo el arbol de clocks que setea BOARD_InitBootClocks +
 * CLOCK_AttachClk(kFRO_HF_DIV_to_FLEXCAN0). El default BOARD_BootClockFRO96M
 * deja el FRO_HF_DIV a 96 MHz, asi que el FlexCAN ve 96 MHz. */
#define CAN_RX_CLOCK_FREQ_HZ        CLOCK_GetFlexcanClkFreq()

/* Pin mux para CAN0 en FRDM-MCXA156:
 *   - PORT1.12 = CAN0_RXD (ALT11)
 *   - PORT1.13 = CAN0_TXD (ALT11)
 * Estos pines van directo al transceiver TJA1057 onboard (conector J22). */
#define CAN_RX_PORT                 PORT1
#define CAN_RX_PIN_RXD              (12U)
#define CAN_RX_PIN_TXD              (13U)
#define CAN_RX_PIN_ALT              kPORT_MuxAlt11

/* ---------------------------------------------------------------------------
 * Variables internas del modulo
 * ------------------------------------------------------------------------ */

/* Handle del driver de transferencias FlexCAN (lo escribe el driver). */
static flexcan_handle_t g_flexcanHandle;

/* Estructura del frame donde el driver deposita los bytes recibidos.
 * El driver guarda un puntero a este struct durante toda la transferencia,
 * asi que el buffer debe ser estable. */
static flexcan_frame_t g_rxFrame;

/* Estructura del transfer (lo mismo: la pasamos por puntero al driver). */
static flexcan_mb_transfer_t g_rxXfer;

/* Bandera "hay frame nuevo no consumido aun" + storage del frame
 * decodificado. volatile porque el callback corre en ISR y el lazo
 * principal las lee desde el main thread. */
static volatile bool             g_bFrameAvail   = false;
static volatile can_rx_joints_t  g_sLatestJoints = { 0U, 0U, 0U };
static volatile uint32_t         g_u32LatestTickMs = 0U;

/* Contadores de diagnostico. Actualizados solo por el callback. */
static volatile uint32_t g_u32RxOkCount  = 0U;
static volatile uint32_t g_u32RxErrCount = 0U;

/* ---------------------------------------------------------------------------
 * Helpers de decodificacion
 * ------------------------------------------------------------------------ */

/*!
 * @brief Decodifica los 8 bytes del frame en un can_rx_joints_t.
 *
 * Layout (coincide con joints_pack_frame en el TX):
 *   byte0       = Hombro (u8)
 *   byte1<<8|2  = Codo   (u16 big-endian)
 *   byte3       = Muneca (u8)
 *
 * @note  El driver fsl_flexcan expone los 8 bytes del payload como
 *        dataByte0..7 en una union con dataWord0/dataWord1. Es seguro
 *        leer byte por byte.
 */
static void can_rx_decode_frame(const flexcan_frame_t *psFrame,
                                can_rx_joints_t *psOut)
{
    psOut->u8Shoulder = psFrame->dataByte0;
    psOut->u16Elbow   = ((uint16_t)psFrame->dataByte1 << 8) |
                         (uint16_t)psFrame->dataByte2;
    psOut->u8Wrist    = psFrame->dataByte3;
}

/* ---------------------------------------------------------------------------
 * Callback del driver FlexCAN
 *
 * El SDK lo declara con la macro FLEXCAN_CALLBACK(name) que expande a
 * `void name(CAN_Type *base, flexcan_handle_t *handle, status_t status,
 *           uint32_t result, void *userData)`.
 *
 * Lo invoca el driver desde la ISR del FlexCAN cuando ocurre una transicion
 * de estado (TxIdle, RxIdle, ErrorStatus, etc.). Aqui solo nos interesa el
 * RxIdle sobre nuestro MB.
 * ------------------------------------------------------------------------ */
static FLEXCAN_CALLBACK(can_rx_callback)
{
    (void)base;
    (void)handle;
    (void)userData;

    switch (status)
    {
        case kStatus_FLEXCAN_RxIdle:
        {
            /* `result` lleva el indice del message buffer que provoco el
             * evento. Solo procesamos si es NUESTRO MB. */
            if (result == CAN_RX_MB_NUM)
            {
                /* Decodifica el frame que el driver acaba de dejar en
                 * g_rxFrame y lo guarda como "ultimo recibido". Aunque
                 * la app no haya consumido el anterior, sobrescribimos:
                 * para un loop de control de protesis interesa siempre
                 * el dato mas fresco, no acumular. */
                can_rx_joints_t sDecoded;
                can_rx_decode_frame(&g_rxFrame, &sDecoded);

                /* Asignacion campo a campo (struct volatile no admite =). */
                g_sLatestJoints.u8Shoulder = sDecoded.u8Shoulder;
                g_sLatestJoints.u16Elbow   = sDecoded.u16Elbow;
                g_sLatestJoints.u8Wrist    = sDecoded.u8Wrist;
                g_u32LatestTickMs          = tick_get_ms();
                g_bFrameAvail              = true;

                g_u32RxOkCount++;

                /* Rearmamos inmediatamente el MB para el siguiente frame.
                 * Sin esto, el periferico recibiria el siguiente frame
                 * pero el driver no haria callback porque el handle no
                 * estaria esperando nada. */
                g_rxXfer.mbIdx = (uint8_t)CAN_RX_MB_NUM;
                g_rxXfer.frame = &g_rxFrame;
                (void)FLEXCAN_TransferReceiveNonBlocking(CAN_RX_PERIPH,
                                                         &g_flexcanHandle,
                                                         &g_rxXfer);
            }
            break;
        }
        case kStatus_FLEXCAN_ErrorStatus:
        {
            /* El bus reporto algo: bit error, stuff error, ACK error,
             * CRC error, form error, etc. En este proyecto basta con
             * contar. Una version mas robusta inspeccionaria `result`
             * que tiene los flags de ESR1 para clasificar. */
            g_u32RxErrCount++;
            break;
        }
        default:
            /* Otros estados (TxIdle, WakeUp) no aplican al RX puro. */
            break;
    }
}

/* ---------------------------------------------------------------------------
 * Configuracion del reloj del periferico FLEXCAN0
 *
 * Esto vive en hardware_init.c del ejemplo SDK (BOARD_InitHardware). Lo
 * replicamos aqui para que el modulo sea autosuficiente.
 * ------------------------------------------------------------------------ */
static void can_rx_init_clock(void)
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
 * Configuracion de los pines CAN0_RXD/TXD en PORT1
 *
 * El ejemplo del SDK hace esto en pin_mux.c via Config Tools. Lo replicamos
 * a mano aqui para que el modulo sea autosuficiente.
 * ------------------------------------------------------------------------ */
static void can_rx_init_pins(void)
{
    /* PORT1 necesita su clock habilitado para poder escribirle a sus
       registros PCR (Pin Control Register). */
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);

    /* Configuracion comun para ambos pines: pull deshabilitado (el
       transceiver maneja la linea), slew rate fast (CAN cambia rapido),
       drive strength low / normal, mux a ALT11 (CAN0_RXD/TXD), input
       buffer enable (para que CAN0_RXD pueda leer).

       port_pin_config_t en MCXA156 tiene 11 campos (uno extra "drive
       strength 1"). Usamos init posicional para evitar nombres de campo
       inciertos entre versiones de SDK. */
    const port_pin_config_t kCanPortCfg =
    {
        kPORT_PullDisable,            /* pullSelect          */
        kPORT_LowPullResistor,        /* pullValueSelect     */
        kPORT_FastSlewRate,           /* slewRate            */
        kPORT_PassiveFilterDisable,   /* passiveFilterEnable */
        kPORT_OpenDrainDisable,       /* openDrainEnable     */
        kPORT_LowDriveStrength,       /* driveStrength0      */
        kPORT_NormalDriveStrength,    /* driveStrength1      */
        CAN_RX_PIN_ALT,               /* mux = ALT11 (CAN0)  */
        kPORT_InputBufferEnable,      /* inputBuffer         */
        kPORT_InputNormal,            /* invertInput         */
        kPORT_UnlockRegister,         /* lockRegister        */
    };

    PORT_SetPinConfig(CAN_RX_PORT, CAN_RX_PIN_RXD, &kCanPortCfg);
    PORT_SetPinConfig(CAN_RX_PORT, CAN_RX_PIN_TXD, &kCanPortCfg);
}

/* ---------------------------------------------------------------------------
 * can_rx_init - Configura el modulo FlexCAN0 en modo receptor
 *
 * Esta es practicamente la misma secuencia que el `main()` del ejemplo
 * `flexcan_interrupt_transfer.c`, pero limitada al lado RX.
 * ------------------------------------------------------------------------ */
void can_rx_init(void)
{
    /* (a) Reloj del periferico (FRO_HF_DIV @ 96 MHz). */
    can_rx_init_clock();

    /* (b) Pin mux de los pines CAN0. */
    can_rx_init_pins();

    /* (1) Llenamos la struct de config con defaults del driver.
     *     FLEXCAN_GetDefaultConfig setea, entre otros:
     *       - clkSrc                = kFLEXCAN_ClkSrc0
     *       - bitRate               = 1000000U  (lo sobrescribimos abajo)
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
    flexcanConfig.bitRate = CAN_RX_BITRATE_BPS;

    /* (3) Pedimos al SDK que calcule los segmentos optimos (propSeg,
     *     phaseSeg1, phaseSeg2, RJW) para nuestro reloj + bitrate. */
    flexcan_timing_config_t timingConfig;
    memset(&timingConfig, 0, sizeof(timingConfig));
    if (FLEXCAN_CalculateImprovedTimingValues(CAN_RX_PERIPH,
                                              flexcanConfig.bitRate,
                                              CAN_RX_CLOCK_FREQ_HZ,
                                              &timingConfig))
    {
        memcpy(&(flexcanConfig.timingConfig),
               &timingConfig,
               sizeof(flexcan_timing_config_t));
    }
    /* Si fallara el calculo (no deberia para 500 kbps con FRO_HF a 96 MHz),
     * el driver se queda con los defaults internos. Nos enteremos por el
     * contador de errores si la baudrate efectiva no fuera la correcta. */

    /* (4) Inicializa el periferico: programa CTRL1, MCR, etc.
     *     El tercer argumento es el reloj del modulo (no del nucleo). */
    FLEXCAN_Init(CAN_RX_PERIPH, &flexcanConfig, CAN_RX_CLOCK_FREQ_HZ);

    /* (5) Registra nuestro callback ISR-driven. NULL en el ultimo
     *     argumento porque no usamos userData (el callback ya tiene
     *     acceso a las globales del modulo). */
    FLEXCAN_TransferCreateHandle(CAN_RX_PERIPH, &g_flexcanHandle,
                                 can_rx_callback, NULL);

    /* (6) Aplica mascara global de RX. FLEXCAN_RX_MB_STD_MASK(id, rtr, ide)
     *     construye la mascara binaria. Pasando 0x7FF (todos los bits de
     *     ID standard) significa "que coincidan TODOS los 11 bits": filtro
     *     exacto. El campo rtr=0 y ide=0 son don't-care con esta mascara
     *     para frames de datos estandar. */
    FLEXCAN_SetRxMbGlobalMask(CAN_RX_PERIPH,
                              FLEXCAN_RX_MB_STD_MASK(0x7FFU, 0U, 0U));

    /* (7) Configura el message buffer 0 como RX estandar de datos con
     *     ID 0x200. Tras esto, el periferico esta listo para capturar
     *     el primer frame que coincida. */
    flexcan_rx_mb_config_t mbConfig;
    mbConfig.format = kFLEXCAN_FrameFormatStandard;
    mbConfig.type   = kFLEXCAN_FrameTypeData;
    mbConfig.id     = FLEXCAN_ID_STD(CAN_RX_FRAME_ID);
    FLEXCAN_SetRxMbConfig(CAN_RX_PERIPH, CAN_RX_MB_NUM, &mbConfig, true);

    /* (8) Arma el primer TransferReceiveNonBlocking. A partir de aqui,
     *     cada frame entrante dispara el callback, que ademas de marcar
     *     la bandera de frame disponible vuelve a llamar a
     *     TransferReceiveNonBlocking para mantener la cola activa. */
    g_rxXfer.mbIdx = (uint8_t)CAN_RX_MB_NUM;
    g_rxXfer.frame = &g_rxFrame;
    (void)FLEXCAN_TransferReceiveNonBlocking(CAN_RX_PERIPH,
                                             &g_flexcanHandle,
                                             &g_rxXfer);
}

/* ---------------------------------------------------------------------------
 * can_rx_try_get - Lee el ultimo frame disponible (si hay)
 * ------------------------------------------------------------------------ */
bool can_rx_try_get(can_rx_joints_t *psJoints, uint32_t *pu32TimestampMs)
{
    if (psJoints == NULL)
    {
        return false;
    }

    /* Lectura+limpieza atomica: deshabilitamos interrupciones globales
     * por un instante para evitar que el callback intervenga entre la
     * lectura de la bandera y la copia del struct. La latencia agregada
     * es de pocos ciclos (sub-us). */
    bool bAvail = false;

    __disable_irq();
    if (g_bFrameAvail)
    {
        psJoints->u8Shoulder = g_sLatestJoints.u8Shoulder;
        psJoints->u16Elbow   = g_sLatestJoints.u16Elbow;
        psJoints->u8Wrist    = g_sLatestJoints.u8Wrist;
        if (pu32TimestampMs != NULL)
        {
            *pu32TimestampMs = g_u32LatestTickMs;
        }
        g_bFrameAvail = false;
        bAvail = true;
    }
    __enable_irq();

    return bAvail;
}

uint32_t can_rx_get_ok_count(void)
{
    return g_u32RxOkCount;
}

uint32_t can_rx_get_err_count(void)
{
    return g_u32RxErrCount;
}
