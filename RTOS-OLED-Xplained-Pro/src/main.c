#include <asf.h>
#include "conf_board.h"

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

/** IOS **/
#define LED_1_PIO PIOA
#define LED_1_PIO_ID ID_PIOA
#define LED_1_IDX 0
#define LED_1_IDX_MASK (1 << LED_1_IDX)

#define LED_2_PIO PIOC
#define LED_2_PIO_ID ID_PIOC
#define LED_2_IDX 30
#define LED_2_IDX_MASK (1 << LED_2_IDX)

#define LED_3_PIO PIOB
#define LED_3_PIO_ID ID_PIOB
#define LED_3_IDX 2
#define LED_3_IDX_MASK (1 << LED_3_IDX)

#define BUT_1_PIO PIOD
#define BUT_1_PIO_ID ID_PIOD
#define BUT_1_IDX 28
#define BUT_1_IDX_MASK (1u << BUT_1_IDX)

#define BUT_2_PIO PIOC
#define BUT_2_PIO_ID ID_PIOC
#define BUT_2_IDX 31
#define BUT_2_IDX_MASK (1u << BUT_2_IDX)

#define BUT_3_PIO PIOA
#define BUT_3_PIO_ID ID_PIOA
#define BUT_3_IDX 19
#define BUT_3_IDX_MASK (1u << BUT_3_IDX)

#define AFEC_POT AFEC0
#define AFEC_POT_ID ID_AFEC0
#define AFEC_POT_CHANNEL 0 // Canal do pino PD30

/** RTOS  */
#define TASK_OLED_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_OLED_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_ADC_STACK_SIZE (1024*10 / sizeof(portSTACK_TYPE))
#define TASK_ADC_STACK_PRIORITY (tskIDLE_PRIORITY)

#define TASK_EVENT_STACK_SIZE (1024*10 / sizeof(portSTACK_TYPE))
#define TASK_EVENT_STACK_PRIORITY (tskIDLE_PRIORITY)

#define TASK_ALARM_STACK_SIZE (1024*10 / sizeof(portSTACK_TYPE))
#define TASK_ALARM_STACK_PRIORITY (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);


typedef struct {
	uint value;
} adcData;

typedef struct {
	int but;
	int status;
} adcDataBut;

typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
} calendar;



/************************************************************************/
/* globals                                                           */
/************************************************************************/

QueueHandle_t xQueueADC;
QueueHandle_t xQueueEvent;
SemaphoreHandle_t xSemaphoreAfecAlarm;
SemaphoreHandle_t xSemaphoreEventAlarm;
SemaphoreHandle_t xSemaphoreDeletaAlarme;


/************************************************************************/
/* prototypes                                                           */
/************************************************************************/
void io_init(void);
void TC_init(Tc *TC, int ID_TC, int TC_CHANNEL, int freq);
static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel, afec_callback_t callback);
void pisca_led (int n, int t, Pio *pio, const uint32_t ul_mask);
void pin_toggle(Pio *pio, uint32_t mask);

/************************************************************************/
/* RTOS application funcs                                               */
/************************************************************************/

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

void but1_callback(void) {
	adcDataBut but1;
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	but1.but = 1;
	
	if (pio_get(BUT_1_PIO, PIO_INPUT, BUT_1_IDX_MASK) == 0){
		but1.status = 1;
	} else{
		but1.status = 0;
	}
		
	xQueueSendFromISR(xQueueEvent, &but1, xHigherPriorityTaskWoken);
}

void but2_callback(void) {
	adcDataBut but2;
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	but2.but = 2;
	
	if (pio_get(BUT_2_PIO, PIO_INPUT, BUT_2_IDX_MASK) == 0){
		but2.status = 1;
	} else{
		but2.status = 0;
	}
	
	xQueueSendFromISR(xQueueEvent, &but2, xHigherPriorityTaskWoken);
}

void but3_callback(void) {
	adcDataBut but3;
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	
	but3.but = 3;
	
	if (pio_get(BUT_3_PIO, PIO_INPUT, BUT_3_IDX_MASK) == 0){
		but3.status = 1;
	} else{
		but3.status = 0;
	}
	
	xQueueSendFromISR(xQueueEvent, &but3, xHigherPriorityTaskWoken);
}


void TC1_Handler(void) {
	volatile uint32_t ul_dummy;

	ul_dummy = tc_get_status(TC0, 1);

	/* Avoid compiler warning */
	UNUSED(ul_dummy);

	/* Selecina canal e inicializa conversão */
	afec_channel_enable(AFEC_POT, AFEC_POT_CHANNEL);
	afec_start_software_conversion(AFEC_POT);
}

void TC4_Handler(void) {
	/**
	* Devemos indicar ao TC que a interrupção foi satisfeita.
	* Isso é realizado pela leitura do status do periférico
	**/
	volatile uint32_t status = tc_get_status(TC1, 1);

	pin_toggle(LED_1_PIO, LED_1_IDX_MASK);
	
}

void TC2_Handler(void) {

	volatile uint32_t status = tc_get_status(TC0, 2);

	/** Muda o estado do LED (pisca) **/
	pin_toggle(LED_2_PIO, LED_2_IDX_MASK);  
}



void RTC_Handler(void) {
	uint32_t ul_status = rtc_get_status(RTC);
	
	/* seccond tick */
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {
	}
	
	/* Time or date alarm */
	if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
	}

	rtc_clear_status(RTC, RTC_SCCR_SECCLR);
	rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
	rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
	rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
	rtc_clear_status(RTC, RTC_SCCR_CALCLR);
	rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}


static void AFEC_pot_Callback(void) {
	adcData adc;
	adc.value = afec_channel_get_value(AFEC_POT, AFEC_POT_CHANNEL);
	BaseType_t xHigherPriorityTaskWoken = pdTRUE;
	xQueueSendFromISR(xQueueADC, &adc, &xHigherPriorityTaskWoken);
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/


static void task_adc(void *pvParameters) {
	config_AFEC_pot(AFEC_POT, AFEC_POT_ID, AFEC_POT_CHANNEL, AFEC_pot_Callback);
	TC_init(TC0, ID_TC1, 1, 1);
	tc_start(TC0, 1);
	
	/* Leitura do valor atual do RTC */
	uint32_t current_hour, current_min, current_sec;
	uint32_t current_year, current_month, current_day, current_week;

	// variável para recever dados da fila
	adcData adc;
	
	int i = 0;
	int j = 0;
	int alarme;

	while (1) {
		if (xQueueReceive(xQueueADC, &(adc), 1000)) {
			rtc_get_date(RTC, &current_year, &current_month, &current_day, &current_week);
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			printf("[AFEC ] %02d:%02d:%04d %02d:%02d:%02d %d \n", current_day, current_month, current_year, current_hour, current_min, current_sec, adc.value);
			if (adc.value > 3000){
				i++;
				if (i == 5){
					xSemaphoreGive(xSemaphoreAfecAlarm);
					alarme = 1;
					i = 0;
				}
			} if (adc.value < 1000 && alarme){
				j++;
				if (j == 10){
					xSemaphoreGive(xSemaphoreDeletaAlarme);
					alarme = 0;
					j = 0;
				}
			}
		} 
	}
}

static void task_event(void *pvParameters) {
	// variável para recever dados da fila
	adcDataBut but;
	int status_but1 = 0;
	int status_but2 = 0;
	int status_but3 = 0;
	
	/* Leitura do valor atual do RTC */
	uint32_t current_hour, current_min, current_sec;
	uint32_t current_year, current_month, current_day, current_week;

	while (1) {
		if (xQueueReceive(xQueueEvent, &(but), 1000)) {
			rtc_get_date(RTC, &current_year, &current_month, &current_day, &current_week);
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			if (but.but == 1){
				status_but1 = but.status;
			} if (but.but == 2){
				status_but2 = but.status;
			} if (but.but == 3){
				status_but3 = but.status;
			}
			
			if ((status_but1 && status_but2) || (status_but1 && status_but3) || (status_but2 && status_but3)){
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xSemaphoreGive(xSemaphoreEventAlarm);	
			}

			
			printf("[EVENT ] %02d:%02d:%04d %02d:%02d:%02d  %d: %d\n", current_day, current_month, current_year, current_hour, current_min, current_sec, but.but, but.status);
		} 
	}
}


static void task_alarm(void *pvParameters) {
	// variável para recever dados da fila
	
	/* Leitura do valor atual do RTC */
	uint32_t current_hour, current_min, current_sec;
	uint32_t current_year, current_month, current_day, current_week;

	while (1) {
		if (xSemaphoreTake(xSemaphoreAfecAlarm, 10 / portTICK_PERIOD_MS)) {
			rtc_get_date(RTC, &current_year, &current_month, &current_day, &current_week);
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			printf("[ALARM ] %02d:%02d:%04d %02d:%02d:%02d  AFEC\n", current_day, current_month, current_year, current_hour, current_min, current_sec);
			TC_init(TC1, ID_TC4, 1, 5);
			tc_start(TC1, 1);
		} if (xSemaphoreTake(xSemaphoreEventAlarm, 10 / portTICK_PERIOD_MS)) {
			rtc_get_date(RTC, &current_year, &current_month, &current_day, &current_week);
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			printf("[ALARM ] %02d:%02d:%04d %02d:%02d:%02d  EVENT\n", current_day, current_month, current_year, current_hour, current_min, current_sec);
			TC_init(TC0, ID_TC2, 2, 5);
			tc_start(TC0, 2);
		}
		if (xSemaphoreTake(xSemaphoreDeletaAlarme, 10 / portTICK_PERIOD_MS)){
			tc_stop(TC1, 1);
			pio_set(LED_1_PIO, LED_1_IDX_MASK);
		}
	}
}



static void task_oled(void *pvParameters) {
	gfx_mono_ssd1306_init();

	for (;;)  {
		gfx_mono_draw_filled_circle(12,12, 4, GFX_PIXEL_XOR, GFX_QUADRANT0| GFX_QUADRANT1 | GFX_QUADRANT2 | GFX_QUADRANT3);
		vTaskDelay(200);
	}
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

void pisca_led (int n, int t, Pio *pio, const uint32_t ul_mask) {
	for (int i=0;i<n;i++){
		pio_clear(pio, ul_mask);
		delay_ms(t);
		pio_set(pio, ul_mask);
		delay_ms(t);
	}
}

void pin_toggle(Pio *pio, uint32_t mask) {
	if(pio_get_output_data_status(pio, mask))
	pio_clear(pio, mask);
	else
	pio_set(pio,mask);
}


void TC_init(Tc *TC, int ID_TC, int TC_CHANNEL, int freq) {
	uint32_t ul_div;
	uint32_t ul_tcclks;
	uint32_t ul_sysclk = sysclk_get_cpu_hz();

	pmc_enable_periph_clk(ID_TC);

	tc_find_mck_divisor(freq, ul_sysclk, &ul_div, &ul_tcclks, ul_sysclk);
	tc_init(TC, TC_CHANNEL, ul_tcclks | TC_CMR_CPCTRG);
	tc_write_rc(TC, TC_CHANNEL, (ul_sysclk / ul_div) / freq);

	NVIC_SetPriority((IRQn_Type)ID_TC, 4);
	NVIC_EnableIRQ((IRQn_Type)ID_TC);
	tc_enable_interrupt(TC, TC_CHANNEL, TC_IER_CPCS);
}


void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type) {
	/* Configura o PMC */
	pmc_enable_periph_clk(ID_RTC);

	/* Default RTC configuration, 24-hour mode */
	rtc_set_hour_mode(rtc, 0);

	/* Configura data e hora manualmente */
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.second);

	/* Configure RTC interrupts */
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);

	/* Ativa interrupcao via alarme */
	rtc_enable_interrupt(rtc,  irq_type);
}


static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel,
                            afec_callback_t callback) {
  /*************************************
   * Ativa e configura AFEC
   *************************************/
  /* Ativa AFEC - 0 */
  afec_enable(afec);

  /* struct de configuracao do AFEC */
  struct afec_config afec_cfg;

  /* Carrega parametros padrao */
  afec_get_config_defaults(&afec_cfg);

  /* Configura AFEC */
  afec_init(afec, &afec_cfg);

  /* Configura trigger por software */
  afec_set_trigger(afec, AFEC_TRIG_SW);

  /*** Configuracao específica do canal AFEC ***/
  struct afec_ch_config afec_ch_cfg;
  afec_ch_get_config_defaults(&afec_ch_cfg);
  afec_ch_cfg.gain = AFEC_GAINVALUE_0;
  afec_ch_set_config(afec, afec_channel, &afec_ch_cfg);

  /*
  * Calibracao:
  * Because the internal ADC offset is 0x200, it should cancel it and shift
  down to 0.
  */
  afec_channel_set_analog_offset(afec, afec_channel, 0x200);

  /***  Configura sensor de temperatura ***/
  struct afec_temp_sensor_config afec_temp_sensor_cfg;

  afec_temp_sensor_get_config_defaults(&afec_temp_sensor_cfg);
  afec_temp_sensor_set_config(afec, &afec_temp_sensor_cfg);

  /* configura IRQ */
  afec_set_callback(afec, afec_channel, callback, 1);
  NVIC_SetPriority(afec_id, 4);
  NVIC_EnableIRQ(afec_id);
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		.charlength = CONF_UART_CHAR_LENGTH,
		.paritytype = CONF_UART_PARITY,
		.stopbits = CONF_UART_STOP_BITS,
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

void io_init(void) {
	pmc_enable_periph_clk(LED_1_PIO_ID);
	pmc_enable_periph_clk(LED_2_PIO_ID);
	pmc_enable_periph_clk(LED_3_PIO_ID);
	pmc_enable_periph_clk(BUT_1_PIO_ID);
	pmc_enable_periph_clk(BUT_2_PIO_ID);
	pmc_enable_periph_clk(BUT_3_PIO_ID);

	pio_configure(LED_1_PIO, PIO_OUTPUT_1, LED_1_IDX_MASK, PIO_DEFAULT);
	pio_configure(LED_2_PIO, PIO_OUTPUT_1, LED_2_IDX_MASK, PIO_DEFAULT);
	pio_configure(LED_3_PIO, PIO_OUTPUT_1, LED_3_IDX_MASK, PIO_DEFAULT);

	pio_configure(BUT_1_PIO, PIO_INPUT, BUT_1_IDX_MASK,
	PIO_PULLUP | PIO_DEBOUNCE);
	pio_configure(BUT_2_PIO, PIO_INPUT, BUT_2_IDX_MASK,
	PIO_PULLUP | PIO_DEBOUNCE);
	pio_configure(BUT_3_PIO, PIO_INPUT, BUT_3_IDX_MASK,
	PIO_PULLUP | PIO_DEBOUNCE);

	pio_handler_set(BUT_1_PIO, BUT_1_PIO_ID, BUT_1_IDX_MASK, PIO_IT_EDGE,
	but1_callback);
	pio_handler_set(BUT_2_PIO, BUT_2_PIO_ID, BUT_2_IDX_MASK, PIO_IT_EDGE,
	but2_callback);
	pio_handler_set(BUT_3_PIO, BUT_3_PIO_ID, BUT_3_IDX_MASK, PIO_IT_EDGE,
	but3_callback);

	pio_enable_interrupt(BUT_1_PIO, BUT_1_IDX_MASK);
	pio_enable_interrupt(BUT_2_PIO, BUT_2_IDX_MASK);
	pio_enable_interrupt(BUT_3_PIO, BUT_3_IDX_MASK);

	pio_get_interrupt_status(BUT_1_PIO);
	pio_get_interrupt_status(BUT_2_PIO);
	pio_get_interrupt_status(BUT_3_PIO);

	NVIC_EnableIRQ(BUT_1_PIO_ID);
	NVIC_SetPriority(BUT_1_PIO_ID, 4);

	NVIC_EnableIRQ(BUT_2_PIO_ID);
	NVIC_SetPriority(BUT_2_PIO_ID, 4);

	NVIC_EnableIRQ(BUT_3_PIO_ID);
	NVIC_SetPriority(BUT_3_PIO_ID, 4);
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/


int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();
	configure_console();
	io_init();
	
	calendar rtc_initial = {2018, 3, 19, 12, 15, 45 ,1};
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_SR_SEC|RTC_SR_ALARM);

	xSemaphoreAfecAlarm = xSemaphoreCreateBinary();
	if (xSemaphoreAfecAlarm == NULL)
		printf("falha em criar o semaforo \n");

	xSemaphoreDeletaAlarme = xSemaphoreCreateBinary();
	if (xSemaphoreDeletaAlarme == NULL)
		printf("falha em criar o semaforo \n");


	xSemaphoreEventAlarm = xSemaphoreCreateBinary();
	if (xSemaphoreEventAlarm == NULL)
		printf("falha em criar o semaforo \n");
	
	xQueueADC = xQueueCreate(100, sizeof(adcData));
	  if (xQueueADC == NULL)
		printf("falha em criar a queue xQueueADC \n");

	xQueueEvent = xQueueCreate(100, sizeof(adcDataBut));
	if (xQueueEvent == NULL)
		printf("falha em criar a queue xQueueEvent \n");

	if (xTaskCreate(task_oled, "oled", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create oled task\r\n");
	}
	
	if (xTaskCreate(task_adc, "ADC", TASK_ADC_STACK_SIZE, NULL, TASK_ADC_STACK_PRIORITY, NULL) != pdPASS) {
		  printf("Failed to create test ADC task\r\n");
	}

	if (xTaskCreate(task_event, "EVENT", TASK_EVENT_STACK_SIZE, NULL, TASK_EVENT_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create test ADC task\r\n");
	}	
	
	if (xTaskCreate(task_alarm, "ALARM", TASK_ALARM_STACK_SIZE, NULL, TASK_ALARM_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create test ADC task\r\n");
	}

	vTaskStartScheduler();
	while(1){}

	return 0;
}
