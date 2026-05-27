/*
 * app.h - Punto de entrada de la capa de aplicacion del TX (FRDM-MCXA156).
 *
 * Proyecto 2 NXP-UAG. La MCXA156 actua como TRANSMISOR de la red CAN:
 * lee SW2/SW3, mantiene el estado de las 3 articulaciones, y empaqueta
 * un frame estandar ID 0x200 (DLC 8) cada 50 ms. El receptor (MCXN947)
 * decodifica y emite CSV al viewer 3D PyVista.
 */

#ifndef APP_H_
#define APP_H_

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Inicializa los modulos del firmware del TX y entra al lazo
 *        principal. No regresa nunca.
 */
void appMain(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H_ */
