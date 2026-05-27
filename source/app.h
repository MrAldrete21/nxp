/*
 * app.h - Punto de entrada de la capa de aplicacion del RX (FRDM-MCXA156).
 *
 * Proyecto 2 NXP-UAG. Paso A (bring-up de CAN): receptor 0x200 + log
 * verbose por LPUART0. PWM/CSV/watchdog se agregan en pasos siguientes.
 */

#ifndef APP_H_
#define APP_H_

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Inicializa los modulos del firmware del RX y entra al lazo
 *        principal. No regresa nunca.
 */
void appMain(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H_ */
