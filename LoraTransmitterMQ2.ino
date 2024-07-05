#include <Adafruit_TinyUSB.h>
#include <SX126x-RAK4630.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include "ADC121C021.h"

//defining the functions for reading the gas sensor values
#define EN_PIN WB_IO6	 //Logic high enables the device. Logic low disables the device
#define ALERT_PIN WB_IO5 //a high indicates that the respective limit has been violated.
#define MQ2_ADDRESS 0x51 //the device i2c address
#define RatioMQ2CleanAir (1.0) //RS / R0 = 1.0 ppm
#define MQ2_RL (10.0)		   //the board RL = 10KΩ  can adjust

//defining the parameters for the lora peer to peer communication
// Define LoRa parameters
#define RF_FREQUENCY 923000000 // Hz
#define TX_OUTPUT_POWER 22		// dBm
#define LORA_BANDWIDTH 0		// [0: 125 kHz, 1: 250 kHz, 2: 500 kHz, 3: Reserved]
#define LORA_SPREADING_FACTOR 7 // [SF7..SF12]
#define LORA_CODINGRATE 1		// [1: 4/5, 2: 4/6,  3: 4/7,  4: 4/8]
#define LORA_PREAMBLE_LENGTH 8  // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT 0   // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 5000 //changed from original value of 3000
#define TX_TIMEOUT_VALUE 5000 //changed from original value of 3000

//Lora function declarations
void OnTxDone(void);
void OnTxTimeout(void);

#ifdef NRF52_SERIES
#define LED_BUILTIN 35
#endif

//for mq2 sensor
ADC121C021 MQ2;
//for lorawan sensor
static RadioEvents_t RadioEvents;
static uint8_t TxdBuffer[64];

void setup()
{
  //mq2 sensor start
	pinMode(ALERT_PIN, INPUT);
	pinMode(EN_PIN, OUTPUT);
	digitalWrite(EN_PIN, HIGH); //power on RAK12004
	delay(5);
  //initialize the lora chip
  lora_rak4630_init();
	time_t timeout = millis();
	Serial.begin(115200);
	while (!Serial)
	{
		delay(100); //delaying for 100 milliseconds indefinitely
	}
	//********ADC121C021 ADC convert init ********************************
	while (!(MQ2.begin(MQ2_ADDRESS, Wire)))
	{
		Serial.println("please check device!!!");
		delay(200);
	}
	Serial.println("MQ2 gas sensor seems to be working fine");
  //testing the lora p2p test
  Serial.println("============");
  Serial.println("Lorap2p Tx Test");
  Serial.println("=========");

  //Initialize radio call backs
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.RxDone = NULL; //since this is for transmission
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = NULL;
  RadioEvents.RxError = NULL;

  //Initialize the radio
  Radio.Init(&RadioEvents);

  //Set the radio channel
  Radio.SetChannel(RF_FREQUENCY);

  //Set Radio Tx configuration
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0 , LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODINGRATE, LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON, true, false, 0, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);
  //send();
  delay(3000);
  //sending some data to establish connection

	//**************init MQ2 *****************************************************
	MQ2.setRL(MQ2_RL);
	/*
   *detect Propane gas if to detect other gas  need to reset A and B value，it depend on MQ sensor datasheet 
   */
	MQ2.setA(-0.890);			//A -> Slope, -0.685
	MQ2.setB(1.125);			//B -> Intersect with X - Axis  1.019
								//Set math model to calculate the PPM concentration and the value of constants
	MQ2.setRegressionMethod(0); //PPM =  pow(10, (log10(ratio)-B)/A)

	float calcR0 = 0;
	for (int i = 1; i <= 100; i++)
	{
		calcR0 += MQ2.calibrateR0(RatioMQ2CleanAir);
	}
	MQ2.setR0(calcR0 / 10);
	if (isinf(calcR0))
	{
		Serial.println("Warning: Conection issue founded, R0 is infite (Open circuit detected) please check your wiring and supply");
		while (1);
	}
	if (calcR0 == 0)
	{
		Serial.println("Warning: Conection issue founded, R0 is zero (Analog pin with short circuit to ground) please check your wiring and supply");
		while (1);
	}

	float r0 = MQ2.getR0();
	Serial.printf("R0 Value is:%3.2f\r\n", r0);
}
void loop()
{
	float sensorPPM;
	float PPMpercentage;
  float tempPPM; //need to fix this part

  //handle radio events
  Radio.IrqProcess();

	Serial.println("Getting Conversion Readings from ADC121C021");
	Serial.println(" ");
	sensorPPM = MQ2.readSensor();
  tempPPM = sensorPPM; //no need to fix this variable
	Serial.printf("sensor PPM Value is: %3.4f\r\n", sensorPPM);
	PPMpercentage = sensorPPM / 10000;
  sensorPPM = (round(tempPPM*100));
  sensorPPM = sensorPPM/100;
  Serial.printf ("The new rounded value of sensorPPM is %3.4f\r\n ", sensorPPM); //to test the value of the new rounded file
	Serial.printf("PPM percentage Value is:%3.2f%%\r\n", PPMpercentage);
	Serial.println(" ");
	Serial.println("        ***************************        ");
	Serial.println(" ");
  
  int intPart = (int)sensorPPM;
  float decPart = sensorPPM - (float)intPart;
  if (sensorPPM >= 1000 ){
    int temp = intPart;
    uint8_t ppmbuf[7];
    for (int i=3 ; i >=0  ; i--){
      ppmbuf[i] = (temp % 10)+'0';
      temp /= 10;
    }
    ppmbuf[4] = '.';
    ppmbuf[5] =  ((int)(decPart *100) / 10) +'0' ;
    ppmbuf[6] =  ((int)(decPart *100) % 10) +'0';
    Radio.Send(ppmbuf, 7);
    //tesing lines
    Serial.println("The contents of the packet are :");
    for (int j=0 ; j <=6 ; j++){
      const char buffer = (char)ppmbuf[j];
      Serial.println(ppmbuf[j]);
    }
  }
  else {
    uint8_t ppmbuf[6];
    int temp = intPart;
    for (int i=2 ; i >= 0 ; i--){
      ppmbuf[i] = (temp % 10) +'0';
      temp = temp / 10; 
    }
    ppmbuf[3] = '.';
    ppmbuf[4] = ((int)(decPart *100) / 10) + '0';
    ppmbuf[5] = ((int)(decPart *100) % 10) + '0';
    Radio.Send(ppmbuf, 6);
    //tesing lines
    Serial.println("The contents of the packet are :");
    for (int j=0 ; j <=5 ; j++){
      const char buffer = (char)ppmbuf[j];
      Serial.println(ppmbuf[j]);
    }
  }
  // TxdBuffer[0] = 'H';
  // TxdBuffer[1] = 'e';
  // TxdBuffer[2] = 'l';
  // TxdBuffer[3] = 'l';
  // TxdBuffer[4] = 'o';
  // Radio.Send(TxdBuffer, 5); 
  
	delay(15000);
  Serial.printf("The delay partt of the function has been reached");
  //yielld();
}

void OnTxDone(void){
  Serial.println("onTxDone");
  delay(1000);
  //send();
}

void OnTxTimeout(void)
{
  Serial.println("OnTxTimeout");
}
// void send(){
//   TxdBuffer[0] = 'H';
//   TxdBuffer[1] = 'e';
//   TxdBuffer[2] = 'l';
//   TxdBuffer[3] = 'l';
//   TxdBuffer[4] = 'o';
//   Radio.Send(TxdBuffer, 5); 
// }