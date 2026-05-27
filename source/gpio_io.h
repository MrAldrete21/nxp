/*
 * gpio_io.h - Control de los LEDs onboard (RGB) del RX (FRDM-MCXA156).
 *
 * LEDs del FRDM-MCXA156 (todos active-low, anodo a 3V3 con resistencia de
 * 470 ohm; escribir 0 enciende, 1 apaga):
 *   - Rojo  -> P3_12   (GPIO3, pin 12)
 *   - Verde -> P3_13   (GPIO3, pin 13)
 *   - Azul  -> P3_0    (GPIO3, pin 0)
 *
 * Asignacion de significado para el RX (segun especificacion del proyecto):
 *   - Azul   = latido del firmware (toggle cada 500 ms en lazo de app).
 *   - Verde  = recibo de frames CAN OK (encendido si hay frame reciente).
 *   - Rojo   = sin frames / error / watchdog (modo seguro).
 *
 * En Paso A solo se usan azul (latido) y verde (frame OK). El rojo se
 * agregara cuando se implemente el watchdog SW (Paso siguiente).
 */

#ifndef GPIO_IO_H_
#define GPIO_IO_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Inicializa los 3 LEDs como salida digital, valor inicial 1
 *        (apagado, porque son active-low).
 *
 * Tambien habilita el reloj de PORT3/GPIO3 y los saca de reset, ya que el
 * pin_mux.c base del led_blinky no configura los pines de los LEDs del RX
 * (configura solo el rojo en la variante del SDK). Para que el modulo
 * sea autosuficiente, hacemos toda la configuracion aqui.
 */
void gpio_io_init(void);

/*!
 * @brief Cambia el estado del LED azul (toggle). Usado como heartbeat.
 */
void gpio_io_toggle_blue(void);

/*!
 * @brief Enciende o apaga cada LED individualmente.
 *        bOn = true -> enciende (escribe 0); false -> apaga (escribe 1).
 */
void gpio_io_set_red(bool bOn);
void gpio_io_set_green(bool bOn);
void gpio_io_set_blue(bool bOn);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_IO_H_ */
