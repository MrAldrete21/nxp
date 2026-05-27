/*
 * gpio_io.c - Implementacion del control de LEDs del FRDM-MCXA156.
 *
 * En MCXA156 los LEDs onboard son active-low contra 3V3:
 *   - Rojo  -> P3_12  (GPIO3, pin 12)
 *   - Verde -> P3_13  (GPIO3, pin 13)
 *   - Azul  -> P3_0   (GPIO3, pin 0)
 *
 * Los macros BOARD_LED_*_GPIO / BOARD_LED_*_GPIO_PIN viven en board.h del
 * proyecto base; aqui los usamos en vez de hardcodear el numero de pin
 * para que el modulo siga funcionando si el SDK actualiza el nombre.
 *
 * IMPORTANTE: a diferencia del TX (donde BOARD_InitBootPins ya configura
 * todo el pin mux de los LEDs), en el RX el `led_blinky` base SOLO
 * configura el LED rojo. Por eso aqui replicamos manualmente el pin mux
 * para los 3 LEDs (PORT3 + GPIO3, ALT0).
 */

#include "gpio_io.h"

#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
#include "board.h"

/* Valor logico segun "encendido" o "apagado" (LEDs active-low). */
#define LED_LOGIC_ON     (0U)
#define LED_LOGIC_OFF    (1U)

/*!
 * @brief Habilita reloj del PORT3 y GPIO3, los saca de reset, y configura
 *        los 3 pines fisicos (P3_12, P3_13, P3_0) como GPIO digital (ALT0).
 *
 * El RX usa exclusivamente PORT3 para los LEDs RGB onboard. El TX (N947)
 * por contraste tiene los LEDs en PORT0/PORT1.
 */
static void gpio_io_init_pins(void)
{
    /* Habilitar el clock del bloque PORT3 (necesario para escribir sus PCR)
       y del bloque GPIO3 (necesario para escribir sus registros de direccion
       y de datos). En MCXA156 los kCLOCK_Gate*** llevan prefijo "Gate". */
    CLOCK_EnableClock(kCLOCK_GatePORT3);
    CLOCK_EnableClock(kCLOCK_GateGPIO3);

    /* Sacar PORT3 y GPIO3 del reset. ReleasePeripheralReset libera al
       periferico que esta en reset por default al boot. */
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);

    /* Configuracion fisica de cada pin: ALT0 (GPIO), input buffer habilitado,
       slew normal, sin pull. Estructura port_pin_config_t en MCXA156 tiene
       11 campos (uno extra de "drive strength 1" respecto al MCXN947); por
       eso usamos init posicional siguiendo el patron del SDK pin_mux.c. */
    const port_pin_config_t kLedPortCfg =
    {
        kPORT_PullDisable,            /* pullSelect          */
        kPORT_LowPullResistor,        /* pullValueSelect     */
        kPORT_FastSlewRate,           /* slewRate            */
        kPORT_PassiveFilterDisable,   /* passiveFilterEnable */
        kPORT_OpenDrainDisable,       /* openDrainEnable     */
        kPORT_LowDriveStrength,       /* driveStrength0      */
        kPORT_NormalDriveStrength,    /* driveStrength1 (MCXA156: campo extra) */
        kPORT_MuxAlt0,                /* mux = GPIO          */
        kPORT_InputBufferEnable,      /* inputBuffer         */
        kPORT_InputNormal,            /* invertInput         */
        kPORT_UnlockRegister,         /* lockRegister        */
    };

    PORT_SetPinConfig(PORT3, BOARD_LED_RED_GPIO_PIN,   &kLedPortCfg);
    PORT_SetPinConfig(PORT3, BOARD_LED_GREEN_GPIO_PIN, &kLedPortCfg);
    PORT_SetPinConfig(PORT3, BOARD_LED_BLUE_GPIO_PIN,  &kLedPortCfg);
}

void gpio_io_init(void)
{
    /* (1) Pin mux + clock gates + reset release. */
    gpio_io_init_pins();

    /* (2) Configuracion logica como salidas digitales apagadas. */
    const gpio_pin_config_t kOutOffCfg =
    {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic  = LED_LOGIC_OFF,
    };

    GPIO_PinInit(BOARD_LED_RED_GPIO,   BOARD_LED_RED_GPIO_PIN,   &kOutOffCfg);
    GPIO_PinInit(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN, &kOutOffCfg);
    GPIO_PinInit(BOARD_LED_BLUE_GPIO,  BOARD_LED_BLUE_GPIO_PIN,  &kOutOffCfg);
}

void gpio_io_toggle_blue(void)
{
    GPIO_PortToggle(BOARD_LED_BLUE_GPIO, 1UL << BOARD_LED_BLUE_GPIO_PIN);
}

void gpio_io_set_red(bool bOn)
{
    GPIO_PinWrite(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN,
                  bOn ? LED_LOGIC_ON : LED_LOGIC_OFF);
}

void gpio_io_set_green(bool bOn)
{
    GPIO_PinWrite(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN,
                  bOn ? LED_LOGIC_ON : LED_LOGIC_OFF);
}

void gpio_io_set_blue(bool bOn)
{
    GPIO_PinWrite(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_GPIO_PIN,
                  bOn ? LED_LOGIC_ON : LED_LOGIC_OFF);
}
