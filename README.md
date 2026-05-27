# nxp — Transmisor CAN (FRDM-MCXA156)

Nodo **transmisor** de la red CAN del **Proyecto 2 NXP-UAG**
"Red CAN para Coordinación de Prótesis Robótica Modular".

> **Cambio de rol:** En esta iteración la MCXA156 es **TRANSMISOR** (lee SW2/SW3, manda el frame CAN). La MCXN947 es **RECEPTOR** (decodifica y emite CSV al viewer 3D PyVista).

## Topología del sistema

```
   [FRDM-MCXA156]                CAN 500 kbps            [FRDM-MCXN947]                 USB-VCP            [Viewer PyVista]
   botones SW2/SW3 ─── J22 ◀────────────────────────▶ J10 ───── recibe ─────────────────────────────────▶  brazo 3D
   transmite                                                    decodifica
   frame 0x200                                                  emite CSV "S,E,W\r\n"
   cada 50 ms                                                   cada 50 ms
```

- **Bus:** CAN clásico, 500 kbps, frame estándar (11-bit ID).
- **ID que transmite:** `0x200`, DLC 8 bytes.
- **Layout del payload:** `byte0`=Hombro (u8 0..180), `byte1<<8 | byte2`=Codo (u16 0..150), `byte3`=Muñeca (u8 0..90), `bytes 4..7`=reservados.
- **Salida visible:** log verbose por LPUART0 a 115200 8N1 (MCU-Link USB-VCP).

## Botones

| Acción                   | Efecto                                                         |
| :----------------------- | :------------------------------------------------------------- |
| SW2 click                | Ciclar selección: Hombro → Codo → Muñeca → Hombro → …          |
| SW3 click corto          | **Contraer** (+5°, más flexión) en la articulación seleccionada |
| SW3 long-press (≥600 ms) | **Retraer**  (−5°, más extensión) en la articulación seleccionada |

---

## Cableado CAN

Las dos tarjetas FRDM tienen transceiver **TJA1057 onboard** (no necesita transceiver externo). Conectar:

| MCXA156 (J22)    | MCXN947 (J10)   | Señal |
| :--------------- | :-------------- | :---- |
| pin 2            | pin 1           | CANH  |
| pin 4            | pin 2           | CANL  |
| pin 3            | pin 4           | GND   |

**Atención:** los pinouts de J22 (A156) y J10 (N947) son distintos — no es 1:1.

Una resistencia de **120 Ω entre CANH y CANL** en cada extremo del bus mejora la integridad de señal, pero para distancias cortas (< 30 cm) los TJA1057 suelen tolerar el bus sin terminación.

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
| SW2         | P1_7        | ALT0  | botón onboard "+"               |
| SW3         | P0_6        | ALT0  | botón onboard "−" / long-ciclar |

### Significado de los LEDs

| Estado del LED       | Significado                                            |
| :------------------- | :----------------------------------------------------- |
| Azul parpadea 1 Hz   | Firmware vivo (heartbeat cada 500 ms).                 |

---

## Cómo importar y compilar (otra PC)

Asume **MCUXpresso IDE 25.6.x** + SDK **frdm-mcxa156 v26.3.0** instalados.

### 1. Clonar este repo

```powershell
git clone https://github.com/MrAldrete21/nxp.git brazo
cd brazo
```

### Si ya tenías el repo clonado de antes (cambio de rol RX → TX)

```powershell
cd brazo
git pull
```

Verás archivos nuevos (`buttons.c/h`, `joints.c/h`, `can_tx.c/h`) y archivos eliminados (`can_rx.c/h`). En MCUXpresso IDE: clic derecho en `source/` → **Delete** los `can_rx.c/h` antiguos del proyecto IDE, copia los nuevos `*.c/*.h` del repo al `source/` del proyecto IDE, presiona **F5** (Refresh), y rebuild.

### 2. Instalar SDK FRDM-MCXA156 v26.3.0

En MCUXpresso IDE → pestaña **"Installed SDKs"** → arrastrar el `.zip` del SDK O usar **"Install MCUXpresso SDKs"** del Welcome para descargarlo del catálogo NXP.

### 3. Importar `led_blinky` desde el SDK

Quickstart Panel → **"Import SDK example(s)…"** → board `frdmmcxa156` → `driver_examples/led_blinky` (o `led_blinky_peripheral`) → *Finish*.

### 4. Reemplazar `source/` con los archivos del repo

En MCUXpresso IDE: borrar `source/led_blinky.c` (clic derecho → Delete) y copiar al `source/` del proyecto IDE TODOS los archivos `.c/.h` de la carpeta `source/` del repo clonado:

```powershell
Copy-Item -Path "C:\Users\<usuario>\Desktop\brazo\source\*" `
          -Destination "<workspace>\<proyecto_IDE>\source\" -Force
```

(Reemplaza `<usuario>`, `<workspace>`, `<proyecto_IDE>` por tus rutas reales.) Después F5 en el IDE para refrescar.

### 5. Agregar driver `fsl_flexcan` al proyecto

Clic derecho en el proyecto → **Manage SDK Components** → pestaña Drivers → marcar **flexcan** → OK. El IDE copia `fsl_flexcan.{h,c}` a `drivers/`.

### 6. Build + Flash + Debug

Martillo verde → Bichito verde → seleccionar **MCU-LINK (CMSIS-DAP) Probe** → OK → F8 (Resume).

### 7. Verificación por terminal serial

Abre PuTTY u otro terminal serial en `COMx` (el del MCU-Link de la A156), **115200 8N1**. Debes ver:

```
=======================================
 TX  FRDM-MCXA156  -  Protesis Robotica
=======================================
[INIT] modulos OK
       FlexCAN0 @ 500 kbps, ID 0x200, DLC 8, periodo 50 ms
       UI: SW2=+5  SW3=-5  SW3-long=ciclar articulacion
---------------------------------------
TX | S= 90  E= 75  W= 45  sel=Hombro  can[ok= 19 err=  0 drop=  0]
```

- Aprieta **SW2** y debe imprimir `[BTN] SW2 short -> +5 deg` y el campo `S` debe subir a 95.
- Aprieta **SW3** corto y el campo `S` baja a 85.
- Aprieta **SW3** largo (≥600 ms) y debe imprimir `[BTN] SW3 LONG -> sel=Codo`. Ahora SW2/SW3 modificarán `E`.
- Si `can[err]` sube y `can[ok]` no, no hay receptor en el bus o el cable está mal.

---

## Estructura del firmware

```
source/
├── main.c          Entry-point. BOARD_InitBoot* + appMain().
├── app.{c,h}       Scheduler cooperativo. Botones + CAN tx + log.
├── tick.{c,h}      SysTick @ 1 ms. tick_get_ms() es el reloj del proyecto.
├── uart_dbg.{c,h}  LPUART0 init directa (sin DbgConsole/newlib). 115200 8N1.
├── gpio_io.{c,h}   LEDs RGB onboard. PORT3/GPIO3 + reset release.
├── buttons.{c,h}   SW2 (GPIO1.7) + SW3 (GPIO0.6) con debounce + long-press.
├── joints.{c,h}    Estado de las 3 articulaciones + frame packing (C puro).
└── can_tx.{c,h}    FlexCAN0 transmisor ISR-driven, MB1, ID 0x200, 500 kbps.
```

Convenciones de código (estilo UAG):

- Hungarian camelCase para variables (`u8Shoulder`, `bFrameAvail`, `pcStr`).
- `SCREAMING_SNAKE_CASE` para `#define`.
- Llaves estilo Allman.
- Comentarios en español. Sin `printf` de stdlib (sólo `snprintf` a buffer + `LPUART_WriteBlocking`).
- Cada módulo es autosuficiente: hace su propio pin-mux + clock-gate + reset-release, NO depende de regenerar `pin_mux.c` con MCUXpresso Config Tools.

---

## Nodo receptor (FRDM-MCXN947)

El receptor vive en **otro proyecto** (no en este repo). Hace lo opuesto:

1. Recibe el frame `0x200` del bus CAN (FlexCAN0 RX en P1_10/P1_11 ALT11).
2. Decodifica `(S, E, W)` de los 8 bytes.
3. Emite `"%u,%u,%u\r\n"` por **LPUART4** (USB-VCP del MCU-Link de la N947) a 115200 8N1, cada 50 ms.

El viewer 3D `pc_viewer/viewer.py` (PyVista) abre el COM virtual de la N947, parsea las líneas CSV, y anima un brazo antropomórfico en pantalla.

```powershell
# En la PC donde está el USB del MCU-Link de la N947:
cd pc_viewer
.\run.ps1            # autodetecta el COM
# o bien
.\run.ps1 --port COM7
```

---

## Roadmap

- [x] **Paso A:** Bring-up CAN (RX en 156). Validado en hardware.
- [x] **Inversión de roles:** 156 pasa a TX, 947 pasa a RX + CSV al viewer.
- [ ] **Paso C:** Watchdog SW (200 ms sin frame → modo seguro, LED rojo intermitente).
- [ ] **Paso PWM:** opcional — agregar FLEXPWM al RX para mover servos físicos o LEDs externos.

---

## Troubleshooting

| Síntoma                                              | Causa probable                                                    |
| :--------------------------------------------------- | :---------------------------------------------------------------- |
| LED azul no parpadea                                 | El firmware no llegó a `appMain()`. Revisar consola del debugger. |
| Log se ve "encriptado" / símbolos raros              | Baudrate del terminal ≠ 115200. Ajustar a 115200 8N1.             |
| `can[err]` sube y `can[ok]=0`                        | No hay receptor en el bus (faltan ACKs) o cable mal conectado.    |
| Botón no responde                                    | Mal contacto o `buttons_init()` no se llamó. Reset y reintentar.  |
| `can[ok]` sube pero el viewer no se mueve            | Revisar el receptor (947) y el COM al que apunta el viewer.       |
| `can[drop]` sube                                     | El TX previo no terminó (callback no llegó). Bus muy ocupado o ACK error. |
