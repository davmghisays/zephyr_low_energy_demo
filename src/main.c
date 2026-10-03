#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
#include <zephyr/sys/printk.h>
#endif

#define USER_NODE DT_PATH(zephyr_user)
#define READS_PER_CYCLE 8U

#if !DT_NODE_HAS_PROP(USER_NODE, io_channels)
#error "El overlay debe definir zephyr,user/io-channels"
#endif

static const struct adc_dt_spec light_adc = ADC_DT_SPEC_GET(USER_NODE);

/*
 * Se mantienen volatile para poder inspeccionarlas con un depurador despues
 * de una prueba funcional. No se debe dejar el depurador conectado al medir.
 */
volatile uint16_t demo_last_raw;
volatile uint32_t demo_sample_count;
volatile uint32_t demo_sample_digest;
volatile int demo_last_error;

#if defined(CONFIG_DEMO_MODE_RESPONSE)
static struct pm_policy_latency_request response_latency;
#endif

#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
static const char *energy_mode_name(void)
{
#if defined(CONFIG_DEMO_MODE_REFERENCE)
	return "referencia";
#elif defined(CONFIG_DEMO_MODE_RESPONSE)
	return "respuesta-rapida";
#else
	return "ahorro";
#endif
}
#endif

static void apply_energy_policy(void)
{
#if defined(CONFIG_DEMO_MODE_REFERENCE)
	/*
	 * Impide que Zephyr seleccione cualquiera de los estados PM durante las
	 * esperas. El hilo aun se bloquea y el idle normal puede ejecutar WFI.
	 * El perfil es fijo: el lock dura toda la ejecucion; si se cambiase el
	 * modo en runtime se liberaria con pm_policy_state_all_lock_put().
	 */
	pm_policy_state_all_lock_get();
#elif defined(CONFIG_DEMO_MODE_RESPONSE)
	/*
	 * Deja reposar al sistema, pero excluye estados cuya exit-latency-us
	 * supere el limite. La solicitud dura toda la ejecucion; en un cambio de
	 * modo se retiraria con pm_policy_latency_request_remove().
	 */
	pm_policy_latency_request_add(&response_latency,
				      CONFIG_DEMO_RESPONSE_LATENCY_US);
#else
	/* Ahorro: la aplicacion no limita los estados que puede elegir Zephyr. */
#endif
}

static int read_light_cycle(uint16_t *average)
{
	uint32_t sum = 0U;

	for (uint32_t i = 0U; i < READS_PER_CYCLE; i++) {
		uint16_t raw = 0U;
		struct adc_sequence sequence = {
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};
		int ret;

		ret = adc_sequence_init_dt(&light_adc, &sequence);
		if (ret < 0) {
			return ret;
		}

		ret = adc_read_dt(&light_adc, &sequence);
		if (ret < 0) {
			return ret;
		}

		sum += raw;
	}

	*average = (uint16_t)(sum / READS_PER_CYCLE);
	return 0;
}

#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
static void print_startup_diagnostic(void)
{
	const struct pm_state_info *states;
	uint8_t count = pm_state_cpu_get_all(0U, &states);
	uint32_t available_mask = 0U;

	/*
	 * Capturamos la disponibilidad antes del primer printk. El driver UART
	 * STM32 bloquea temporalmente los estados STOP mientras transmite; si se
	 * consultase dentro del bucle de impresion, todos aparecerian como "no".
	 */
	for (uint8_t i = 0U; (i < count) && (i < 32U); i++) {
		if (pm_policy_state_is_available(states[i].state,
						 states[i].substate_id)) {
			available_mask |= BIT(i);
		}
	}

	printk("\nDemo de energia NUCLEO-WL55JC1 + KY-018\n");
	printk("Modo: %s; intervalo: %d ms; A0/PB1/ADC1_IN5\n",
	       energy_mode_name(), CONFIG_DEMO_SAMPLE_INTERVAL_MS);
	printk("La consola es solo diagnostica: no mida corriente con este perfil.\n");
	printk("Estados PM declarados (permitido por la politica de este modo):\n");
	for (uint8_t i = 0U; i < count; i++) {
		printk("  %s subestado=%u residencia=%u us salida=%u us permitido=%s\n",
		       pm_state_to_str(states[i].state), states[i].substate_id,
		       states[i].min_residency_us, states[i].exit_latency_us,
		       ((i < 32U) && ((available_mask & BIT(i)) != 0U)) ? "si" : "no");
	}
}
#endif

int main(void)
{
	int ret;
	int64_t next_sample_ms;

	if (!adc_is_ready_dt(&light_adc)) {
		demo_last_error = -ENODEV;
#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
		printk("ERROR: ADC no preparado (%s)\n", light_adc.dev->name);
#endif
		return 0;
	}

	ret = adc_channel_setup_dt(&light_adc);
	if (ret < 0) {
		demo_last_error = ret;
#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
		printk("ERROR: configuracion ADC: %d\n", ret);
#endif
		return 0;
	}

	apply_energy_policy();

#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
	print_startup_diagnostic();
#endif

	next_sample_ms = k_uptime_get();
	while (true) {
		uint16_t raw;

		ret = read_light_cycle(&raw);
		demo_last_error = ret;
		if (ret == 0) {
			int32_t millivolts = raw;
			int mv_ret = adc_raw_to_millivolts_dt(&light_adc,
							  &millivolts);

			demo_last_raw = raw;
			demo_sample_count++;
			demo_sample_digest = (demo_sample_digest * 33U) ^ raw;

#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
			if (mv_ret == 0) {
				printk("muestra=%" PRIu32 " raw=%u tension=%" PRId32
				       " mV digest=0x%08" PRIx32 "\n",
				       demo_sample_count, raw, millivolts,
				       demo_sample_digest);
			} else {
				printk("muestra=%" PRIu32 " raw=%u digest=0x%08" PRIx32
				       " (mV no disponible: %d)\n",
				       demo_sample_count, raw, demo_sample_digest, mv_ret);
			}
#else
			ARG_UNUSED(mv_ret);
			ARG_UNUSED(millivolts);
#endif
		}
#if defined(CONFIG_DEMO_CONSOLE_OUTPUT)
		else {
			printk("ERROR: lectura ADC: %d\n", ret);
		}
#endif

		/*
		 * Fecha absoluta: el inicio de cada ciclo conserva el mismo periodo
		 * aunque la lectura tarde algo. Mientras duerme, el hilo main no esta
		 * ejecutable y el hilo idle deja que la politica PM elija un estado.
		 */
		next_sample_ms += CONFIG_DEMO_SAMPLE_INTERVAL_MS;
		if (next_sample_ms <= k_uptime_get()) {
			next_sample_ms = k_uptime_get() + CONFIG_DEMO_SAMPLE_INTERVAL_MS;
		}
		k_sleep(K_TIMEOUT_ABS_MS(next_sample_ms));
	}

	return 0;
}
