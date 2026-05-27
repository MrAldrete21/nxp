/*
 * buttons.c - Implementacion del modulo de botones para FRDM-MCXA156.
 *
 * Variante de polling del ejemplo `gpio_input_interrupt` del SDK:
 *  - El ejemplo configura el pin del switch como interrupcion (kPORT_InterruptFallingEdge)
 *    y atiende en ISR. Nosotros muestreamos por polling cada 5 ms desde la
 *    aplicacion. Razones para no usar IRQ:
 *      1) El debounce mecanico exige esperar despues de cada flanco;
 *         con IRQ habria que armar un timer para el debounce, mas complejo.
 *      2) Necesitamos medir tiempo de presion (long-press), lo que requiere
 *         consultar el tick desde el modulo botones - un loop periodico es
 *         mas natural para eso.
 *      3) Los botones son humanos (decenas de Hz), no necesitan latencia
 *         de microsegundos.
 *
 * Pinout (FRDM-MCXA156, distinto del MCXN947):
 *   SW2 = P1_7 -> GPIO1 pin 7
 *   SW3 = P0_6 -> GPIO0 pin 6
 *
 * Los dos pines viven en bloques distintos de PORT/GPIO, asi que hay que
 * habilitar y sacar de reset ambos.
 *
 * APIs del SDK reutilizadas:
 *  - PORT_SetPinConfig (de fsl_port.h) para configurar pull-up + GPIO mux.
 *  - GPIO_PinInit (de fsl_gpio.h) para declarar el pin como input.
 *  - GPIO_PinRead para muestrear el nivel.
 */

#include "buttons.h"

#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"
#include "fsl_reset.h"

/* ---------------------------------------------------------------------------
 * Constantes de hardware (FRDM-MCXA156)
 * ------------------------------------------------------------------------ */
/* SW2: P1_7, "SW2" en silkscreen. */
#define BTN_SW2_PORT            PORT1
#define BTN_SW2_GPIO            GPIO1
#define BTN_SW2_PIN             (7U)

/* SW3: P0_6, "SW3" en silkscreen. */
#define BTN_SW3_PORT            PORT0
#define BTN_SW3_GPIO            GPIO0
#define BTN_SW3_PIN             (6U)

/* ---------------------------------------------------------------------------
 * Constantes de timing
 * ------------------------------------------------------------------------ */

/* Cantidad de muestras consecutivas con el mismo nivel para aceptar
 * transicion (debounce). Con scan rate 5 ms -> 4 muestras = 20 ms estables. */
#define BTN_DEBOUNCE_SAMPLES    (4U)

/* Umbral de long-press en milisegundos. */
#define BTN_LONG_PRESS_MS       (600U)

/* ---------------------------------------------------------------------------
 * State machine por boton
 * ------------------------------------------------------------------------ */
typedef struct
{
    /* Nivel "estable" del boton actualmente reconocido (true = presionado). */
    bool     bPressed;
    /* Contador de muestras consecutivas del nivel opuesto al estable.
     * Cuando llega a BTN_DEBOUNCE_SAMPLES, conmutamos bPressed. */
    uint8_t  u8DebounceCount;
    /* Timestamp (ms) cuando inicio la presion actual; 0 si no esta presionado. */
    uint32_t u32PressedAtMs;
    /* Bandera "ya emitimos el long-press para esta presion" - evita que se
     * dispare repetidamente cada scan despues del umbral. */
    bool     bLongAlreadyFired;
} btn_state_t;

static btn_state_t g_sw2;
static btn_state_t g_sw3;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/* Lee el pin del SW2 (GPIO1) y devuelve true si esta presionado (active-low). */
static inline bool btn_read_sw2_raw(void)
{
    return (GPIO_PinRead(BTN_SW2_GPIO, BTN_SW2_PIN) == 0U);
}

/* Lee el pin del SW3 (GPIO0) y devuelve true si esta presionado (active-low). */
static inline bool btn_read_sw3_raw(void)
{
    return (GPIO_PinRead(BTN_SW3_GPIO, BTN_SW3_PIN) == 0U);
}

/* Avanza el state machine de debounce para UN boton. Devuelve si hubo
 * un flanco "release" (release_event=true) o "press" (press_event=true).
 *
 * Esto es la implementacion clasica del debounce por contador: cuando vemos
 * un nivel distinto al estable, contamos hasta N; al llegar a N adoptamos
 * el nuevo nivel. Cualquier muestra contraria resetea el contador. */
static void btn_debounce_update(btn_state_t *pSt, bool bRawPressed,
                                bool *pPressEvent, bool *pReleaseEvent)
{
    *pPressEvent   = false;
    *pReleaseEvent = false;

    if (bRawPressed == pSt->bPressed)
    {
        /* Sin cambio respecto al estado estable: reseteamos contador. */
        pSt->u8DebounceCount = 0U;
        return;
    }

    /* Vemos nivel distinto al estable: incrementamos hasta alcanzar el umbral. */
    if (pSt->u8DebounceCount < BTN_DEBOUNCE_SAMPLES)
    {
        pSt->u8DebounceCount++;
    }

    if (pSt->u8DebounceCount >= BTN_DEBOUNCE_SAMPLES)
    {
        /* Confirmado: el boton cambio de nivel estable. */
        pSt->bPressed        = bRawPressed;
        pSt->u8DebounceCount = 0U;

        if (bRawPressed) { *pPressEvent   = true; }
        else             { *pReleaseEvent = true; }
    }
}

/* ---------------------------------------------------------------------------
 * Inicializacion
 * ------------------------------------------------------------------------ */
void buttons_init(void)
{
    /* Habilitar reloj de ambos PORT y GPIO (en MCXA156 los kCLOCK_Gate*
     * llevan prefijo "Gate"). */
    CLOCK_EnableClock(kCLOCK_GatePORT0);
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    CLOCK_EnableClock(kCLOCK_GateGPIO0);
    CLOCK_EnableClock(kCLOCK_GateGPIO1);

    /* Sacar PORT0, PORT1, GPIO0 y GPIO1 de reset. */
    RESET_ReleasePeripheralReset(kPORT0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);

    /* Configuracion comun de los dos pines: GPIO (ALT0) con pull-up interno.
     * Pull-up interno porque la FRDM-MCXA156 no tiene resistencias externas
     * a los SW (la spec del board UM12012 confia en el pull-up interno).
     *
     * port_pin_config_t en MCXA156 tiene 11 campos (uno extra de "drive
     * strength 1" entre driveStrength y mux). Usamos init posicional. */
    const port_pin_config_t kBtnPortCfg =
    {
        kPORT_PullUp,                 /* pullSelect          */
        kPORT_LowPullResistor,        /* pullValueSelect     */
        kPORT_FastSlewRate,           /* slewRate            */
        kPORT_PassiveFilterDisable,   /* passiveFilterEnable */
        kPORT_OpenDrainDisable,       /* openDrainEnable     */
        kPORT_LowDriveStrength,       /* driveStrength0      */
        kPORT_NormalDriveStrength,    /* driveStrength1      */
        kPORT_MuxAlt0,                /* mux = GPIO          */
        kPORT_InputBufferEnable,      /* inputBuffer         */
        kPORT_InputNormal,            /* invertInput         */
        kPORT_UnlockRegister,         /* lockRegister        */
    };
    PORT_SetPinConfig(BTN_SW2_PORT, BTN_SW2_PIN, &kBtnPortCfg);
    PORT_SetPinConfig(BTN_SW3_PORT, BTN_SW3_PIN, &kBtnPortCfg);

    /* Declaramos los pines como input digital. outputLogic se ignora cuando
     * la direccion es input, pero el campo es obligatorio. */
    const gpio_pin_config_t kInCfg =
    {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic  = 0U,
    };
    GPIO_PinInit(BTN_SW2_GPIO, BTN_SW2_PIN, &kInCfg);
    GPIO_PinInit(BTN_SW3_GPIO, BTN_SW3_PIN, &kInCfg);

    /* Limpiar state machines. */
    g_sw2 = (btn_state_t){ 0 };
    g_sw3 = (btn_state_t){ 0 };
}

/* ---------------------------------------------------------------------------
 * Scan periodico
 * ------------------------------------------------------------------------ */
button_event_t buttons_scan(uint32_t u32NowMs)
{
    /* (1) Muestrear ambos pines y avanzar debounce. */
    bool bSw2Raw = btn_read_sw2_raw();
    bool bSw3Raw = btn_read_sw3_raw();

    bool bSw2Press, bSw2Release;
    bool bSw3Press, bSw3Release;
    btn_debounce_update(&g_sw2, bSw2Raw, &bSw2Press, &bSw2Release);
    btn_debounce_update(&g_sw3, bSw3Raw, &bSw3Press, &bSw3Release);

    /* (2) Actualizar timestamp y bandera long-fired en cada flanco confirmado. */
    if (bSw2Press)
    {
        g_sw2.u32PressedAtMs    = u32NowMs;
        g_sw2.bLongAlreadyFired = false;
    }
    if (bSw3Press)
    {
        g_sw3.u32PressedAtMs    = u32NowMs;
        g_sw3.bLongAlreadyFired = false;
    }

    /* (3) Detectar long-press en SW3: el boton sigue presionado y supero
     *     el umbral. Lo disparamos UNA sola vez por presion (de ahi la
     *     bandera bLongAlreadyFired). */
    if (g_sw3.bPressed && !g_sw3.bLongAlreadyFired)
    {
        if ((u32NowMs - g_sw3.u32PressedAtMs) >= BTN_LONG_PRESS_MS)
        {
            g_sw3.bLongAlreadyFired = true;
            return BUTTON_EVENT_SW3_LONG;
        }
    }

    /* (4) Releases: si el boton SE SOLTO, decidimos si fue short.
     *     Para SW3: solo emitimos SHORT si el long aun no se habia disparado
     *     (i.e. el usuario apenas dio un click rapido, no un long-hold). */
    if (bSw3Release)
    {
        if (!g_sw3.bLongAlreadyFired)
        {
            return BUTTON_EVENT_SW3_SHORT;
        }
        /* Si ya disparamos long, ignoramos el release: el long-press ya cumplio. */
    }

    /* SW2 solo tiene short-click (no usamos long en SW2). */
    if (bSw2Release)
    {
        return BUTTON_EVENT_SW2_SHORT;
    }

    /* (5) No hubo eventos este scan. */
    return BUTTON_EVENT_NONE;
}
