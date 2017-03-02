/*
 * main.c
 *
 *  Created on: 12 Feb 2017
 *      Author: rafpe
 */

#include "stm32f4xx.h"
#include "stm32f407xx.h"

#define OV_SCCB_ADDRESS   0x21
#define OV_SCCB_ADDRESS_W 0x42
#define OV_SCCB_ADDRESS_R 0x43
#define QT_DEVICE_ID            0x2E    // Chip ID (read from device-address 0)
#define QT_I2C_ADDRESS          0x1B    // Qtouch device ID
#define QT_I2C_ADDRESS_W        (QT_I2C_ADDRESS << 1)     // Definicja adresu I2C tzw SLA+W
#define QT_I2C_ADDRESS_R        ((QT_I2C_ADDRESS <<1)|1)  // Definicja adresu I2C tzw SLA+R
#define QT_CONFIG_ADDR          32                          // Adres od ktorego mozemy uzyc struktury do konfiguracji do odczytu z I2C

#define I2C			   I2C1
#define I2C_EV5        (!(I2C->SR1 & I2C_SR1_SB))
#define I2C_EV6        (!(I2C->SR1 & I2C_SR1_ADDR))
#define I2C_EV7        (!(I2C->SR1 & I2C_SR1_RXNE))
#define I2C_EV8        (!(I2C->SR1 & I2C_SR1_TXE))
#define I2C_EV8_2      (!(I2C->SR1 & (I2C_SR1_TXE | I2C_SR1_BTF)))




#define TIMEOUT_MAX  10000


uint8_t addr7w,addr7r;
uint32_t reg1,reg2;

void mc02_init(void);
void i2c_setup_gpio(void);
void rcc_init(void);
void i2c_init(void);
uint8_t i2c_read(uint8_t address, uint8_t registry);
void i2c_read_many(uint8_t address, uint8_t registry, uint8_t * result, uint8_t length);
void i2c_write_many(uint8_t address, uint8_t registry, uint8_t* buf, uint32_t length) ;
void i2c_write(uint8_t address, uint8_t registry, uint8_t data) ;

void i2c_set_addr7(uint8_t addr)
{
	addr7w = addr << 1;
	addr7r = addr7w | 1;
}

void rcc_init(void)
{

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN|	// Enable clock: GPIOB
					RCC_AHB1ENR_GPIOCEN;	// Enable clock: GPIOC
	RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;		// Enable clock: I2C1

	__DSB();	// Data Synchronization Barrier

}

void mc02_init(void)
{
	/*
	 *  Enable SYSCLK output to PC9
	 */
	GPIOC->MODER |= GPIO_MODER_MODER9_1 ;   // AF: PC9 => MCO2
}

uint8_t i2c_read(uint8_t address, uint8_t registry)
{
	uint32_t timeout = TIMEOUT_MAX;

	while(I2C1->SR2 & I2C_SR2_BUSY);		// Wait for BUSY line
	I2C1->CR1 |= I2C_CR1_START;				// Generate START condition

	while (!(I2C1->SR1 & I2C_SR1_SB)); 		// Wait for EV5
	I2C1->DR = address<<1;					// Write device address (W)

	while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6
    (void)I2C1->SR2;						// Read SR2

	while (!(I2C1->SR1 & I2C_SR1_TXE));		// Wait for EV8_1
	I2C1->DR = registry;

	I2C1->CR1 |= I2C_CR1_STOP;				// Generate STOP condition


	I2C1->CR1 |= I2C_CR1_START;				// Generate START condition

	while (!(I2C1->SR1 & I2C_SR1_SB)); 		// Wait for EV5
	I2C1->DR = (address << 1 ) | 1;			// Write device address (R)

	while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6
    I2C1->CR1 &= ~I2C_CR1_ACK;              // No ACK
    (void)I2C1->SR2;						// Read SR2

	while (!(I2C1->SR1 & I2C_SR1_RXNE));	// Wait for EV7_1
    uint8_t value = (uint8_t)I2C1->DR;      // Read value

    I2C1->CR1 |= I2C_CR1_STOP;			    // Generate STOP condition

	return value;
}

/*
 * Function responsbile for configuration
 * of GPIO pins
 */
void i2c_setup_gpio(void)
{
	GPIOB->MODER |= GPIO_MODER_MODER6_1 |   // AF: PB6 => I2C1_SCL
					GPIO_MODER_MODER7_1; 	// AF: PB7 => I2C1_SDA


	GPIOB->OTYPER |= GPIO_OTYPER_OT_6|
					 GPIO_OTYPER_OT_7;

	GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR6|
					  GPIO_OSPEEDER_OSPEEDR7;

	GPIOB->PUPDR |= GPIO_PUPDR_PUPDR6_0|
					GPIO_PUPDR_PUPDR7_0;

	/*
	 * Alternate functions are configured in ARFL for PINS 0..7
	 * and in ARFH for PINS 8..15
	 * Based on DS we will select appropiate AF0..AF7
	 */
	GPIOB->AFR[0] |= ( 1 << 30 ) | ( 1 << 26); // P6/P7 => AF4
}

/*
 * Function responsbile for init of I2C
 */
void i2c_init(void)
{

	/*
	 *  Reset I2C from lock state
	 */
	//I2C1->CR1 |= I2C_CR1_SWRST;

	/*
	 * FREQ: Set frequencey based on APB1 clock
	 * when using HSI@16MHz this is 2MHz
	 */
	I2C1->CR2 &= ~(I2C_CR2_FREQ);
	I2C1->CR2 |= 0b000010;

	/*
	 * Depending on the frequency of said prescaler must be installed in
	 * accordance with the selected data rate.
	 * We choose the maximum, for standard mode - 100 kHz:
	 *
	 *  2MHz / 100 = 20 kHz;
	 *
	 */
	I2C1->CCR &= ~I2C_CCR_CCR;
	I2C1->CCR |= 80;

	/*
	 * clock period is equal to (1 / 2 MHz = 500 ns), therefore the maximum rise time:
	 * 1000 ns / 500 ns = 2 + 1 (plus one - a small margin) = 3
	 *
	 */
	I2C1->TRISE = 3;


	/*
	 * Enable perifpherial at the END
	 */
	I2C1->CR1 = I2C_CR1_ACK|
				I2C_CR1_PE;     // PE : Peripherial enable
}


void i2c_read_many(uint8_t address, uint8_t registry, uint8_t * result, uint8_t length)
{
	uint32_t timeout = TIMEOUT_MAX;

	while(I2C1->SR2 & I2C_SR2_BUSY);		// Wait for BUSY line


	I2C1->CR1 |= I2C_CR1_START;				// Generate START condition

	while (!(I2C1->SR1 & I2C_SR1_SB)); 		// Wait for EV5
	I2C1->DR = address<<1;					// Write device address (W)

	while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6
    reg2 = I2C1->SR2;						// Read SR2

	while (!(I2C1->SR1 & I2C_SR1_TXE));		// Wait for EV8_1
	I2C1->DR = registry;					// Write registry address

	I2C1->CR1 |= I2C_CR1_STOP;				// Generate STOP condition


	I2C1->CR1 |= I2C_CR1_START;				// Generate START condition

	while (!(I2C1->SR1 & I2C_SR1_SB)); 		// Wait for EV5
	I2C1->DR = (address << 1 ) | 1;			// Write device address (R)

	if(length==2)
	{
	    while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6

	    I2C1->CR1 &= ~I2C_CR1_ACK;              // No ACK
	    I2C1->CR1 |= I2C_CR1_POS;               // POS
	    (void)I2C1->SR2;

	    while (!(I2C1->SR1 & I2C_SR1_BTF));	    // Wait for BTF
	    I2C1->CR1 |= I2C_CR1_STOP;			    // Generate STOP condition

	    *result++ = (uint8_t)I2C1->DR;          // Read value
	    *result++ = (uint8_t)I2C1->DR;          // Read value
	}
	if(length>2)
	{
	    while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6
	    (void)I2C1->SR2;

	    length--;
	    while(length--)
	    {
		    while (!(I2C1->SR1 & I2C_SR1_BTF));	    // Wait for BTF
		    *result++ = (uint8_t)I2C1->DR;          // Read value

		    if(length==1)
		    {
			    I2C1->CR1 &= ~I2C_CR1_ACK;              // No ACK
			    I2C1->CR1 |= I2C_CR1_STOP;			    // Generate STOP condition
		    }
	    }

	    *result++ = (uint8_t)I2C1->DR;          // Read value
	}


}

void i2c_write(uint8_t address, uint8_t registry, uint8_t data) 
{


	I2C1->CR1 |= I2C_CR1_START;				// Generate START condition

	while (!(I2C1->SR1 & I2C_SR1_SB)); 		// Wait for EV5
	I2C1->DR = address<<1;					// Write device address (W)

	while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6
    reg2 = I2C1->SR2;						// Read SR2

	while (!(I2C1->SR1 & I2C_SR1_TXE));		// Wait for EV8_1
	I2C1->DR = registry;					// Write registry address

	while (!(I2C1->SR1 & I2C_SR1_BTF));	    // Wait for BTF
	I2C1->DR = data;

	I2C1->CR1 |= I2C_CR1_STOP;			    // Generate STOP condition
}


void i2c_write_many(uint8_t address, uint8_t registry, uint8_t* buf, uint32_t length) 
{


	I2C1->CR1 |= I2C_CR1_START;				// Generate START condition

	while (!(I2C1->SR1 & I2C_SR1_SB)); 		// Wait for EV5
	I2C1->DR = address<<1;					// Write device address (W)

	while (!(I2C1->SR1 & I2C_SR1_ADDR));	// Wait for EV6
    reg2 = I2C1->SR2;						// Read SR2

	while (!(I2C1->SR1 & I2C_SR1_TXE));		// Wait for EV8_1
	I2C1->DR = registry;					// Write registry address

	while (--length)
	{
		while (!(I2C1->SR1 & I2C_SR1_BTF));	    // Wait for BTF
		I2C1->DR = *buf++;
	}
	
	while (!(I2C1->SR1 & I2C_SR1_BTF));	    // Wait for BTF

	I2C1->CR1 |= I2C_CR1_STOP;			    // Generate STOP condition
}

int main(void)
{

	uint8_t tablica[8];
	uint8_t zmiana_wartosci = 0x00;

	rcc_init();

	mc02_init();

	i2c_setup_gpio();

	i2c_init();

	uint8_t CLKRC =  i2c_read(OV_SCCB_ADDRESS, 0x11);
	uint8_t DBLV =  i2c_read(OV_SCCB_ADDRESS, 0x6B);

	// Prescaler on 3 => so 3+1 == 4
	i2c_write(OV_SCCB_ADDRESS,0x11,&CLKRC & 0b10000011);
	i2c_write(OV_SCCB_ADDRESS,0x6B,( &DBLV & 0b00111111) | 0b10000000);

	uint8_t and = 0;

	while(1)
	{



	}
}


