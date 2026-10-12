# Demo de energia con Zephyr - variante Runtime PM

Demo para la `NUCLEO-WL55JC1` y un fotoresistor `KY-018`. Lee la luz por el
ADC, muestra el resultado por el puerto serie y permite cambiar la politica de
energia con los tres botones de la placa.

No utiliza BLE, LoRa, radio ni LEDs. En todos los modos se realizan las mismas
ocho conversiones ADC cada 1000 ms. Lo unico que cambia es la restriccion que
la aplicacion entrega a la politica de energia de Zephyr.

Esta rama compara cinco perfiles: sin gestion energetica, solo System-managed,
System-managed con conmutacion del sensor, Runtime PM mas System-managed y la
misma combinacion con alimentacion conmutada del sensor.

La rama `experiment/gpio-sensor-power` anade una prueba: el KY-018 se alimenta
desde Arduino `D4` (`PB5`) en lugar de `3V3`, por lo que su corriente pasa a
formar parte de la medida `I_SoC` de JP1. `prj.conf` decide si ese GPIO queda
activo o se enciende solo durante la adquisicion; los perfiles pueden
sobrescribir esa eleccion.

## Cableado

El KY-018 de la foto contiene una LDR y una resistencia `103` de 10 kohm. Su
salida `S` es analogica y no necesita componentes externos adicionales.

| KY-018 | NUCLEO-WL55JC1 |
|---|---|
| `-` | `GND` |
| `+` | `D4` (`PB5`, GPIO de alimentacion) |
| `S` | `A0`, que corresponde a PB1/ADC1_IN5 |

Con el modulo orientado como en la foto original, los pines son `-`, `+` y `S`
de arriba abajo. Como existen copias con serigrafia incorrecta, conviene
comprobarlo antes con un multimetro. No conecte a la vez `D4` y `3V3` al pin
`+`. Verifique que `S` permanece entre 0 y 3.3 V. No use 5 V.

## Modos

El firmware arranca en ahorro y cambia de modo sin reiniciar ni volver a
flashear:

| Boton | Modo | Comportamiento |
|---|---|---|
| `B1/SW1` | referencia | bloquea los estados de energia gestionados por PM |
| `B2/SW2` | respuesta rapida | solicita una latencia maxima de salida de 5 us |
| `B3/SW3` | ahorro | deja la seleccion de estado a la politica de Zephyr |

Al cambiar de modo se retira primero la restriccion anterior. Pulsar de nuevo
el boton del modo actual no acumula restricciones.

Los valores de latencia de STOP0, STOP1 y STOP2 incluidos en el overlay son
metadata experimental para esta comparacion. No representan una medicion de
latencia extremo a extremo.

## Perfiles de energia

Todos los perfiles hacen las mismas ocho conversiones y mantienen la misma
salida serie. Solo acumulan mecanismos de ahorro:

| Perfil | PM del sistema | Sensor KY-018 | ADC y UART | System-managed |
|---|---|---|---|---|
| `high-energy` | Desactivado | Siempre encendido | Directos, sin Runtime PM | Desactivado |
| `system-managed` | Activo | Siempre encendido | Directos, sin Runtime PM | Activo |
| `sensor-managed` | Activo | Solo durante la adquisicion | Directos, sin Runtime PM | Activo |
| `runtime-system` | Activo | Siempre encendido | Gestionados por Runtime PM | Activo |
| `low-energy` | Activo | Solo durante la adquisicion | Gestionados por Runtime PM | Activo |

`high-energy` desactiva `CONFIG_PM`, deja Runtime PM desactivado para ADC/UART y
desactiva `CONFIG_PM_DEVICE_SYSTEM_MANAGED`. El MCU no entra en STOP y ningun
mecanismo de la demo suspende los perifericos durante la ejecucion. Al arrancar
se hace una unica secuencia suspend/resume para dejar ambos perifericos en un
estado activo conocido; a continuacion se retiran de Runtime PM y permanecen
activos.

Con `system-managed`, ADC/UART no reciben transiciones Runtime PM desde la
aplicacion; System-managed los prepara al entrar y salir de STOP. Este efecto
solo ocurre en modos que permiten STOP: B1 lo bloquea.

`sensor-managed` conserva esas mismas condiciones para MCU, ADC y UART, pero
apaga D4/PB5 entre adquisiciones. Por tanto, la diferencia de corriente media
frente a `system-managed`, con el mismo boton seleccionado, aisla el efecto de
conmutar la alimentacion del KY-018, incluyendo el breve coste de accionar el
GPIO. Ambas medidas deben realizarse con la misma iluminacion, porque la
corriente del divisor depende de la resistencia de la LDR.

Con `runtime-system`, la aplicacion entrega ADC y UART a Runtime PM y deja
System-managed para los demas dispositivos durante STOP. Con `low-energy`, se
anade ademas el corte de D4/PB5 tras cada adquisicion.

En `sensor-managed`, un ciclo de adquisicion es:

```text
activar D4/PB5
esperar CONFIG_DEMO_SENSOR_SETTLE_MS
ocho conversiones con el ADC activo
desactivar D4/PB5
k_sleep()
```

En `low-energy`, el ciclo acumula ademas Runtime PM:

```text
activar D4/PB5
esperar CONFIG_DEMO_SENSOR_SETTLE_MS
pm_device_runtime_get(ADC)
ocho conversiones
pm_device_runtime_put(ADC)
desactivar D4/PB5
k_sleep()
```

Con `CONFIG_DEMO_RUNTIME_PM_DEVICES=y`, el `get` reactiva el ADC y aumenta su
contador de uso. El `put` reduce el contador a cero y lo suspende. La consola
gestiona la UART del mismo modo, programando su suspension 1 ms despues del
ultimo caracter.

Con `CONFIG_DEMO_RUNTIME_PM_DEVICES=n`, la aplicacion usa ADC y UART
directamente y no ejecuta `get/put` entre muestras. La unica secuencia
`enable/disable` ocurre durante el arranque para forzar la inicializacion
suspend/resume requerida por estos drivers; al terminar, Runtime PM queda
desactivado y ambos perifericos permanecen activos. Ese transitorio de arranque
no forma parte de la medicion estacionaria en JP1.

El valor inicial de `CONFIG_DEMO_SENSOR_SETTLE_MS` es 5 ms. Debe aumentarse si
las lecturas cambian al comparar con una espera mayor; puede reducirse despues
de comprobar experimentalmente que el modulo se estabiliza.
`CONFIG_DEMO_SENSOR_POWER_ALWAYS_ON` controla la alimentacion del sensor. Los
perfiles `sensor-managed` y `low-energy` la desactivan entre adquisiciones; los
otros tres la mantienen activa.

Para medir el consumo mas alto del demo, seleccione `high-energy`; este perfil
ya desactiva el PM del sistema y no entra en STOP. Para comparar las estrategias
de ahorro, use B3 en los otros perfiles y mantenga el cableado y la actividad de
consola sin cambios.

## Compilar y flashear

Entorno utilizado:

- Zephyr `4.4.2`.
- Board `nucleo_wl55jc`.
- Zephyr SDK `1.0.1`.

Desde PowerShell, en la raiz del proyecto, seleccione uno de los perfiles:

```powershell
# Sin gestion de energia del sistema ni de dispositivos
.\scripts\build.ps1 -Profile high-energy

# System-managed + conmutacion de la alimentacion del sensor
.\scripts\build.ps1 -Profile sensor-managed

# Solo System-managed
.\scripts\build.ps1 -Profile system-managed

# Runtime PM + System-managed
.\scripts\build.ps1 -Profile runtime-system

# Runtime PM + System-managed + corte de alimentacion del sensor
.\scripts\build.ps1 -Profile low-energy

# Tras seleccionar uno de los perfiles anteriores
.\scripts\flash.ps1
```

Sin parametro, `build.ps1` selecciona `low-energy`. Los perfiles solo
seleccionan archivos `.conf`; no inyectan opciones Kconfig desde el script.

En VS Code estan disponibles las cinco tareas `Demo: Build (...)`, una por
perfil, ademas de `Demo: Flash` y `Serial Monitor`.

El monitor serie utiliza `115200 8N1`. Una ejecucion normal se ve asi:

```text
Modo: ahorro
B1: referencia; B2: respuesta rapida; B3: ahorro
ADC y UART gestionados por Runtime PM
System-managed activo para dispositivos
KY-018 alimentado solo durante la adquisicion
Intervalo: 1000 ms; ADC: A0/PB1; KY-018: D4/PB5
Muestra 1: 602
```

Al iluminar y tapar la LDR, los valores deben cambiar. Cada pulsacion imprime
el nombre del nuevo modo y las muestras continúan apareciendo.

## Medicion de corriente

El puente `JP1`, rotulado `I_SoC`, permite medir en serie la corriente del
STM32WL. En la configuracion normal de esta rama, la corriente del KY-018 pasa
por el GPIO D4/PB5 y por tanto tambien se ve en JP1. La medida no incluye toda
la placa ni el ST-LINK.

Con la placa sin alimentacion:

1. Retire el jumper JP1.
2. Configure el multimetro para corriente continua.
3. Conecte el amperimetro entre los dos pines de JP1.
4. Vuelva a alimentar la placa.

El amperimetro sustituye al jumper y debe permanecer conectado. Nunca lo
conecte directamente entre 3V3 y GND.

Para comparar modos, pulse y suelte el boton correspondiente y espere unos
segundos antes de leer el multimetro. La consola permanece activa en todos los
modos, por lo que los resultados son comparaciones relativas y no el consumo
minimo absoluto del microcontrolador.

Esta variante debe compararse con la rama principal como un experimento
distinto. Runtime PM puede reducir el consumo de referencia y, por tanto,
reducir la diferencia visible entre los tres modos. No se debe atribuir ese
cambio solamente a la seleccion de STOP0, STOP1 o STOP2.

Un multimetro convencional muestra principalmente un promedio y puede no
capturar los pulsos breves de actividad del ADC, la UART y la alimentacion del
sensor. Para comparar cada escalon, mantenga constantes el modo B3, el
cableado y la actividad de consola. Reserve B1 para el limite superior de
`high-energy`.

## Archivos principales

- `src/main.c`: lectura ADC, botones y restricciones de energia.
- `prj.conf`: configuracion de Zephyr.
- `boards/nucleo_wl55jc.overlay`: canal ADC y latencias de los estados PM.
- `scripts/build.ps1`: compilacion.
- `scripts/flash.ps1`: flasheo.
