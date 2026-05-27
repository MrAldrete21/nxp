# rx_mcxa156 — Receptor CAN (FRDM-MCXA156)

Nodo receptor de la red CAN del **Proyecto 2 NXP-UAG**
"Red CAN para Coordinación de Prótesis Robótica Modular".

- **Bus:** CAN clásico, 500 kbps, frame estándar (11-bit ID).
- **ID que escucha:** `0x200`, DLC 8 bytes.
- **Layout del payload:** `byte0`=Hombro (u8 0..180), `byte1<<8 | byte2`=Codo (u16 0..150), `byte3`=Muñeca (u8 0..90), `bytes 4..7`=reservados.
- **Salida:** log verbose por LPUART0 a 115200 8N1 (MCU-Link USB-VCP).
- **Estado actual del firmware:** Paso A — bring-up de CAN completo, sin PWM/CSV/watchdog todavía.

> El transmisor (FRDM-MCXN947, frame `0x200` cada 50 ms) está en otro repo / carpeta. Sin el TX corriendo, el RX seguirá imprimiendo `rx[ok=0 err=...]` y los errores subirán porque ningún nodo está dando ACK al bus.

---

## Cableado CAN

Las dos tarjetas FRDM tienen transceiver **TJA1057 onboard** (no necesita transceiver externo). Conectar:

| MCXA156 (J22)    | MCXN947 (J10)   | Señal |
| :--------------- | :-------------- | :---- |
| pin 2            | pin 1           | CANH  |
| pin 4            | pin 2           | CANL  |
| pin 3            | pin 4           | GND   |

**Atención:** los pinouts de J22 (A156) y J10 (N947) son distintos — no es 1:1.

Una resistencia de terminación de **120 Ω entre CANH y CANL** en cada extremo del bus mejora la integridad de señal, pero para distancias cortas (cable < 30 cm) los TJA1057 suelen tolerar el bus sin terminación.

---

## Pinout del firmware (FRDM-MCXA156)

| Función     | Pin del MCU | Mux   | Header / Componente             |
| :---------- | :---------- | :---- | :------------------------------ |
| CAN0_RXD    | P1_12       | ALT11 | J22 pin 2/4 (vía TJA1057)       |
| CAN0_TXD    | P1_13       | ALT11 | J22 pin 2/4 (vía TJA1057)       |
| LPUART0_RX  | P0_2        | ALT2  | MCU-Link USB-VCP                |
| LPUART0_TX  | P0_3        | ALT2  | MCU-Link USB-VCP                |
| LED Rojo    | P3_12       | ALT0  | RGB onboard (active-low)        |
| LED Verde   | P3_13       | ALT0  | RGB onboard (active-low)        |
| LED Azul    | P3_0        | ALT0  | RGB onboard (active-low)        |

### Significado de los LEDs

| Estado del LED       | Significado                                            |
| :------------------- | :----------------------------------------------------- |
| Azul parpadea 1 Hz   | Firmware vivo (toggle cada 500 ms en lazo de app).     |
| Verde encendido      | Hay frame CAN reciente (< 200 ms desde el último OK).  |
| Verde apagado        | No hay tráfico CAN o se perdió el bus.                 |
| Rojo                 | _Reservado para Paso siguiente (watchdog, fuera-rango)._|

---

## Cómo importar y compilar (otra PC)

Esto asume que tienes **MCUXpresso IDE 25.6.x** + el SDK **frdm-mcxa156 v26.3.0** instalados localmente.

### 1. Clonar este repo

```powershell
git clone <URL_DEL_REPO> rx_mcxa156
cd rx_mcxa156
```

### 2. Instalar el SDK (una vez por máquina)

- En el sitio MCUXpresso SDK Builder de NXP, descarga el paquete **SDK_2.x_FRDM-MCXA156 v26.3.0** (formato P2 site, archivo `.zip`).
- En MCUXpresso IDE → pestaña **"Installed SDKs"** → arrastra y suelta el `.zip` sobre la ventana. El IDE lo registra automáticamente.

### 3. Importar el proyecto base `led_blinky`

1. En MCUXpresso IDE → *File* → *Import…* → *MCUXpresso SDK* → *SDK Import Wizard*.
2. SDK: **frdm-mcxa156** v26.3.0.
3. Marca el ejemplo `led_blinky` (driver_examples → led_blinky) o `led_blinky_peripheral` — cualquiera de las dos sirve, el `main.c` que sobreescribimos detecta `peripherals.h` con `__has_include` y se adapta.
4. *Project name*: `rx_mcxa156`.
5. *Location*: la **carpeta vacía** dentro del repo donde se guardará el proyecto IDE (por ejemplo `rx_mcxa156/rx/`). **NO uses la raíz `rx_mcxa156/` directamente** o el IDE tratará de sobreescribir `source/` y `README.md`.
6. *Finish*. El IDE genera la carpeta del proyecto con `source/led_blinky.c` y compila.

### 4. Reemplazar `source/` con los módulos del repo

1. En el explorador de proyecto, borra el archivo `source/led_blinky.c` que generó el SDK Wizard.
2. Copia (o crea como link) todos los archivos de `<repo>/source/` dentro de la carpeta `source/` del proyecto importado:
   - `main.c`
   - `app.c`, `app.h`
   - `tick.c`, `tick.h`
   - `uart_dbg.c`, `uart_dbg.h`
   - `gpio_io.c`, `gpio_io.h`
   - `can_rx.c`, `can_rx.h`
3. Refresca el proyecto (F5).

> **Tip Windows:** si prefieres no duplicar archivos, en MCUXpresso IDE: clic derecho en `source/` → *New* → *File* → *Advanced >> Link to file in the file system* y apunta a cada archivo del repo.

### 5. Verificar que la build agrega los drivers necesarios

`led_blinky` ya trae `fsl_gpio`, `fsl_port`, `fsl_clock`, `fsl_reset` y `fsl_common`. Adicionalmente este firmware necesita:

- **`fsl_lpuart`** (para `uart_dbg`)
- **`fsl_flexcan`** (para `can_rx`)

Para agregarlos al proyecto:

1. Clic derecho en el proyecto → *Manage SDK Components*.
2. En la pestaña "Drivers", marca **lpuart** y **flexcan**.
3. Aplica. El IDE copia `drivers/fsl_lpuart.{h,c}` y `drivers/fsl_flexcan.{h,c}` dentro del proyecto.

### 6. Compilar y flashear

1. Clic en el martillo (Build). Debe compilar sin warnings.
2. Conecta el MCU-Link de la tarjeta vía USB. Aparece como **MCU-LINK CMSIS-DAP** + puerto VCP.
3. Clic en la flecha verde (Debug). MCUXpresso pregunta interfaz: elige **CMSIS-DAP**, target `MCXA156`. Acepta los defaults.
4. La sesión de debug arranca y para en `main()`. Presiona "Resume" (F8).

### 7. Abrir el log por UART

- Identifica qué `COMx` quedó asignado al MCU-Link de la A156 (Administrador de Dispositivos → *Puertos COM y LPT* → "MCU-Link CMSIS-DAP Vcom Port").
- Abre cualquier terminal serial (Tera Term, PuTTY, MCUXpresso Terminal View, Tabby, etc.) en **`COMx` @ 115200, 8N1, sin control de flujo**.
- Deberías ver:
  ```
  =======================================
   RX  FRDM-MCXA156  -  Protesis Robotica
  =======================================
  [INIT] modulos OK
         FlexCAN0 @ 500 kbps, ID 0x200, DLC 8, RX MB0
  ---------------------------------------
  RX | (sin frame todavia)  rx[ok=    0 err=  0]
  ```
- Cuando enchufes y enciendas el TX (MCXN947) y haya cable CAN entre las dos tarjetas, las líneas pasan a:
  ```
  RX | S= 90  E= 75  W= 45  age=  47 ms  rx[ok=   23 err=  0]
  ```

---

## Estructura del firmware

```
source/
├── main.c          Entry-point. BOARD_InitBoot* + appMain().
├── app.{c,h}       Scheduler cooperativo. Polling de CAN + log + LEDs.
├── tick.{c,h}      SysTick @ 1 ms. tick_get_ms() es el reloj del proyecto.
├── uart_dbg.{c,h}  LPUART0 init directa (sin DbgConsole/newlib). 115200 8N1.
├── gpio_io.{c,h}   LEDs RGB onboard. PORT3/GPIO3 + reset release.
└── can_rx.{c,h}    FlexCAN0 receptor. ISR-driven, MB0, filtro ID exacto.
```

Convenciones de código (estilo UAG):

- Hungarian camelCase para variables (`u8Shoulder`, `bFrameAvail`, `pcStr`).
- `SCREAMING_SNAKE_CASE` para `#define`.
- Llaves estilo Allman.
- Comentarios en español. Sin `printf` de stdlib (sólo `snprintf` a buffer + `LPUART_WriteBlocking`).
- Cada módulo es autosuficiente: hace su propio pin-mux + clock-gate + reset-release, NO depende de regenerar `pin_mux.c` con MCUXpresso Config Tools.

---

## Roadmap (qué falta)

El firmware actual cubre solo el **Paso A** del receptor (CAN end-to-end + visibilidad mínima). Pendiente para sesiones siguientes:

- **Paso B (PWM):** FLEXPWM0 SM0/SM1/SM2 canal A en P3_6/P3_8/P3_10. Mapeo ángulo → duty 5–10 % (servo) o 10–100 % (LED de demostración).
- **Paso C (watchdog SW):** si pasan más de 200 ms sin frame válido → entra a "modo seguro", LED rojo intermitente, PWM en estado neutro.
- **Paso D (uart_csv):** salida CSV `S,E,W\r\n` cada 50 ms por la misma LPUART0 hacia el viewer 3D de PC (`pc_viewer/viewer.py`).

---

## Troubleshooting

| Síntoma                                          | Causa probable                                                    |
| :----------------------------------------------- | :---------------------------------------------------------------- |
| LED azul no parpadea                             | El firmware no llegó a `appMain()`. Revisar consola del debugger. |
| Log se ve "encriptado" / símbolos raros          | Baudrate del terminal ≠ 115200. Ajustar a 115200 8N1.             |
| `rx[ok=0 err=0]` indefinidamente                 | El TX no está corriendo, o el cable CAN está abierto.             |
| `rx[ok=0 err=N]` con N subiendo                  | Hay tráfico pero el ID no coincide, o baudrate del TX ≠ 500 kbps. |
| `rx[ok=0 err=N]` enorme aún con TX corriendo     | Posible inversión CANH/CANL en el cable, o falta GND común.        |
| `age` siempre crece y nunca decrece              | El callback no se está disparando — revisar NVIC, hardware bus.   |
