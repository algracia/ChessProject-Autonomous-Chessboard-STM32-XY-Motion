/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Alberto Gracia Martelo
 * @brief          : Este el main del proyecto final de tallerV
 ******************************************************************************

 ******************************************************************************
 */

#include <stdint.h>
#include <stm32f4xx.h>

#include "GPIOxDriver.h"
#include "BasicTimer.h"
#include "ExtiDriver.h"
#include "USARTxDriver.h"
#include "SysTickDriver.h"
#include "PwmDriver.h"

/*Macros utiles*/
#define SERVO_ARRIBA 		0
#define SERVO_ABAJO 		1
#define PASOS_SERVO			40
#define PASOSxCUADRO 		500
#define CAPTURA_NORMAL		0
#define CAPTURA_INVERTIDA	1

/*Configuramos los handlers*/
GPIO_Handler_t handlerOnBoardLed 			={0};
GPIO_Handler_t handlerSeñalM1				={0};
GPIO_Handler_t handlerSeñalM2				={0};
GPIO_Handler_t handlerDireccM1				={0};
GPIO_Handler_t handlerDireccM2				={0};
GPIO_Handler_t handlerEnableM1				={0};
GPIO_Handler_t handlerEnableM2				={0};
GPIO_Handler_t handlerSeñalServo			={0};
GPIO_Handler_t handlerEndStopX				={0};
GPIO_Handler_t handlerEndStopY				={0};
GPIO_Handler_t handlerPinTX					={0};
GPIO_Handler_t handlerPinRX					={0};

BasicTimer_Handler_t handlerTimerBlinky 	={0};

PWM_Handler_t handlerPwmM1					={0};
PWM_Handler_t handlerPwmM2					={0};
PWM_Handler_t handlerPwmServo				={0};

USART_Handler_t handlerUSART2				={0};

EXTI_Config_t handlerEXTIEndStopX			={0};
EXTI_Config_t handlerEXTIEndStopY			={0};


/*Variables*/

//Las relacionadas al USART
char USARTDataRecieved 			=0;
char bufferMsg[100] 			={0};
char recievedMsg[10]			={0};

//Las relacionadas a los pasos
uint16_t contadorPasosMotores	=0;
uint16_t contadorPasosServo		=0;
int16_t pasosEnXY[2] 			={0};
uint8_t conteoPosXArriba		=0;
uint8_t conteoPosXAbajo			=0;

//Las banderas
uint8_t endStopXFlag			=1;
uint8_t endStopYFlag			=1;
uint8_t iniciarJuego			=0;
uint8_t movDiagonal				=0;
uint8_t movCaballo				=0;
uint8_t enroque 				=0;

/*Headers de funciones*/
void InitHardware (void);
void Home(void);
void BasicMove (void);
void ControlServo(uint8_t posicionServo);
void MovX (int16_t n_pasos);
void MovY (int16_t n_pasos);
void MovDiagonal(int16_t n_pasosX,int16_t n_pasosY);
void MovCaballo(int16_t n_pasosX,int16_t n_pasosY);
void Enroque(int16_t n_pasosReyX,char *mensaje);
void Captura (char *mensaje, uint8_t tipo);
uint16_t PasosxFilaYColumna(char filaColumna);
void CalculoPasos (char *jugada,uint8_t etapa);
void recibirInstruccion(void);

int main(void) {

	InitHardware();

	//Bajamos el iman
	ControlServo(SERVO_ABAJO);

	/* Loop forever*/
	while (1) {

		/* Creamos un if que solo se ejecutara al comienzo de cada juego*/
		if(!iniciarJuego){

			/*Hacemos un pequeño movmiento para despegar el carro de los finales de carrera*/
			BasicMove();
			//desactivamos los enable de los drivers
			GPIO_WritePin(&handlerEnableM1, 0);
			GPIO_WritePin(&handlerEnableM2, 0);


			//Mantenemos un bucle mientras el usuario inicia el juego
			while(USARTDataRecieved != ' '){
				__NOP();
			}

			iniciarJuego = 1;

			Home();
		}


		/*Hacemos que el usuario ingrese su jugada en cada ciclo*/
		recibirInstruccion();

		/*Revisamos si se reinicio la partida*/
		if (recievedMsg[4]=='#' || recievedMsg[5]=='#' || recievedMsg[6]=='#'){
			//Bajamos la bandera de iniciar juego
			iniciarJuego = 0;

			//Volvemos a ejecutar el while (1)
			continue;
		}



		/*Analizamos la jugada ingresada y movemos la pieza*/
		/*Revisamos si hubo una captura normal y procesamos todo el movimiento*/
		Captura(recievedMsg, CAPTURA_NORMAL);


		/*Se inicia la ETAPA 1 de agarrar la pieza*/

		//Calculamos los pasos
		CalculoPasos(recievedMsg, 1);

		//Se realiza el movimiento basico*/
		BasicMove();

		//Se mueve primero en X y luego en Y

			//Movemos en X
			MovX(pasosEnXY[0]);

			//Movemos en Y
			MovY(pasosEnXY[1]);

		//Sujetamos la pieza
		ControlServo(SERVO_ARRIBA);


		/*Se inicia la ETAPA 2 de ubicar la pieza*/
		//Calculamos los pasos
		CalculoPasos(recievedMsg, 2);

		//Se analizar si el movimiento es de enroque
		Enroque(pasosEnXY[0], recievedMsg);

		//si el movimiento no fue de enroque, entonces
		//se analiza si fue de caballo
		if(!(enroque)){

			//Se analiza si el movimiento sera de un caballo
			MovCaballo(pasosEnXY[0], pasosEnXY[1]);

			//Si el movimiento no fue el de un caballo,
			//Se analiza si el movimiento sera en diagonal
			if(!(movCaballo)){
				MovDiagonal(pasosEnXY[0], pasosEnXY[1]);

				//Si el movimiento no fue en diagonal entonces
				//Se mueve primero en X y luego en Y
				if(!(movDiagonal)){
					//Movemos en X
					MovX(pasosEnXY[0]);

					//Movemos en Y
					MovY(pasosEnXY[1]);
				}
			}
		}

		//Soltamos la pieza
		ControlServo(SERVO_ABAJO);

		/*Hacemos que el carro vaya a home en cada ciclo*/
		Home();

		/*Si hubo una captura inversa, entonces, luego de
		 * regresar la pieza que 'come' a su posicion original
		 * se ejecuta la funcion que retorna la pieza 'comida'
		 */
		Captura(recievedMsg, CAPTURA_INVERTIDA);

		sprintf(bufferMsg,"\nMovimiento completado\n");
		writeMsg(&handlerUSART2, bufferMsg);


		writeChar(&handlerUSART2, '*');

	}
}

void InitHardware (void){
	/*Configuramos el Blinky*/
	handlerOnBoardLed.pGPIOx 								= GPIOB;
	handlerOnBoardLed.GPIO_PinConfig.GPIO_PinNumber			= PIN_10;
	handlerOnBoardLed.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_OUT;
	handlerOnBoardLed.GPIO_PinConfig.GPIO_PinOPType			= GPIO_OTYPE_PUSHPULL;
	handlerOnBoardLed.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerOnBoardLed.GPIO_PinConfig.GPIO_PinSpeed			= GPIO_OSPEED_FAST;

	handlerTimerBlinky.ptrTIMx								=TIM2;
	handlerTimerBlinky.TIMx_Config.TIMx_mode				=BTIMER_MODE_UP;
	handlerTimerBlinky.TIMx_Config.TIMx_speed				=BTIMER_SPEED_1ms;
	handlerTimerBlinky.TIMx_Config.TIMx_period				=250;
	handlerTimerBlinky.TIMx_Config.TIMx_interruptEnable		=BTIMER_INTERRUPT_ENABLE;

	GPIO_Config(&handlerOnBoardLed);
	BasicTimer_Config(&handlerTimerBlinky);


	/*Configuramos la Señal de los motores*/
	handlerSeñalM1.pGPIOx 								= GPIOB;
	handlerSeñalM1.GPIO_PinConfig.GPIO_PinNumber		= PIN_4;
	handlerSeñalM1.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_ALTFN;
	handlerSeñalM1.GPIO_PinConfig.GPIO_PinOPType		= GPIO_OTYPE_PUSHPULL;
	handlerSeñalM1.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerSeñalM1.GPIO_PinConfig.GPIO_PinSpeed			= GPIO_OSPEED_FAST;
	handlerSeñalM1.GPIO_PinConfig.GPIO_PinAltFunMode	= AF2;

	handlerSeñalM2.pGPIOx 								= GPIOA;
	handlerSeñalM2.GPIO_PinConfig.GPIO_PinNumber		= PIN_7;
	handlerSeñalM2.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_ALTFN;
	handlerSeñalM2.GPIO_PinConfig.GPIO_PinOPType		= GPIO_OTYPE_PUSHPULL;
	handlerSeñalM2.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerSeñalM2.GPIO_PinConfig.GPIO_PinSpeed			= GPIO_OSPEED_FAST;
	handlerSeñalM2.GPIO_PinConfig.GPIO_PinAltFunMode	= AF2;

	GPIO_Config(&handlerSeñalM1);
	GPIO_Config(&handlerSeñalM2);

	//Iniciamos la señal a un valor conocido
	GPIO_WritePin(&handlerSeñalM1, 1);
	GPIO_WritePin(&handlerSeñalM2, 1);

	//PWM de los motores
	handlerPwmM1.ptrTIMx				=TIM3;
	handlerPwmM1.config.channel 		=PWM_CHANNEL_1;
	handlerPwmM1.config.polarity 		=PWM_POLARITY_ACTIVE_LOW;
	handlerPwmM1.config.prescaler 		=PWM_PRESCALER_100us;
	handlerPwmM1.config.periodo 		=10; //Equivale a un periodo de 1ms
	handlerPwmM1.config.pulseWidth 		=5; //Equivale a un PW de 0.5 ms o un DutyCicle de 50%
	handlerPwmM1.config.interruption	=PWM_PERIOD_INTERRUPT_ENABLE;

	handlerPwmM2.ptrTIMx				=TIM3;
	handlerPwmM2.config.channel 		=PWM_CHANNEL_2;
	handlerPwmM2.config.polarity 		=PWM_POLARITY_ACTIVE_LOW;
	handlerPwmM2.config.prescaler 		=PWM_PRESCALER_100us;
	handlerPwmM2.config.periodo 		=10; //Equivale a un periodo de 1ms
	handlerPwmM2.config.pulseWidth 		=5; //Equivale a un PW de 0.5 ms o un DutyCicle de 50%
	handlerPwmM2.config.interruption	=PWM_PERIOD_INTERRUPT_ENABLE;


	//Cargamos las configuraciones
	pwm_Config(&handlerPwmM1);
	pwm_Config(&handlerPwmM2);

	//Activamos las señales,como para ambos motores es un mismo timer
	//basta con hacer esto para uno solo de los handlers
	startPwmSignal(&handlerPwmM1);

	//Dejamos las salidas deshabilitadas en un principio
	disableOutput(&handlerPwmM1);
	disableOutput(&handlerPwmM2);

	//Direccion motores
	handlerDireccM1.pGPIOx 								= GPIOB;
	handlerDireccM1.GPIO_PinConfig.GPIO_PinNumber		= PIN_5;
	handlerDireccM1.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_OUT;
	handlerDireccM1.GPIO_PinConfig.GPIO_PinOPType		= GPIO_OTYPE_PUSHPULL;
	handlerDireccM1.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerDireccM1.GPIO_PinConfig.GPIO_PinSpeed		= GPIO_OSPEED_FAST;

	handlerDireccM2.pGPIOx 								= GPIOA;
	handlerDireccM2.GPIO_PinConfig.GPIO_PinNumber		= PIN_12;
	handlerDireccM2.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_OUT;
	handlerDireccM2.GPIO_PinConfig.GPIO_PinOPType		= GPIO_OTYPE_PUSHPULL;
	handlerDireccM2.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerDireccM2.GPIO_PinConfig.GPIO_PinSpeed		= GPIO_OSPEED_FAST;

	GPIO_Config(&handlerDireccM1);
	GPIO_Config(&handlerDireccM2);

	//Enable de los motores
	handlerEnableM1.pGPIOx 								= GPIOC;
	handlerEnableM1.GPIO_PinConfig.GPIO_PinNumber		= PIN_4;
	handlerEnableM1.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_OUT;
	handlerEnableM1.GPIO_PinConfig.GPIO_PinOPType		= GPIO_OTYPE_PUSHPULL;
	handlerEnableM1.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerEnableM1.GPIO_PinConfig.GPIO_PinSpeed		= GPIO_OSPEED_FAST;

	handlerEnableM2.pGPIOx 								= GPIOA;
	handlerEnableM2.GPIO_PinConfig.GPIO_PinNumber		= PIN_8;
	handlerEnableM2.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_OUT;
	handlerEnableM2.GPIO_PinConfig.GPIO_PinOPType		= GPIO_OTYPE_PUSHPULL;
	handlerEnableM2.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerEnableM2.GPIO_PinConfig.GPIO_PinSpeed		= GPIO_OSPEED_FAST;

	GPIO_Config(&handlerEnableM1);
	GPIO_Config(&handlerEnableM2);

	//Iniciamos la señal a un valor conocido
	GPIO_WritePin(&handlerEnableM1, 0); //un cero aqui, deshabilita los drivers
	GPIO_WritePin(&handlerEnableM2, 0);


	/*Configuramos la señal del Servomotor*/
	handlerSeñalServo.pGPIOx 								= GPIOB;
	handlerSeñalServo.GPIO_PinConfig.GPIO_PinNumber			= PIN_9;
	handlerSeñalServo.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_ALTFN;
	handlerSeñalServo.GPIO_PinConfig.GPIO_PinOPType			= GPIO_OTYPE_PUSHPULL;
	handlerSeñalServo.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;
	handlerSeñalServo.GPIO_PinConfig.GPIO_PinSpeed			= GPIO_OSPEED_FAST;
	handlerSeñalServo.GPIO_PinConfig.GPIO_PinAltFunMode		= AF2;

	GPIO_Config(&handlerSeñalServo);

	//PWM del Servo
	handlerPwmServo.ptrTIMx					=TIM4;
	handlerPwmServo.config.channel 			=PWM_CHANNEL_4;
	handlerPwmServo.config.polarity 		=PWM_POLARITY_ACTIVE_LOW;
	handlerPwmServo.config.prescaler 		=PWM_PRESCALER_100us;
	handlerPwmServo.config.periodo 			=200; //Equivale a un periodo de 20ms
	handlerPwmServo.config.pulseWidth 		=15;
	handlerPwmServo.config.interruption		=PWM_PERIOD_INTERRUPT_ENABLE;

	//Cargamos la configuracion
	pwm_Config(&handlerPwmServo);

	//Activamos la señal
	startPwmSignal(&handlerPwmServo);

	//Dejamos la salida deshabilitadas en un principio
	disableOutput(&handlerPwmServo);


	/*Configuramos la comunicacion serial*/
	//Configuramos pines para la comunicacion serial
	handlerPinTX.pGPIOx 								= GPIOA;
	handlerPinTX.GPIO_PinConfig.GPIO_PinNumber 			= PIN_2;
	handlerPinTX.GPIO_PinConfig.GPIO_PinMode 			= GPIO_MODE_ALTFN;
	handlerPinTX.GPIO_PinConfig.GPIO_PinAltFunMode 		= AF7;

	handlerPinRX.pGPIOx 								= GPIOA;
	handlerPinRX.GPIO_PinConfig.GPIO_PinNumber 			= PIN_3;
	handlerPinRX.GPIO_PinConfig.GPIO_PinMode 			= GPIO_MODE_ALTFN;
	handlerPinRX.GPIO_PinConfig.GPIO_PinAltFunMode 		= AF7;

	//Cargamos las configuraciones
	GPIO_Config(&handlerPinTX);
	GPIO_Config(&handlerPinRX);

	//Configuramos el USART
	handlerUSART2.ptrUSARTx 						= USART2;
	handlerUSART2.USART_Config.USART_baudrate 		= USART_BAUDRATE_115200;
	handlerUSART2.USART_Config.USART_datasize 		= USART_DATASIZE_8BIT;
	handlerUSART2.USART_Config.USART_mode			= USART_MODE_RXTX;
	handlerUSART2.USART_Config.USART_parity 		= USART_PARITY_NONE;
	handlerUSART2.USART_Config.USART_stopbits 		= USART_STOPBIT_1;
	handlerUSART2.USART_Config.USART_enableIntRX 	= USART_RX_INTERRUP_ENABLE;
	handlerUSART2.USART_Config.USART_enableIntTX 	= USART_TX_INTERRUP_DISABLE;

	USART_Config(&handlerUSART2);


	/*Configuramos los EXTI para los finales de carrera*/

	//Configuramos los pines como entradas
	handlerEndStopX.pGPIOx								= GPIOA;
	handlerEndStopX.GPIO_PinConfig.GPIO_PinNumber		= PIN_1;
	handlerEndStopX.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_IN;
	handlerEndStopX.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;

	handlerEndStopY.pGPIOx								= GPIOA;
	handlerEndStopY.GPIO_PinConfig.GPIO_PinNumber		= PIN_4;
	handlerEndStopY.GPIO_PinConfig.GPIO_PinMode			= GPIO_MODE_IN;
	handlerEndStopY.GPIO_PinConfig.GPIO_PinPuPdControl	= GPIO_PUPDR_NOTHING;

	//Configuramos los handler del EXTI
	handlerEXTIEndStopX.pGPIOHandler	=&handlerEndStopX;
	handlerEXTIEndStopX.edgeType		=EXTERNAL_INTERRUPT_RISING_EDGE;

	handlerEXTIEndStopY.pGPIOHandler	=&handlerEndStopY;
	handlerEXTIEndStopY.edgeType		=EXTERNAL_INTERRUPT_RISING_EDGE;

	//Cargamos las configuraciones
	ExtInt_Config(&handlerEXTIEndStopX);
	ExtInt_Config(&handlerEXTIEndStopY);

	/*Configuramos el Systick*/
	config_SysTick_ms(HSI_CLOCK_CONFIGURED);
}//Fin funcion InitHardware


void Home(void){

	/*Primero paramos todo por seguridad*/
	disableOutput(&handlerPwmM1);
	disableOutput(&handlerPwmM2);

	/*Activamos los enable de los drivers*/
	GPIO_WritePin(&handlerEnableM1, 1);
	GPIO_WritePin(&handlerEnableM2, 1);

	/*Bajamos la bandera en X para
	que inicie el movimiento en esta direccion.*/
	endStopXFlag =0;

	/*Hacemos que se mueva a la izquierda*/
	GPIO_WritePin(&handlerDireccM1, 1); //CW
	GPIO_WritePin(&handlerDireccM2, 1); //CW

	enableOutput(&handlerPwmM1);
	enableOutput(&handlerPwmM2);

	//Mientras no cambie la bandera, el se mantiene en un bucle
	while(!endStopXFlag){
		__NOP();
	}

	/*Ahora, bajamos la bandera en Y para
	que inicie el movimiento en esta direccion.*/
	endStopYFlag =0;

	/*Hacemos que se mueva hacia abajo*/
	GPIO_WritePin(&handlerDireccM1, 1); //CW
	GPIO_WritePin(&handlerDireccM2, 0); //CCW

	//Mientras no cambie la bandera, el se mantiene en un bucle
	while(!endStopYFlag){
		__NOP();
	}

	/*Desactivamos el movimiento*/
	disableOutput(&handlerPwmM1);
	disableOutput(&handlerPwmM2);

	/*desactivamos los enable de los drivers*/
	GPIO_WritePin(&handlerEnableM1, 0);
	GPIO_WritePin(&handlerEnableM2, 0);


}//Fin funcion Home


void BasicMove (void) {

	/*Ahora, hacemos que se mueva hacia arriba
	y a partir de esta posicion, va a estar nuestro cero*/

	//Activamos los enable de los drivers
	GPIO_WritePin(&handlerEnableM1, 1);
	GPIO_WritePin(&handlerEnableM2, 1);

	//Ajustamos la direccion
	GPIO_WritePin(&handlerDireccM1, 0); //CCW
	GPIO_WritePin(&handlerDireccM2, 1); //CW

	//Habilitamos las salidas de los PWM
	enableOutput(&handlerPwmM1);
	enableOutput(&handlerPwmM2);

	//Inicializamos el contador de pasos
	contadorPasosMotores =0;

	/*Hacemos que se quede en un bucle hasta que se
	complete el numero de pasos*/
	while(!(contadorPasosMotores > 385)){
		__NOP();
	}

	/*Ahora hacemos que se desplace en diagonal hacia
	 *el centro del cuadro A1*/

	//deshabilitamos la salida PWM del motor que no se mueve
	disableOutput(&handlerPwmM2);

	//Ajustamos la direccion
	GPIO_WritePin(&handlerDireccM1, 0); //CW

	//Hacemos que se desplaze 1 cuadro en diagonal
	//Usando nuevamente un bucle

	//Inicializamos el contador de pasos
	contadorPasosMotores =0;

	while(!(contadorPasosMotores > PASOSxCUADRO)){
		__NOP();
	}

	//Detenemos todo
	disableOutput(&handlerPwmM1);
	disableOutput(&handlerPwmM2);

}//Fin funcion BasicMove

void MovX (int16_t n_pasos){

	if(n_pasos >= 0){

		/*se mueve hacia la derecha*/
		//Ajustamos la direccion
		GPIO_WritePin(&handlerDireccM1, 0); //CCW
		GPIO_WritePin(&handlerDireccM2, 0); //CCW

		//Habilitamos las salidas de los PWM
		enableOutput(&handlerPwmM1);
		enableOutput(&handlerPwmM2);

		//Reiniciamos el contador de pasos
		contadorPasosMotores =0;

		//hacemos un bucle hasta que se llegue
		//al destino
		while(!(contadorPasosMotores >= n_pasos)){
			__NOP();
		}

		//Deshabilitamos las salidas de los PWM
		disableOutput(&handlerPwmM1);
		disableOutput(&handlerPwmM2);

	}
	else{

		/*se mueve hacia la izquierda*/
		//hacemos que el numero de pasos vuelva a ser positivo
		n_pasos = -n_pasos;

		//Ajustamos la direccion
		GPIO_WritePin(&handlerDireccM1, 1); //CW
		GPIO_WritePin(&handlerDireccM2, 1); //CW

		//Habilitamos las salidas de los PWM
		enableOutput(&handlerPwmM1);
		enableOutput(&handlerPwmM2);

		//Reiniciamos el contador de pasos
		contadorPasosMotores =0;

		//hacemos un bucle hasta que se llegue
		//al destino
		while(!(contadorPasosMotores >= n_pasos)){
			__NOP();
		}

		//Deshabilitamos las salidas de los PWM
		disableOutput(&handlerPwmM1);
		disableOutput(&handlerPwmM2);

	}
}//Fin funcion MovX

void MovY(int16_t n_pasos){


	if(n_pasos >= 0){

		/*se mueve hacia arriba*/
		//Ajustamos la direccion
		GPIO_WritePin(&handlerDireccM1, 0); //CCW
		GPIO_WritePin(&handlerDireccM2, 1); //CW

		//Habilitamos las salidas de los PWM
		enableOutput(&handlerPwmM1);
		enableOutput(&handlerPwmM2);

		//Reiniciamos el contador de pasos
		contadorPasosMotores =0;

		//hacemos un bucle hasta que se llegue
		//al destino
		while(!(contadorPasosMotores >= n_pasos)){
			__NOP();
		}

		//Deshabilitamos las salidas de los PWM
		disableOutput(&handlerPwmM1);
		disableOutput(&handlerPwmM2);

	}
	else{

		/*se mueve hacia abajo*/
		//hacemos que el numero de pasos vuelva a ser positivo
		n_pasos = -n_pasos;

		//Ajustamos la direccion
		GPIO_WritePin(&handlerDireccM1, 1); //CW
		GPIO_WritePin(&handlerDireccM2, 0); //CCW

		//Habilitamos las salidas de los PWM
		enableOutput(&handlerPwmM1);
		enableOutput(&handlerPwmM2);

		//Reiniciamos el contador de pasos
		contadorPasosMotores =0;

		//hacemos un bucle hasta que se llegue
		//al destino
		while(!(contadorPasosMotores >= n_pasos)){
			__NOP();
		}

		//Deshabilitamos las salidas de los PWM
		disableOutput(&handlerPwmM1);
		disableOutput(&handlerPwmM2);

	}
}//Fin funcion MovY


void MovDiagonal(int16_t n_pasosX,int16_t n_pasosY){

	/*Comparamos el numero de pasos X y en Y y si
	 * son iguales, significa que el movimiento se
	 * da en una diagonal.
	 *
	 * Esto implica que n_pasosX/n_pasosY = +-1
	 */

	uint16_t pasosD =0;

	if(n_pasosX/n_pasosY == 1 || n_pasosX/n_pasosY == -1 ){

		if(n_pasosX >0 && n_pasosY >0){

			/*se mueve en diagonal derecha arriba*/
			//Ajustamos la direccion
			GPIO_WritePin(&handlerDireccM1, 0); //CCW

			//Habilitamos las salida del PWM
			//solo del motor que se mueve y
			//deshabilitamos la del otro
			enableOutput(&handlerPwmM1);
			disableOutput(&handlerPwmM2);

			//Reiniciamos el contador de pasos
			contadorPasosMotores =0;

			//hacemos un bucle hasta que se llegue
			//al destino
			pasosD = (2*n_pasosX) + PASOSxCUADRO;
			while(!(contadorPasosMotores >= pasosD )){
				__NOP();
			}

			//Deshabilitamos las salidas de los PWM
			disableOutput(&handlerPwmM1);
			disableOutput(&handlerPwmM2);
		}

		else if(n_pasosX <0 && n_pasosY >0){

			/*se mueve en diagonal izquierda arriba*/
			//Ajustamos la direccion
			GPIO_WritePin(&handlerDireccM2, 1); //CW

			//Habilitamos las salida del PWM
			//solo del motor que se mueve y
			//deshabilitamos la del otro
			disableOutput(&handlerPwmM1);
			enableOutput(&handlerPwmM2);

			//Reiniciamos el contador de pasos
			contadorPasosMotores =0;

			//hacemos un bucle hasta que se llegue
			//al destino
			pasosD = (2*n_pasosY);
			while(!(contadorPasosMotores >= pasosD)){
				__NOP();
			}

			//Deshabilitamos las salidas de los PWM
			disableOutput(&handlerPwmM1);
			disableOutput(&handlerPwmM2);
		}

		else if(n_pasosX >0 && n_pasosY <0){

			/*se mueve en diagonal izquierda abajo*/
			//Ajustamos la direccion
			GPIO_WritePin(&handlerDireccM2, 0); //CCW

			//Habilitamos las salida del PWM
			//solo del motor que se mueve y
			//deshabilitamos la del otro
			disableOutput(&handlerPwmM1);
			enableOutput(&handlerPwmM2);

			//Reiniciamos el contador de pasos
			contadorPasosMotores =0;

			//hacemos un bucle hasta que se llegue
			//al destino
			pasosD = (2*n_pasosX);
			while(!(contadorPasosMotores >= pasosD)){
				__NOP();
			}

			//Deshabilitamos las salidas de los PWM
			disableOutput(&handlerPwmM1);
			disableOutput(&handlerPwmM2);
		}

		else if(n_pasosX <0 && n_pasosY <0){

			/*se mueve en diagonal derecha abajo*/
			//Ajustamos la direccion
			GPIO_WritePin(&handlerDireccM1, 1); //CW

			//Habilitamos las salida del PWM
			//solo del motor que se mueve y
			//deshabilitamos la del otro
			enableOutput(&handlerPwmM1);
			disableOutput(&handlerPwmM2);

			//Reiniciamos el contador de pasos
			contadorPasosMotores =0;

			//hacemos un bucle hasta que se llegue
			//al destino
			pasosD = -(2*n_pasosX) + PASOSxCUADRO;
			while(!(contadorPasosMotores >= pasosD)){
				__NOP();
			}

			//Deshabilitamos las salidas de los PWM
			disableOutput(&handlerPwmM1);
			disableOutput(&handlerPwmM2);
		}

		movDiagonal =1;
	}
	else{
		movDiagonal =0;
	}

}//Fin funcion MovDiagonal

void MovCaballo(int16_t n_pasosX,int16_t n_pasosY){
	/*Si se va a mover un caballo, se cumple que n_pasosX = 2*n_pasosY
	 * o n_pasosY = 2*n_pasosX
	 * entonces, si n_pasosX/n_pasosY = +-2 o n_pasosY/n_pasosX = +-2,
	 * se garantiza que se mueve el caballo y es posible saber su direccion*/

	//Realizamos las comparaciones
	if(n_pasosX/n_pasosY == 2 || n_pasosX/n_pasosY == -2 ){
		//el mov sera en una L horizontal

		//Primero nos movemos en Y la mitad de los pasos
		//para asi desplazarse entre los bordes de los cuadros
		MovY(n_pasosY/2);

		//Luego, nos movemos la cantidad de pasos solicitado en X
		MovX(n_pasosX);

		//Finalmente nos movemos en Y el resto de pasos
		MovY(n_pasosY/2);

		//Levantamos la bandera que indica que efectivamente hubo un movimiento de caballo
		movCaballo =1;

	}
	else if(n_pasosY/n_pasosX == 2 || n_pasosY/n_pasosX == -2 ){
		//el mov sera en una L vertical

		//Primero nos movemos en X la mitad de los pasos
		//para asi desplazarse entre los bordes de los cuadros
		MovX(n_pasosX/2);

		//Luego, nos movemos la cantidad de pasos solicitado en Y
		MovY(n_pasosY);

		//Finalmente nos movemos en X el resto de pasos
		MovX(n_pasosX/2);

		//Levantamos la bandera que indica que efectivamente hubo un movimiento de caballo
		movCaballo =1;

	}
	else{
		movCaballo =0;
	}
}//Fin funcion MovCaballo

void Enroque(int16_t n_pasosReyX,char *mensaje){

	/*El enroque se da si en el mensaje enviado por python
	 * se encuentra el simbolo & para enroque corto y
	 * @ para enroque largo
	 */

	if(mensaje[5] != '-' && mensaje[5] != '#'){
		//Estamos en la funcion de enroque normal

		if (mensaje[4] == '&'){
			//Estamos en enroque corto

			/*Primero debemos mover el rey*/

			//lo movemos la cantidad de pasos
			//solicitados en X
			MovX(n_pasosReyX);

			//Soltamos al rey
			ControlServo(SERVO_ABAJO);

			/*Ahora, movemos la torre*/
			//Llegamos hasta ella
			MovX(PASOSxCUADRO);

			//La sujetamos
			ControlServo(SERVO_ARRIBA);

			//Bajamos con ella media casilla
			MovY(-PASOSxCUADRO/2);

			//Nos desplazamos con ella hacia f1
			MovX(-2*PASOSxCUADRO);

			//La subimos medio cuadro
			MovY(PASOSxCUADRO/2);

			/*Subimos la bandera de enroque*/
			enroque =1;

		}
		else if(mensaje[4] == '@'){
			//Estamos en enroque largo

			/*Primero debemos mover el rey*/

			//lo movemos la cantidad de pasos
			//solicitados en X
			MovX(n_pasosReyX);

			//Soltamos al rey
			ControlServo(SERVO_ABAJO);

			/*Ahora, movemos la torre*/
			//Llegamos hasta ella
			MovX(-2*PASOSxCUADRO);

			//La sujetamos
			ControlServo(SERVO_ARRIBA);

			//Bajamos con ella media casilla
			MovY(-PASOSxCUADRO/2);

			//Nos desplazamos con ella hacia d1
			MovX(3*PASOSxCUADRO);

			//La subimos medio cuadro
			MovY(PASOSxCUADRO/2);

			/*Subimos la bandera de enroque*/
			enroque =1;


		}
		else{
			enroque =0;
		}
	}
	else if (mensaje[5] == '-'){
		//Estamos en enroque invertido

		if (mensaje[4] == '&'){
			//Estamos en enroque corto

			/*Primero debemos mover el rey*/
			//y lo hacemos media casilla hacia arriba
			MovY(PASOSxCUADRO/2);

			//Luego, lo movemos la cantidad de pasos
			//solicitados en X
			MovX(n_pasosReyX);

			//Finalmente, bajamos al rey media casilla
			MovY(-PASOSxCUADRO/2);

			//Soltamos al rey
			ControlServo(SERVO_ABAJO);

			/*Ahora, movemos la torre*/
			//Llegamos hasta ella
			MovX(PASOSxCUADRO);

			//La sujetamos
			ControlServo(SERVO_ARRIBA);

			//Nos desplazamos con ella hacia h1
			MovX(2*PASOSxCUADRO);

			/*Subimos la bandera de enroque*/
			enroque =1;

		}
		else if(mensaje[4] == '@'){
			//Estamos en enroque largo

			/*Primero debemos mover el rey*/
			//y lo hacemos media casilla hacia arriba
			MovY(PASOSxCUADRO/2);

			//Luego, lo movemos la cantidad de pasos
			//solicitados en X
			MovX(n_pasosReyX);

			//Finalmente, bajamos al rey media casilla
			MovY(-PASOSxCUADRO/2);

			//Soltamos al rey
			ControlServo(SERVO_ABAJO);

			/*Ahora, movemos la torre*/
			//Llegamos hasta ella
			MovX(-PASOSxCUADRO);

			//La sujetamos
			ControlServo(SERVO_ARRIBA);

			//Nos desplazamos con ella hacia a1
			MovX(-3*PASOSxCUADRO);

			/*Subimos la bandera de enroque*/
			enroque =1;

		}
		else{
			enroque =0;
		}
	}
}//Fin funcion Enroque


void Captura (char *mensaje, uint8_t tipo){

	int16_t pasosX 	=0;
	int16_t pasosY 	=0;
	int16_t posY	=0;
	int16_t posX	=0;

	if(mensaje[4] == 'x'){
		//Hay una captura

		if(conteoPosXAbajo < 0){
			conteoPosXAbajo =0;

		}

		if (conteoPosXArriba<0){
			conteoPosXArriba =0;
		}

		switch(tipo){

		case CAPTURA_NORMAL: {

			if(mensaje[5] != '-' && mensaje[5] != '#'){
				//Estamos en una captura normal

				/*Primero, hacemos que el iman llegue hacia
				 * donde se ubica la pieza a comer, la cual esta
				 * en la posicion final de pieza de la jugada normal
				 * */

				//Hacemos el basicMove
				BasicMove();

				//calculamos el numero de pasos en X
				pasosX = PasosxFilaYColumna(mensaje[2]);

				//Ahora, calculamos el numero de pasos en Y
				pasosY = PasosxFilaYColumna(mensaje[3]);

				//Nos movemos primero en X
				MovX(pasosX);

				//Luego, nos movemos en Y
				MovY(pasosY);

				//Sujetamos la pieza
				ControlServo(SERVO_ARRIBA);

				//La desplazamos media casilla hacia la derecha
				MovX(PASOSxCUADRO/2);

				//Revisamos si la pieza esta en la parte mas alta
				//o mas baja del tablero
				if(pasosY >= 4*PASOSxCUADRO){
					//Esta en la parte mas alta

					//Calculamos los pasos en Y y movemos
					//justo hasta el borde del tablero
					posY = 8*PASOSxCUADRO - pasosY - PASOSxCUADRO/2;

					//Desplazamos la pieza hasta lo mas alto posible en y
					MovY(posY);

					//Ahora miramos cuanto se debe mover en x
					posX = ((conteoPosXArriba-1)*PASOSxCUADRO/2) - pasosX;

					//Desplazamos esa pieza hasta esa posicion
					MovX(posX);

					//La desplazamos media casilla hasta donde debe estar
					MovY(PASOSxCUADRO/2);

					//La soltamos
					ControlServo(SERVO_ABAJO);

					//Aumentamos en 1 el contador de piezas arriba
					conteoPosXArriba++;

					//Volvemos a home
					Home();

				}
				else{
					//Esta es la parte mas baja

					//Calculamos los pasos en Y y movemos
					//justo hasta el borde del tablero
					posY = -PASOSxCUADRO -pasosY + PASOSxCUADRO/2;

					//Desplazamos la pieza hasta lo mas bajo posible en y
					MovY(posY);

					//Ahora miramos cuanto se debe mover en x
					posX = ((conteoPosXAbajo-1)*PASOSxCUADRO/2) - pasosX;

					//Desplazamos esa pieza hasta esa posicion
					MovX(posX);

					//La desplazamos media casilla hasta donde debe estar
					MovY(-PASOSxCUADRO/2);

					//La soltamos
					ControlServo(SERVO_ABAJO);

					//Aumentamos en 1 el contador de piezas abajo
					conteoPosXAbajo++;

					//Volvemos a home
					Home();

				}
				}

			break;
			}
		case CAPTURA_INVERTIDA:{

			if(mensaje[5] == '-'){

				uint16_t casillaX =0;
				uint16_t casillaY =0;

				//Estamos en una captura invertida

				/*Primero, hacemos que el iman llegue hacia
				 * donde se ubica la pieza a 'des-comer', la cual esta
				 * en la posicion final arriba o abajo del tablero
				 * */

				//Pero para verificar si la pieza esta arriba o abajo,
				//debemos mirar el numero de pasos respecto a la casilla
				//inicial de la ficha a retornar

				//calculamos las posiciones de la casilla que va a ocupar
				//la pieza 'des-comida'
				casillaX = PasosxFilaYColumna(mensaje[0]);
				casillaY = PasosxFilaYColumna(mensaje[1]);

				//Hacemos el basicMove
				BasicMove();

				if (casillaY >= 4*PASOSxCUADRO){
					//Esta en la parte mas alta

					//Calculamos la posicion en Y
					posY = 8*PASOSxCUADRO;

					//Movemos en Y
					MovY(posY);

					//Calculamos la posicion en X
					posX = (conteoPosXArriba -1)*PASOSxCUADRO/2;

					//Movemos en X
					MovX(posX);

					//Sujetamos la pieza
					ControlServo(SERVO_ARRIBA);

					//La desplazamos hacia abajo media casilla
					MovY(-PASOSxCUADRO/2);

					//Calculamos la cantidad de pasos que se va a mover
					//para llegar al destino

					pasosX = casillaX - posX + (PASOSxCUADRO/2);

					//Movemos en x
					MovX(pasosX);

					//Calculamos la cantidad de pasos en Y
					pasosY = casillaY - posY + PASOSxCUADRO/2;

					//Movemos en Y
					MovY(pasosY);

					//Movemos la pieza medio cuadro hacia la izquierda
					MovX(-PASOSxCUADRO/2);

					//Soltamos la pieza
					ControlServo(SERVO_ABAJO);

					//disminuimos el contador piezas arriba
					conteoPosXArriba-- ;

					//Volvemos a Home
					Home();


				}

				else{
					//Estamos en la parte mas baja

					//Calculamos la posicion en Y
					posY = -PASOSxCUADRO;

					//Movemos en Y
					MovY(posY);

					//Calculamos la posicion en X
					posX = (conteoPosXAbajo -1)*PASOSxCUADRO/2;

					//Desplazamos hasta esa posicion
					MovX(posX);

					//Sujetamos la pieza
					ControlServo(SERVO_ARRIBA);

					//La desplazamos hacia arriba media casilla
					MovY(PASOSxCUADRO/2);

					//Calculamos la cantidad de pasos que se va a mover
					//para llegar al destino

					pasosX = casillaX - posX + (PASOSxCUADRO/2);

					//Movemos en x
					MovX(pasosX);

					//Calculamos la cantidad de pasos en Y
					pasosY = 390 + casillaY - (PASOSxCUADRO/2);

					//Movemos en Y
					MovY(pasosY);

					//Movemos la pieza medio cuadro hacia la izquierda
					MovX(-PASOSxCUADRO/2);

					//Soltamos la pieza
					ControlServo(SERVO_ABAJO);

					//disminuimos en una unidad el contador de piezas abajo
					conteoPosXAbajo--;

					//Volvemos a Home
					Home();

				}

			}//Fin del 'if' que valida la captura invertida

			break;
		}
		default:{
			__NOP();
			break;
		}

		}//Fin de switch

	}//Fin del 'if' que valida la funcion

}//Fin de la funcion Captura


uint16_t PasosxFilaYColumna (char filaColumna){
	/*Esta funcion va a recibir el caracter que representa
	 * una fila o una columna y devolvera el numero de pasos
	 * necesario para llegar a ella desde nuestro cero
	 */

	uint16_t pasos=0;

	switch(filaColumna){

	/*Primero para las columnas*/
	case 'a':{
		pasos = 0;
		break;
	}
	case 'b':{
		pasos = PASOSxCUADRO;
		break;
	}
	case 'c':{
		pasos = 2*PASOSxCUADRO;
		break;
	}
	case 'd':{
		pasos = 3*PASOSxCUADRO;
		break;
	}
	case 'e':{
		pasos = 4*PASOSxCUADRO;
		break;
	}
	case 'f':{
		pasos = 5*PASOSxCUADRO;
		break;
	}
	case 'g':{
		pasos = 6*PASOSxCUADRO;
		break;
	}
	case 'h':{
		pasos = 7*PASOSxCUADRO;
		break;
	}

	/*Ahora para las filas*/
	case '1':{
		pasos = 0;
		break;
	}
	case '2':{
		pasos = PASOSxCUADRO;
		break;
	}
	case '3':{
		pasos = 2*PASOSxCUADRO;
		break;
	}
	case '4':{
		pasos = 3*PASOSxCUADRO;
		break;
	}
	case '5':{
		pasos = 4*PASOSxCUADRO;
		break;
	}
	case '6':{
		pasos = 5*PASOSxCUADRO;
		break;
	}
	case '7':{
		pasos = 6*PASOSxCUADRO;
		break;
	}
	case '8':{
		pasos = 7*PASOSxCUADRO;
		break;
	}
	default:{
		pasos =0;

		sprintf(bufferMsg,"Tal parece que no ingresó un caracter valido");
		writeMsg(&handlerUSART2, bufferMsg);

		sprintf(bufferMsg,"\nintente nuevamente");
		writeMsg(&handlerUSART2, bufferMsg);
		break;
	}
	}
	return pasos;
}

void CalculoPasos (char *jugada,uint8_t etapa){
	/*Esta funcion nos va a permitir analizar el string con
	 * la jugada en notacion algebraica.
	 *
	 * Sabiendo que:
	 *
	 * jugada = {columnaInicial,filaInicial,columnaFinal,filaFinal,"flag" de enroque o captura,
	 * "flag" de juagada invertida o Jaque Mate,"flag" de Jaque Mate,'\0'}
	 */

	/*Se divide en dos etapas. En la primera, calcula los pasos para llegar
	 * a la casilla donde se ecuentra la pieza que se va mover y en la segunda,
	 * mueve dicha pieza hacia su posicion final
	 */

	switch(etapa){

	case 1:{
		//Calculamos el numero de pasos en X para llegar a la columna
		//indicada en la jugada
		pasosEnXY[0] = PasosxFilaYColumna(jugada[0]);

		//Ahora, calculamos el numero de pasos en Y
		pasosEnXY[1] = PasosxFilaYColumna(jugada[1]);

		break;
	}
	case 2:{
		//Calculamos el numero de pasos en X para llegar a la columna
		//indicada en la segunda parte de la jugada
		pasosEnXY[0] =  PasosxFilaYColumna(jugada[2]) - PasosxFilaYColumna(jugada[0]);

		//Ahora, calculamos el numero de pasos en Y
		pasosEnXY[1] = PasosxFilaYColumna(jugada[3]) - PasosxFilaYColumna(jugada[1]);

		break;
	}
	default:{
		pasosEnXY[0] =0;
		pasosEnXY[1]=0;
		break;
	}
	}

}//Fin funcion CalcularPasos

void ControlServo(uint8_t posicionServo){

	switch(posicionServo){

	case SERVO_ARRIBA:{
		//Hacemos que el servo suba
		updatePulseWidth(&handlerPwmServo, 16);

		//Habilitamos el movimiento
		enableOutput(&handlerPwmServo);

		//Reiniciamos la variable para los pasos
		contadorPasosServo =0;

		//hacemos un bucle hasta que se llegue
		//al destino
		while(!(contadorPasosServo > PASOS_SERVO )){
			__NOP();
		}

		//deshabilitamos el movimiento
		disableOutput(&handlerPwmServo);

		break;
	}

	case SERVO_ABAJO:{
		//Hacemos que el servo baje
		updatePulseWidth(&handlerPwmServo, 14);

		//Habilitamos el movimiento
		enableOutput(&handlerPwmServo);

		//Reiniciamos la variable para los pasos
		contadorPasosServo =0;

		//hacemos un bucle hasta que se llegue
		//al destino
		while(!(contadorPasosServo > PASOS_SERVO )){
			__NOP();
		}

		//deshabilitamos el movimiento
		disableOutput(&handlerPwmServo);

		break;
	}

	default:{
		__NOP();
	}
	}

}

void recibirInstruccion(void){

//	sprintf(bufferMsg,"\nSTM listo para recibir el nuevo movimiento\n");
//	writeMsg(&handlerUSART2, bufferMsg);

	uint8_t counter =0;
	USARTDataRecieved = '\0';

	while(USARTDataRecieved != '$'){

		if(USARTDataRecieved != '\0' && USARTDataRecieved != '$'){
			recievedMsg[counter] =USARTDataRecieved;
			counter++;
			USARTDataRecieved ='\0';

		}
	}

	recievedMsg[counter] = '\0';
	sprintf(bufferMsg, "\nSu jugada fue: ");
	writeMsg(&handlerUSART2, bufferMsg);
	writeMsg(&handlerUSART2, recievedMsg);
}


/*Callbacks*/
void BasicTimer2_Callback(void){
	GPIOxTooglePin(&handlerOnBoardLed);

}

void BasicTimer3_Callback(void){
	contadorPasosMotores++;

}

void BasicTimer4_Callback(void){
	contadorPasosServo++;

}

void usart2Rx_Callback(void){
	//Cada que se lanze la interrupcion, recibimos datos
	USARTDataRecieved =getRxData();
}

void callback_extInt1(void){
	endStopXFlag =1;
}

void callback_extInt4(void){
	endStopYFlag =1;
}








