# Demo real de energia con Zephyr, NUCLEO-WL55JC1 y KY-018

La aplicacion mide luz por `A0/PB1/ADC1_IN5` y compara tres politicas de
energia sin BLE, LoRa ni ninguna transmision. Las tres mediciones hacen
exactamente ocho conversiones ADC y escriben una linea por el puerto serie cada
1000 ms; solo cambia la restriccion que la aplicacion entrega a la politica PM
de Zephyr.

## 0. Mapa mental: que hace cada cosa

- **Compilar** crea un firmware, pero no cambia la placa.
- **Flashear** copia ese firmware al STM32WL y reinicia la aplicacion.
- Los botones cambian el modo durante la ejecucion, sin recompilar ni flashear.
- Hay una sola configuracion: siempre muestra una lectura por segundo en el
  monitor serie.
- JP1 no es solo un punto de prueba. Es parte del cable que alimenta el MCU:
  debe estar cerrado por el jumper o continuamente por el amperimetro.

Entorno comprobado para este proyecto:

- Zephyr `v4.4.2` y west `1.5.0`.
- Board Zephyr: `nucleo_wl55jc` (valido para la NUCLEO-WL55JC1).
- Toolchain: Zephyr SDK `1.0.1`.
- CPU0: STM32WL55 Cortex-M4.
- Estados declarados: `suspend-to-idle`, subestados 1, 2 y 3. El driver de
  esta version los implementa respectivamente como STOP0, STOP1 y STOP2.
- La gestion de energia de dispositivos esta habilitada para que el driver
  suspenda y reinicialice correctamente el ADC alrededor de cada STOP. Sin
  ella, STM32WL presenta el fallo conocido de quedarse bloqueado tras la
  primera lectura (`zephyrproject-rtos/zephyr#37352`).

Validacion realizada en esta placa: la configuracion unica compila y se puede
flashear. Tambien se comprobo que el ADC sigue leyendo despues de varios ciclos
de suspension y reanudacion.

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

## 2. Elegir el modo con los botones

El firmware arranca en ahorro. Los tres botones integrados seleccionan el modo
inmediatamente y el monitor serie confirma cada cambio:

| Boton | GPIO | Modo | Restriccion de la aplicacion | Estado esperado durante una espera larga |
|---|---|---|---|---|
| `B1/SW1` | PA0 | referencia | `pm_policy_state_all_lock_get()` bloquea los estados PM | idle normal, sin STOP gestionado por PM |
| `B2/SW2` | PA1 | respuesta rapida | solicitud maxima de salida de 5 us | solo subestado 1, STOP0 |
| `B3/SW3` | PC6 | ahorro | ninguna restriccion de la aplicacion | subestado 3, STOP2 |

Al cambiar, el programa retira primero la restriccion anterior y aplica la
nueva. Pulsar otra vez el boton del modo actual no acumula restricciones. No es
necesario recompilar ni reiniciar la placa.

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

## 3. Compilar, flashear y probar

Desde PowerShell en la raiz del proyecto:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\flash.ps1
```

Abra el puerto serie a `115200 8N1`. Tambien se puede usar
`Ctrl+Shift+B -> Demo: Build`, y las tareas `Demo: Flash` y
`Serial Monitor` de VS Code.

La salida muestra el modo y una linea por segundo:

```text
Modo: ahorro
B1: referencia; B2: respuesta rapida; B3: ahorro
Intervalo: 1000 ms; ADC: A0/PB1
Muestra 12: 2870
```

Ilumine y tape la LDR. Anote al menos cinco valores en cada condicion. Deben
cambiar claramente y, con el circuito confirmado arriba, aumentar al iluminar.
No continue a la medida de corriente si aparecen errores ADC o si la lectura no
cambia.

Si aparece solamente `Muestra 1`, haga primero esta prueba sin multimetro:
apague, coloque el jumper JP1, encienda y repita la prueba. Si asi aparecen
`Muestra 2`, `Muestra 3`, etc., el firmware funciona y antes se estaba abriendo
la alimentacion al retirar o mover una sonda. Para medir, las dos sondas deben
permanecer sujetas a los dos pines de JP1 durante toda la prueba; use pinzas o
ganchos, no contactos manuales intermitentes.

Pulse cada boton y compruebe que aparece el nombre correcto y que siguen
llegando muestras. El firmware queda en `build/zephyr/zephyr.hex`; se compila y
flashea una sola vez para probar los tres modos.

La consola permanece activa durante todas las medidas. Su consumo y sus
despertares forman parte del resultado, por lo que estas medidas sirven para una
comparacion relativa entre modos, no para declarar el consumo minimo absoluto
del microcontrolador. El texto y la frecuencia de salida son iguales en los
tres modos.

## 4. Medida correcta en JP1

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

Receta practica, sin alternar entre jumper y sondas con la placa encendida:

1. **Fase funcional:** JP1 colocado, multimetro fuera. Compile y flashee una
   vez, seleccione el modo con su boton y compruebe varias muestras en serie.
2. **Fase de preparacion:** cierre el monitor serie y desconecte el USB.
3. **Fase de medida:** retire JP1, conecte firmemente el amperimetro en modo
   corriente entre ambos pines y solo entonces vuelva a conectar el USB.
   Abra otra vez el monitor serie para confirmar que siguen apareciendo muestras.
4. Mantenga las sondas conectadas. Si se abre el circuito, el MCU se apaga y
   la siguiente conexion comienza otra vez en `Muestra 1`.

Mantenga en todas las pruebas la misma luz, cableado, alimentacion, rango del
multimetro y estado de los jumpers. Los LED del ST-LINK pueden seguir encendidos,
pero no atraviesan JP1; aun asi no interprete esta medida como consumo de la
placa completa.

Un multimetro convencional suele mostrar un promedio lento y no captura el
pico breve de las ocho conversiones. Informe solo el promedio/intervalo visible.
Para forma, amplitud y carga de los pulsos hace falta un power profiler,
osciloscopio con shunt o instrumento equivalente.

## 5. Protocolo repetible y tabla

1. Compile y flashee una vez. Ilumine/tape y confirme en el monitor serie que
   sigue midiendo.
2. Fije mecanicamente el sensor y la fuente de luz. Espere 30 s.
3. Conecte el amperimetro en JP1 siguiendo el procedimiento anterior y mantenga
   abierto el monitor serie.
4. Registre durante 60 s el valor estable o minimo/maximo visibles.
5. Pulse el boton del siguiente modo, sueltelo y espere 5 s antes de registrar.
   Repita tres veces por modo y alterne el orden para reducir deriva termica.
6. Antes de cada cambio confirme que las muestras responden a la luz y al
   tapado. No incluya en la medida el instante de la pulsacion.

| Modo | Periodo (ms) | Luz/posicion | Repeticion | Rango DMM | I media visible | I min-max visible | Lectura KY-018 OK | Notas |
|---|---:|---|---:|---|---:|---:|---|---|
| referencia | 1000 | | 1 | | | | | |
| referencia | 1000 | | 2 | | | | | |
| referencia | 1000 | | 3 | | | | | |
| respuesta | 1000 | | 1 | | | | | |
| respuesta | 1000 | | 2 | | | | | |
| respuesta | 1000 | | 3 | | | | | |
| ahorro | 1000 | | 1 | | | | | |
| ahorro | 1000 | | 2 | | | | | |
| ahorro | 1000 | | 3 | | | | | |

Use la media de las tres repeticiones. Sin inventar datos:

```text
ahorro frente a referencia (%) = 100 * (I_ref - I_ahorro) / I_ref
ahorro frente a respuesta (%)  = 100 * (I_resp - I_ahorro) / I_resp
```

Si tension e intervalo son iguales, el porcentaje de corriente media es tambien
el porcentaje aproximado de energia por unidad de tiempo. No presente precision
mayor que la resolucion y estabilidad observadas del multimetro.

## 6. Si STOP0 y STOP2 no se distinguen

No fuerce una conclusion. La diferencia puede quedar oculta por la resolucion
del instrumento, la resistencia interna del amperimetro, una sesion de debug,
los consumos base del dominio RF/temporizador o los promedios lentos del DMM.
Informe que el montaje no resolvio esa diferencia.

Para comparar frecuencias, pulse `B3/SW3` para seleccionar ahorro y cambie
temporalmente en `prj.conf`:

```ini
CONFIG_DEMO_SAMPLE_INTERVAL_MS=100
```

Compare ese resultado con ahorro a 1000 ms en una tabla aparte y restaure
despues el valor original. En ese experimento la variable independiente es la
**frecuencia de muestreo**, no la politica de estados de energia.

## Fuentes tecnicas

- [Manual de la NUCLEO-WL55JC, UM2592](https://www.st.com/resource/en/user_manual/um2592-stm32wl-nucleo64-board-mb1389-stmicroelectronics.pdf)
- [Datasheet STM32WL55/54](https://www.st.com/resource/en/datasheet/stm32wl55jc.pdf)
- [Placa `nucleo_wl55jc` en Zephyr](https://docs.zephyrproject.org/latest/boards/st/nucleo_wl55jc/doc/nucleo_wl55jc.html)
- [Implementacion STM32WL de STOP0/1/2 en Zephyr 4.4.2](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.2/soc/st/stm32/stm32wlx/power.c)
- [Estados STM32WL declarados en Zephyr 4.4.2](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.2/dts/arm/st/wl/stm32wl.dtsi)
- [Binding `zephyr,power-state`](https://docs.zephyrproject.org/latest/build/dts/api/bindings/power/zephyr%2Cpower-state.html)
- [API de politica PM de Zephyr](https://docs.zephyrproject.org/latest/doxygen/html/group__subsys__pm__sys__policy.html)
- [Incidencia STM32WL: ADC bloqueado tras la primera lectura con PM](https://github.com/zephyrproject-rtos/zephyr/issues/37352)
