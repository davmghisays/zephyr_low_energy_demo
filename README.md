# Demo real de energia con Zephyr, NUCLEO-WL55JC1 y KY-018

La aplicacion mide luz por `A0/PB1/ADC1_IN5` y compara tres politicas de
energia sin BLE, LoRa ni ninguna transmision. Los tres binarios de medida hacen
exactamente ocho conversiones ADC cada 5000 ms; solo cambia la restriccion que
la aplicacion entrega a la politica PM de Zephyr.

Entorno comprobado para este proyecto:

- Zephyr `v4.4.2` y west `1.5.0`.
- Board Zephyr: `nucleo_wl55jc` (valido para la NUCLEO-WL55JC1).
- Toolchain: Zephyr SDK `1.0.1`.
- CPU0: STM32WL55 Cortex-M4.
- Estados declarados: `suspend-to-idle`, subestados 1, 2 y 3. El driver de
  esta version los implementa respectivamente como STOP0, STOP1 y STOP2.

Validacion realizada en este equipo: compilan `diagnostic`, los tres perfiles
`verify-*`, los tres perfiles silenciosos y `saving-fast`. Durante esta
preparacion no habia ningun ST-LINK conectado, por lo que las lecturas ADC y
las corrientes quedan deliberadamente como pruebas fisicas pendientes; no se
han inventado resultados de hardware.

`CMakeLists.txt` aplica en Windows `-fno-use-linker-plugin`: en esta instalacion
`ld.bfd` no puede cargar `liblto_plugin.dll`. La demo no usa LTO, por lo que el
workaround no cambia el firmware generado; solo permite enlazarlo.

## 1. Identificacion y cableado del modulo

La placa de la foto no debe identificarse por `HW-483 v0.2`: esa referencia se
usa tambien para otros modulos. Lo que se observa realmente es una LDR, una
resistencia `103` (10 kohm) y ningun comparador ni potenciometro. Es, por tanto,
un divisor resistivo KY-018 con salida **analogica**. No hacen falta componentes
externos adicionales.

Con los componentes hacia arriba, los tres pines a la izquierda y el texto
`HW-483 v0.2` legible como en la foto, el orden de arriba abajo es:

```text
-    GND
+    3V3
S    señal analogica
```

Como existen copias con serigrafia equivocada, haga esta comprobacion con la
placa sin alimentar antes de conectarla a la Nucleo:

1. Entre `S` y `-` deben aparecer aproximadamente 10 kohm, sin variar apenas
   al tapar la LDR.
2. Entre `S` y `+` la resistencia debe cambiar mucho al iluminar/tapar.
3. Si no ocurre asi, no conecte el modulo y determine los nodos por continuidad.

Despues conecte:

| KY-018 | NUCLEO-WL55JC1 | Funcion |
|---|---|---|
| `-` | `GND` del conector Arduino | referencia comun |
| `+` | `3V3` del conector Arduino, nunca `5V` | alimentacion del divisor |
| `S` | `A0` (CN8 pin 1, PB1/ADC1_IN5) | entrada ADC |

Antes de unir `S` a `A0`, alimente el modulo a 3.3 V y mida `S` respecto de
GND: debe permanecer entre 0 y 3.3 V. En este montaje la lectura normalmente
sube con mas luz porque la LDR une `+` con `S` y la resistencia de 10 kohm une
`S` con `-`. Alimentarlo a 3.3 V garantiza que la señal no pueda superar la
alimentacion admitida por el ADC.

## 2. Que hace cada modo

| Perfil de medida | Restriccion de la aplicacion | Estado esperado durante una espera larga |
|---|---|---|
| `reference` | `pm_policy_state_all_lock_get()` bloquea los estados PM | idle normal, sin STOP gestionado por PM |
| `response` | solicitud maxima de salida de 5 us | solo subestado 1, STOP0 |
| `saving` | ninguna restriccion de la aplicacion | subestado 3, STOP2 |

El hilo principal usa una espera con fecha absoluta. Mientras espera no esta
ejecutable, por lo que el hilo idle puede aplicar la politica. `reference` no
hace espera activa: Zephyr aun puede ejecutar el idle basico/WFI, pero no entra
en ninguno de los tres estados PM declarados.

### Nota honesta sobre la latencia

El Devicetree original de Zephyr 4.4.2 declara residencias minimas de 100, 500 y
900 us, pero no incluye `exit-latency-us`. Con ceros, la API de latencia no
produciria ninguna diferencia. El overlay de este proyecto añade 4/8/9 us para
STOP0/1/2 y el modo `response` solicita 5 us.

Esos valores son metadata experimental redondeada a partir de la tabla 58 del
datasheet STM32WL55/54 (los tiempos publicados separan claramente STOP0 de
STOP1/2). No son una garantia de latencia extremo a extremo de Zephyr: para una
afirmacion de tiempo real habria que medir desde el evento de despertar hasta
un GPIO de la aplicacion con osciloscopio y ajustar la metadata.

## 3. Compilar y probar primero el sensor

Desde PowerShell en la raiz del proyecto:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-profile.ps1 -Profile diagnostic
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash-profile.ps1 -Profile diagnostic
```

Abra el puerto serie a `115200 8N1`. Tambien se puede usar
`Ctrl+Shift+B -> Demo: Build profile`, y las tareas `Demo: Flash profile` y
`Serial Monitor` de VS Code.

La salida muestra el modo, los tres estados y una linea por segundo:

```text
muestra=12 raw=2870 tension=2312 mV digest=0x...
```

Ilumine y tape la LDR. Anote al menos cinco valores en cada condicion. Deben
cambiar claramente y, con el circuito confirmado arriba, aumentar al iluminar.
No continue a la medida de corriente si aparecen errores ADC o si la lectura no
cambia.

Para comprobar la misma lectura y las restricciones de cada modo use, uno por
uno, `verify-reference`, `verify-response` y `verify-saving`. Estos perfiles
tienen consola y periodo de 1 s: sirven para verificar, **no para comparar
corriente**. En la lista de estados disponibles debe verse:

- referencia: los tres en `no`;
- respuesta: solo el subestado 1 en `si`;
- ahorro: los tres en `si`.

## 4. Compilaciones de medida

Compile los tres perfiles silenciosos:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-profile.ps1 -Profile reference
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-profile.ps1 -Profile response
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-profile.ps1 -Profile saving
```

Cada uno genera `build-<perfil>/zephyr/zephyr.hex`. Para flashear, por ejemplo:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash-profile.ps1 -Profile response
```

Los perfiles de medida tienen consola, `printk`, logging, depuracion y LED de
aplicacion desactivados. No cambian ni el periodo de 5000 ms ni las ocho
conversiones y el calculo efectuado en cada ciclo.

## 5. Medida correcta en JP1

JP1 esta rotulado `I_SoC`. Segun el manual UM2592, al retirar su puente y poner
un amperimetro en serie se mide toda la corriente del **STM32WL** (`I_RF +
I_SYS`). No es la corriente total de la placa: no incluye ST-LINK, sus LED, el
regulador ni el KY-018 alimentado desde el pin 3V3 de los conectores.

Procedimiento seguro para cada firmware:

1. Con JP1 puesto y sin amperimetro, compile y flashee. No deje una sesion de
   depuracion abierta.
2. Desconecte el USB y toda alimentacion.
3. Retire unicamente el puente JP1. Con JP1 abierto el MCU no recibe corriente.
4. Configure el multimetro en corriente continua y empiece por un rango seguro.
   Compruebe que la punta roja esta en el borne de corriente.
5. Conecte las dos puntas a los dos pines de JP1: el amperimetro sustituye al
   puente y queda **en serie**. Si el signo es negativo, intercambie las puntas.
6. Compruebe el circuito antes de alimentar. Nunca conecte un amperimetro entre
   3V3 y GND.
7. Conecte el USB, espere 30 s y mida. Si el MCU se reinicia o no arranca, quite
   alimentacion y use un rango con menor resistencia interna o un analizador de
   potencia.
8. Apague antes de cambiar firmware, rango, puntas o recolocar JP1.

Mantenga en todas las pruebas la misma luz, cableado, alimentacion, rango del
multimetro y estado de los jumpers. Los LED del ST-LINK pueden seguir encendidos,
pero no atraviesan JP1; aun asi no interprete esta medida como consumo de la
placa completa.

Un multimetro convencional suele mostrar un promedio lento y no captura el
pico breve de las ocho conversiones. Informe solo el promedio/intervalo visible.
Para forma, amplitud y carga de los pulsos hace falta un power profiler,
osciloscopio con shunt o instrumento equivalente.

## 6. Protocolo repetible y tabla

1. Ejecute antes `verify-<modo>`, ilumine/tape y confirme que sigue midiendo.
2. Flashee el perfil silencioso correspondiente.
3. Fije mecanicamente el sensor y la fuente de luz. Espere 30 s.
4. Registre durante 60 s el valor estable o minimo/maximo visibles.
5. Repita tres veces por modo. Alterne el orden de modos para reducir deriva
   termica y de bateria/fuente.
6. Al final vuelva a ejecutar `verify-<modo>` y confirme otra vez luz/tapado.

| Modo | Periodo (ms) | Luz/posicion | Repeticion | Rango DMM | I media visible | I min-max visible | Lectura KY-018 OK | Notas |
|---|---:|---|---:|---|---:|---:|---|---|
| referencia | 5000 | | 1 | | | | | |
| referencia | 5000 | | 2 | | | | | |
| referencia | 5000 | | 3 | | | | | |
| respuesta | 5000 | | 1 | | | | | |
| respuesta | 5000 | | 2 | | | | | |
| respuesta | 5000 | | 3 | | | | | |
| ahorro | 5000 | | 1 | | | | | |
| ahorro | 5000 | | 2 | | | | | |
| ahorro | 5000 | | 3 | | | | | |

Use la media de las tres repeticiones. Sin inventar datos:

```text
ahorro frente a referencia (%) = 100 * (I_ref - I_ahorro) / I_ref
ahorro frente a respuesta (%)  = 100 * (I_resp - I_ahorro) / I_resp
```

Si tension e intervalo son iguales, el porcentaje de corriente media es tambien
el porcentaje aproximado de energia por unidad de tiempo. No presente precision
mayor que la resolucion y estabilidad observadas del multimetro.

## 7. Si STOP0 y STOP2 no se distinguen

No fuerce una conclusion. La diferencia puede quedar oculta por la resolucion
del instrumento, la resistencia interna del amperimetro, una sesion de debug,
los consumos base del dominio RF/temporizador o los promedios lentos del DMM.
Informe que el montaje no resolvio esa diferencia.

La variante `saving-fast` mantiene exactamente la politica `saving` y el mismo
trabajo por muestra, pero cambia el periodo de 5000 a 100 ms:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-profile.ps1 -Profile saving-fast
```

Compare `saving` con `saving-fast` en una tabla aparte. En ese experimento la
variable independiente es la **frecuencia de muestreo**, no la politica de
estados de energia. El aumento esperado del promedio procede de ejecutar el
mismo trabajo activo mas veces por segundo.

## Fuentes tecnicas

- [Manual de la NUCLEO-WL55JC, UM2592](https://www.st.com/resource/en/user_manual/um2592-stm32wl-nucleo64-board-mb1389-stmicroelectronics.pdf)
- [Datasheet STM32WL55/54](https://www.st.com/resource/en/datasheet/stm32wl55jc.pdf)
- [Placa `nucleo_wl55jc` en Zephyr](https://docs.zephyrproject.org/latest/boards/st/nucleo_wl55jc/doc/nucleo_wl55jc.html)
- [Implementacion STM32WL de STOP0/1/2 en Zephyr 4.4.2](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.2/soc/st/stm32/stm32wlx/power.c)
- [Estados STM32WL declarados en Zephyr 4.4.2](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.2/dts/arm/st/wl/stm32wl.dtsi)
- [Binding `zephyr,power-state`](https://docs.zephyrproject.org/latest/build/dts/api/bindings/power/zephyr%2Cpower-state.html)
- [API de politica PM de Zephyr](https://docs.zephyrproject.org/latest/doxygen/html/group__subsys__pm__sys__policy.html)
