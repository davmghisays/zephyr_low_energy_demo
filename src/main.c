#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

/*
 * La aplicacion lee el KY-018 periodicamente y muestra el resultado por serie.
 * Los botones solo cambian la politica de energia. La lectura, el intervalo y
 * los mensajes son iguales en los tres modos.
 */
#define ADC_NODE DT_PATH(zephyr_user)
#define READS_PER_CYCLE 8
#define BUTTON_COUNT 3

/* MODE_NONE solo se usa durante el arranque, antes de aplicar el primer modo. */
enum energy_mode {
	MODE_NONE = -1,
	MODE_REFERENCE,
	MODE_RESPONSE,
	MODE_SAVING,
};

/* sw0, sw1 y sw2 ya estan definidos por la NUCLEO en su Devicetree. */
static const struct gpio_dt_spec buttons[BUTTON_COUNT] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios),
};

/* Estos dos dispositivos se obtienen del Devicetree de la placa. */
static const struct adc_dt_spec light_adc = ADC_DT_SPEC_GET(ADC_NODE);
static const struct gpio_dt_spec sensor_power =
	GPIO_DT_SPEC_GET(ADC_NODE, sensor_power_gpios);
static const struct device *const console_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Cada boton necesita su callback. La peticion de latencia se conserva para
 * poder retirarla al abandonar el modo de respuesta rapida.
 */
static struct gpio_callback button_callbacks[BUTTON_COUNT];
static struct pm_policy_latency_request latency_request;
static struct k_work mode_work;

/* La interrupcion escribe requested_mode y el work de Zephyr lo lee.
 * atomic_t permite compartir el valor de forma segura entre ambos contextos.
 * El work evita cambiar la politica o imprimir dentro de la interrupcion.
 */
static atomic_t requested_mode = MODE_SAVING;
static enum energy_mode current_mode = MODE_NONE;

#if !defined(CONFIG_DEMO_RUNTIME_PM_DEVICES)
/*
 * En STM32WL, pasar una vez por suspend/resume deja el periferico en el mismo
 * estado inicial que la variante Runtime PM que ya esta validada. disable()
 * retira inmediatamente el dispositivo de Runtime PM y lo deja activo, por lo
 * que no hay transiciones get/put ni apagados entre adquisiciones.
 */
static int prepare_direct_device(const struct device *dev)
{
	int err = pm_device_runtime_enable(dev);

	if (err != 0) {
		return err;
	}

	return pm_device_runtime_disable(dev);
}
#endif

static void set_energy_mode(enum energy_mode mode)
{
	if (mode == current_mode) {
		return;
	}

	/* Primero se retira la restriccion anterior. Cada get/add debe tener su
	 * put/remove para no dejar una restriccion acumulada por accidente.
	 */
	if (current_mode == MODE_REFERENCE) {
		pm_policy_state_all_lock_put();
	} else if (current_mode == MODE_RESPONSE) {
		pm_policy_latency_request_remove(&latency_request);
	}

	if (mode == MODE_REFERENCE) {
		/* Impide que la politica elija cualquiera de los estados STOP. */
		pm_policy_state_all_lock_get();
		printk("Modo: referencia\n");
	} else if (mode == MODE_RESPONSE) {
		/* Solo permite estados cuya salida cumpla la latencia solicitada. */
		pm_policy_latency_request_add(&latency_request,
					      CONFIG_DEMO_RESPONSE_LATENCY_US);
		printk("Modo: respuesta rapida\n");
	} else {
		/* Ahorro no añade restricciones: decide la politica de Zephyr. */
		printk("Modo: ahorro\n");
	}

	current_mode = mode;
}

static void change_mode(struct k_work *work)
{
	ARG_UNUSED(work);
	/* El cambio real se hace aqui, no dentro de la interrupcion GPIO. */
	set_energy_mode((enum energy_mode)atomic_get(&requested_mode));
}

static void button_pressed(const struct device *port,
			   struct gpio_callback *callback, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	/* La interrupcion solo guarda el boton pulsado y solicita el trabajo. */
	for (int i = 0; i < BUTTON_COUNT; i++) {
		if (callback == &button_callbacks[i]) {
			atomic_set(&requested_mode, i);
			k_work_submit(&mode_work);
			return;
		}
	}
}

static int setup_buttons(void)
{
	for (int i = 0; i < BUTTON_COUNT; i++) {
		/* GPIO_INPUT conserva el pull-up y la polaridad declarados por la placa. */
		if (!gpio_is_ready_dt(&buttons[i]) ||
		    gpio_pin_configure_dt(&buttons[i], GPIO_INPUT) != 0) {
			return -1;
		}

		gpio_init_callback(&button_callbacks[i], button_pressed,
				   BIT(buttons[i].pin));

		/* Una pulsacion genera la interrupcion que tambien despierta al MCU. */
		if (gpio_add_callback(buttons[i].port, &button_callbacks[i]) != 0 ||
		    gpio_pin_interrupt_configure_dt(&buttons[i],
						    GPIO_INT_EDGE_TO_ACTIVE) != 0) {
			return -1;
		}
	}

	return 0;
}

static int setup_devices(void)
{
	if (!adc_is_ready_dt(&light_adc) ||
	    adc_channel_setup_dt(&light_adc) != 0) {
		return -1;
	}

	/* D4/PB5 alimenta el KY-018 segun el perfil de energia seleccionado. */
	if (!gpio_is_ready_dt(&sensor_power) ||
	    gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_INACTIVE) != 0) {
		return -1;
	}

#if defined(CONFIG_DEMO_SENSOR_POWER_ALWAYS_ON)
	/* El sensor permanece detras de JP1 y no se interrumpe entre muestras.
	 */
	if (gpio_pin_set_dt(&sensor_power, 1) != 0) {
		return -1;
	}
#endif

	/* La variante gestionada entrega ADC y UART a Runtime PM. La directa los
	 * normaliza una vez durante el arranque y los deja activos permanentemente.
	 */
#if defined(CONFIG_DEMO_RUNTIME_PM_DEVICES)
	if (pm_device_runtime_enable(light_adc.dev) != 0) {
		return -1;
	}

	if (!device_is_ready(console_uart) ||
	    pm_device_runtime_enable(console_uart) != 0) {
		return -1;
	}
#else
	if (!device_is_ready(console_uart) ||
	    prepare_direct_device(light_adc.dev) != 0 ||
	    prepare_direct_device(console_uart) != 0) {
		return -1;
	}
#endif
	return 0;
}

static int sensor_power_enable(void)
{
#if !defined(CONFIG_DEMO_SENSOR_POWER_ALWAYS_ON)
	int err = gpio_pin_set_dt(&sensor_power, 1);

	if (err != 0) {
		return err;
	}

	/* El divisor del KY-018 y la entrada ADC deben asentarse antes de leer. */
	k_sleep(K_MSEC(CONFIG_DEMO_SENSOR_SETTLE_MS));
#endif

	return 0;
}

static int sensor_power_disable(void)
{
#if !defined(CONFIG_DEMO_SENSOR_POWER_ALWAYS_ON)
	return gpio_pin_set_dt(&sensor_power, 0);
#else
	return 0;
#endif
}

static int adc_power_get(void)
{
#if defined(CONFIG_DEMO_RUNTIME_PM_DEVICES)
	return pm_device_runtime_get(light_adc.dev);
#else
	return 0;
#endif
}

static int adc_power_put(void)
{
#if defined(CONFIG_DEMO_RUNTIME_PM_DEVICES)
	return pm_device_runtime_put(light_adc.dev);
#else
	return 0;
#endif
}

static int read_light(uint16_t *average)
{
	uint16_t sample;
	uint32_t sum = 0;

	/* Zephyr completa canales, resolucion y ganancia desde el overlay. */
	struct adc_sequence sequence = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};

	int err = adc_sequence_init_dt(&light_adc, &sequence);
	if (err != 0) {
		return err;
	}

	/* La alimentacion externa se gestiona en el mismo tramo que el ADC. */
	err = sensor_power_enable();
	if (err != 0) {
		return err;
	}

	/* En el perfil gestionado se reactiva el ADC; en directo es un no-op. */
	err = adc_power_get();
	if (err != 0) {
		(void)sensor_power_disable();
		return err;
	}

	/* Todos los modos realizan las mismas ocho conversiones. */
	for (int i = 0; i < READS_PER_CYCLE; i++) {
		err = adc_read_dt(&light_adc, &sequence);
		if (err != 0) {
			break;
		}
		sum += sample;
	}

	/* La liberacion PM debe ejecutarse incluso si una conversion fallo. */
	int pm_err = adc_power_put();
	int power_err = sensor_power_disable();

	if (err != 0) {
		return err;
	}
	if (pm_err != 0) {
		return pm_err;
	}
	if (power_err != 0) {
		return power_err;
	}

	*average = sum / READS_PER_CYCLE;
	return 0;
}

int main(void)
{
	/* La siguiente muestra tiene una fecha fija para no acumular retrasos. */
	int64_t next_sample = k_uptime_get();
	uint32_t sample_count = 0;

	if (setup_devices() != 0) {
		printk("Error al iniciar ADC, UART o alimentacion del sensor\n");
		return 0;
	}

	/* El firmware siempre empieza sin restricciones, en modo ahorro. */
	k_work_init(&mode_work, change_mode);
	set_energy_mode(MODE_SAVING);

	if (setup_buttons() != 0) {
		printk("Error al iniciar los botones\n");
		return 0;
	}

	printk("B1: referencia; B2: respuesta rapida; B3: ahorro\n");
#if defined(CONFIG_DEMO_RUNTIME_PM_DEVICES)
	printk("ADC y UART gestionados por Runtime PM\n");
#else
	printk("ADC y UART directos, sin Runtime PM de aplicacion\n");
#endif
#if !defined(CONFIG_PM)
	printk("PM del sistema desactivado; no se entra en STOP\n");
#elif defined(CONFIG_PM_DEVICE_SYSTEM_MANAGED)
	printk("System-managed activo para dispositivos\n");
#else
	printk("System-managed desactivado para dispositivos\n");
#endif
#if defined(CONFIG_DEMO_SENSOR_POWER_ALWAYS_ON)
	printk("KY-018 alimentado permanentemente\n");
#else
	printk("KY-018 alimentado solo durante la adquisicion\n");
#endif
	printk("Intervalo: %d ms; ADC: A0/PB1; KY-018: D4/PB5\n",
	       CONFIG_DEMO_SAMPLE_INTERVAL_MS);
	printk("Estabilizacion del sensor: %d ms\n",
	       CONFIG_DEMO_SENSOR_SETTLE_MS);

	while (1) {
		uint16_t raw;

		/* Esta parte del ciclo es exactamente igual en los tres modos. */
		int err = read_light(&raw);

		if (err == 0) {
			int32_t light_mv = raw;
			int conv_err = adc_raw_to_millivolts_dt(&light_adc, &light_mv);

			sample_count++;
			if (conv_err == 0) {
				printk("Muestra %u -> raw: %u - tension -> %u\n",
				       (unsigned int)sample_count, (unsigned int)raw,
				       (unsigned int)light_mv);
			} else {
				printk("Muestra %u -> raw: %u - sin conversion a mV\n",
				       (unsigned int)sample_count, (unsigned int)raw);
			}
		} else {
			printk("Error al leer el ADC\n");
		}

		next_sample += CONFIG_DEMO_SAMPLE_INTERVAL_MS;

		/* k_sleep deja el hilo inactivo; no es un bucle de espera. Mientras no
		 * haya trabajo, Zephyr puede llevar el sistema al estado permitido por
		 * el modo actual. La fecha absoluta evita acumular retrasos entre ciclos.
		 */
		k_sleep(K_TIMEOUT_ABS_MS(next_sample));
	}

	return 0;
}
