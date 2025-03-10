#include "Memory_Map.h"
#include "rf_global_vars.h"
#include <stdio.h>
#include "scm3C_hardware_interface.h"
#include "scm3_hardware_interface.h"
#include "scum_radio_bsp.h"
#include "bucket_o_functions.h"
#include "sensor_adc/adc_test.h"

extern char send_packet[127];
extern char recv_packet[130];

unsigned int chips[100];
unsigned int chip_index = 0;
int raw_chips;
int jj;
unsigned int acfg3_val;

extern unsigned int LC_target;
extern unsigned int LC_code;
extern unsigned int IF_clk_target;
extern unsigned int IF_coarse;
extern unsigned int IF_fine;
extern unsigned int cal_iteration;
extern unsigned int ASC[38];
//extern unsigned int ASC_FPGA[38];

unsigned int num_packets_received;
unsigned int num_crc_errors;
unsigned int wrong_lengths;
unsigned int LQI_chip_errors;
unsigned int num_valid_packets_received;
unsigned int IF_estimate;

unsigned int current_thresh = 0;
extern unsigned int run_test_flag;
extern unsigned int num_packets_to_test;
signed short cdr_tau_value;

extern unsigned int HF_CLOCK_fine;
extern unsigned int HF_CLOCK_coarse;
extern unsigned int RC2M_superfine;
extern unsigned int RC2M_fine;
extern unsigned int RC2M_coarse;

unsigned int num_32k_ticks_in_100ms;
unsigned int num_2MRC_ticks_in_100ms;
unsigned int num_IFclk_ticks_in_100ms;
unsigned int num_LC_ch11_ticks_in_100ms;
unsigned int num_HFclock_ticks_in_100ms;

extern unsigned int RX_channel_codes[16];
extern unsigned int TX_channel_codes[16];
extern unsigned short optical_cal_iteration,optical_cal_finished;

signed int SFD_timestamp = 0;
signed int SFD_timestamp_n_1 = 0;
signed int timing_correction = 0;
extern unsigned short doing_initial_packet_search;

// Timer parameters 
extern unsigned int packet_interval;
extern unsigned int radio_startup_time;
extern unsigned int expected_RX_arrival;
extern unsigned int ack_turnaround_time;
extern unsigned int guard_time;
extern unsigned short current_RF_channel;

extern unsigned short do_debug_print;

// Sensor ADC: General
extern unsigned short ADC_DATA_VALID;
extern unsigned short ADC_CONTINUOUS;
extern unsigned short ADC_STOP;

// Sensor ADC: Loopback-specific
unsigned int cycles_reset = 1000;
unsigned int cycles_to_start = 1000;
unsigned int cycles_pga = 1000;

//microcontroller variables 

unsigned int counter =0 ; 
unsigned int state =0; 

void UART_ISR() {	
	static char i=0;
	static char buff[4] = {0x0, 0x0, 0x0, 0x0};
	static char waiting_for_end_of_copy = 0;
	char inChar;
	int t;
	
	inChar = UART_REG__RX_DATA;
  	buff[3] = buff[2];
	buff[2] = buff[1];
	buff[1] = buff[0];
	buff[0] = inChar;
	
	// If we are still waiting for the end of a load command
	if (waiting_for_end_of_copy) {
		if (inChar=='\n'){
			int j=0;
			printf("copying string of size %u to send_packet: ", i);
			for (j=0; j < i; j++) {
				printf("%c",send_packet[j]);
			}
			printf("\n");
			RFCONTROLLER_REG__TX_PACK_LEN = i;
			i = 0;
			waiting_for_end_of_copy = 0;
		} else if (i < 127) {		
			send_packet[i] = inChar;			
			i++;
		} else {
			printf("Input exceeds maximum packet size\n");
		}
	} else { //If waiting for a command
		// zzz: Used for small demo code
		if ( (buff[3]=='z') && (buff[2]=='z') && (buff[1]=='z') && (buff[0]=='\n') ) {
			printf("Change me!\n");
		// Copies string from UART to send_packet
		} else if ( (buff[3]=='c') && (buff[2]=='p') && (buff[1]=='y') && (buff[0]==' ') ) {
			waiting_for_end_of_copy = 1;
		// Sends TX_LOAD signal to radio controller
		} else if ( (buff[3]=='l') && (buff[2]=='o') && (buff[1]=='d') && (buff[0]=='\n') ) {
		  RFCONTROLLER_REG__CONTROL = 0x1;
			printf("TX LOAD\n");
		// Sends TX_SEND signal to radio controller
	  	} else if ( (buff[3]=='s') && (buff[2]=='n') && (buff[1]=='d') && (buff[0]=='\n') ) {
		  RFCONTROLLER_REG__CONTROL = 0x2;
		  printf("TX SEND\n");
		// Sends RX_START signal to radio controller
		} else if ( (buff[3]=='r') && (buff[2]=='c') && (buff[1]=='v') && (buff[0]=='\n') ) {
			printf("Recieving\n");
	    DMA_REG__RF_RX_ADDR = &recv_packet[0];
		  RFCONTROLLER_REG__CONTROL = 0x4;
		// Sends RX_STOP signal to radio controller
	  	} else if ( (buff[3]=='e') && (buff[2]=='n') && (buff[1]=='d') && (buff[0]=='\n') ) {
			RFCONTROLLER_REG__CONTROL = 0x8;
			printf("RX STOP\n");
		// Sends RF_RESET signal to radio controller
		} else if ( (buff[3]=='r') && (buff[2]=='s') && (buff[1]=='t') && (buff[0]=='\n') ) {
		  RFCONTROLLER_REG__CONTROL = 0x10;
			printf("RF RESET\n");	
		// Returns the status register of the radio controller
		} else if ( (buff[3]=='s') && (buff[2]=='t') && (buff[1]=='a') && (buff[0]=='\n') ) {
		  int status = RFCONTROLLER_REG__STATUS;
			printf("status register is 0x%x\n", status);	
		
			printf("power=%d, reset=%d, %d\n",ANALOG_CFG_REG__10,ANALOG_CFG_REG__4,doing_initial_packet_search);
		// Trigger a soft reset
	  	} else if ( (buff[3]=='s') && (buff[2]=='f') && (buff[1]=='t') && (buff[0]=='\n') ) {
	  		*(unsigned int*)(0xE000ED0C) = 0x05FA0004;
		// Initiate a single on-chip FSM-driven ADC conversion
	  	} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='1') && (buff[0]=='\n') ) {
		 	printf("Starting on-chip FSM ADC conversion\n");
		 	ADC_DATA_VALID = 0;
		 	onchip_control_adc_shot();
		// Initiate a single ADC conversion with loopback control of the ADC
		} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='2') && (buff[0]=='\n') ) {
		 	printf("Starting loopback-controlled ADC conversion\n");
		 	ADC_DATA_VALID = 0;
		 	loopback_control_adc_shot(cycles_reset, cycles_to_start, cycles_pga);
		// Initiate a single ADC conversion with external GPIO control of the ADC
		} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='3') && (buff[0]=='\n') ) {
			printf("Starting externally-driven GPIO ADC conversion");
			ADC_DATA_VALID = 0;
			// TODO
		// Initiate continuous on-chip FSM-driven ADC conversions
		} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='4') && (buff[0]=='\n') ) {
			printf("Starting continuous on-chip FSM ADC conversions\n");
			ADC_DATA_VALID = 0;
			onchip_control_adc_continuous();
		// Initiate continuous loopback-controlled ADC conversions 
		} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='5') && (buff[0]=='\n') ) {
			printf("Starting continuous loopback-controlled ADC conversions\n");
			ADC_DATA_VALID = 0;
			loopback_control_adc_continuous(cycles_reset, cycles_to_start, cycles_pga);
		// Initiate continuous external GPIO-controlled ADC conversions
		} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='6') && (buff[0]=='\n') ) {
			printf("Starting continuous externally-criven GPIO ADC conversions\n");
			ADC_DATA_VALID = 0;
			// TODO
		// Halt a continuous ADC run, otherwise does nothing
		} else if ( (buff[3]=='a') && (buff[2]=='d') && (buff[1]=='0') && (buff[0]=='\n') ) {
			printf("Halting continuous ADC run\n");
			halt_adc_continuous();
		// Uses the radio timer to send TX_LOAD in 0.5s, TX_SEND in 1s, capture when SFD is sent and capture when packet is sent
		} else if ( (buff[3]=='a') && (buff[2]=='t') && (buff[1]=='x') && (buff[0]=='\n') ) {
			unsigned int t = RFTIMER_REG__COUNTER + 0x3D090;
			RFTIMER_REG__COMPARE0 = t;
		  RFTIMER_REG__COMPARE1 = t + 0x3D090;
		  printf("%x\n", RFTIMER_REG__COMPARE0);
		  printf("%x\n", RFTIMER_REG__COMPARE1);
		  RFTIMER_REG__COMPARE0_CONTROL = 0x5;
		  RFTIMER_REG__COMPARE1_CONTROL = 0x9;
		  RFTIMER_REG__CAPTURE0_CONTROL = 0x9;
		  RFTIMER_REG__CAPTURE1_CONTROL = 0x11;
		  printf("Auto TX\n");
		// Uses the radio timer to send RX_START in 0.5s, capture when SFD is received and capture when packet is received
		} else if ( (buff[3]=='a') && (buff[2]=='r') && (buff[1]=='x') && (buff[0]=='\n') ) {
			RFTIMER_REG__COMPARE0 = RFTIMER_REG__COUNTER + 0x3D090;
		  RFTIMER_REG__COMPARE0_CONTROL = 0x11;
		  RFTIMER_REG__CAPTURE0_CONTROL = 0x21;
		  RFTIMER_REG__CAPTURE1_CONTROL = 0x41;
			DMA_REG__RF_RX_ADDR = &recv_packet[0];
			printf("Auto RX\n");
		// Reset the radio timer compare and capture units
		} else if ( (buff[3]=='r') && (buff[2]=='r') && (buff[1]=='t') && (buff[0]=='\n') ) {
			RFTIMER_REG__COMPARE0_CONTROL = 0x0;
			RFTIMER_REG__COMPARE1_CONTROL = 0x0;
			RFTIMER_REG__CAPTURE0_CONTROL = 0x0;
			RFTIMER_REG__CAPTURE1_CONTROL = 0x0;
		  printf("Radio timer reset\n");				
			
		// Attempt to recover if stuck in unprogrammable mode
		} else if ( (buff[3]=='x') && (buff[2]=='x') && (buff[1]=='1') && (buff[0]=='\n') ) {
		
			//for(t=0;t<=38;t++){
			//	ASC_FPGA[t] = 0;	
			//}
	
			for(t=0;t<=38;t++) {ASC[t] = 0;}
			
			// Program analog scan chain
			analog_scan_chain_write(&ASC[0]);
			analog_scan_chain_load();
			
			// Program analog scan chain on SCM3B
			//analog_scan_chain_write_3B_fromFPGA(&ASC[0]);
			//analog_scan_chain_load_3B_fromFPGA();
			
			printf("Mote re-initialized to default\n");
			
			radio_disable_interrupts();
			
			rftimer_disable_interrupts();
			
		// Debug pring
		} else if ( (buff[3]=='x') && (buff[2]=='x') && (buff[1]=='2') && (buff[0]=='\n') ) {
			do_debug_print = 1;
		// Unknown command
		} else if (inChar=='\n'){printf("unknown command\n");}
	}
}

void ADC_ISR() {
	ADC_DATA_VALID = 1;
	// printf("%d\n", (ADC_DATA_VALID&0xFFFF));
	printf("%d\n", ADC_REG__DATA);
}


void RF_ISR() {
	unsigned int interrupt = RFCONTROLLER_REG__INT;
	unsigned int error     = RFCONTROLLER_REG__ERROR;
	
  //if (interrupt & 0x00000001) printf("TX LOAD DONE\n");
	//if (interrupt & 0x00000002) printf("TX SFD DONE\n");
	if (interrupt & 0x00000004){ //printf("TX SEND DONE\n");
		
		
			GPIO_REG__OUTPUT |= 0x1;
			GPIO_REG__OUTPUT |= 0x1;
			GPIO_REG__OUTPUT &= ~(0x1);
		
		
		// Packet sent; turn transmitter off
		radio_rfOff();
		
		// Apply frequency corrections
		radio_frequency_housekeeping();
		
		//printf("TX DONE\n");
		printf("IF=%d, LQI=%d, CDR=%d, len=%d, SFD=%d, LC=%d\n",IF_estimate,LQI_chip_errors,cdr_tau_value,recv_packet[0],packet_interval,LC_code);
		
	}
	if (interrupt & 0x00000008){// printf("RX SFD DONE\n");
		SFD_timestamp_n_1 = SFD_timestamp;
		SFD_timestamp = RFTIMER_REG__COUNTER;
		
		//printf("%d --",SFD_timestamp);
		
		// Disable watchdog if a SFD has been found
		RFTIMER_REG__COMPARE3_CONTROL = 0x0;
		
		if(doing_initial_packet_search==1){
			// Sync next RX turn-on to expected arrival time
			RFTIMER_REG__COUNTER = expected_RX_arrival;
		}
		
		GPIO_REG__OUTPUT |= 0x4;
		GPIO_REG__OUTPUT &= ~(0x4);
		
	}
	if (interrupt & 0x00000010) {
		
		//int i;
		unsigned int RX_DONE_timestamp;
		char num_bytes_rec = recv_packet[0];
		//char *current_byte_rec = recv_packet+1;
		//printf("RX DONE\n");
		//printf("RX'd %d: \n", num_bytes_rec);
		//for (i=0; i < num_bytes_rec; i++) {
		//	printf("%c",current_byte_rec[i]);
		//}
		//printf("\n");
		
		
		RX_DONE_timestamp = RFTIMER_REG__COUNTER;
		
		num_packets_received++;
		
		// continuous rx for debug
		//printf("IF=%d, LQI=%d, CDR=%d, %d\n",read_IF_estimate(),read_LQI(),ANALOG_CFG_REG__25,SFD_timestamp);
		//radio_rxEnable();
		//radio_rxNow();		
			
			
		if(num_bytes_rec != 22){
			wrong_lengths++;
			

			
			// If packet was a failure, turn the radio off
			if(doing_initial_packet_search == 0){

				// Exit RX mode (so can reprogram on FPGA version)
				analog_scan_chain_load();
				
				radio_rfOff();
			}
			else{
				
				// Keep listening if doing initial search
				radio_rxEnable();
				radio_rxNow();
			}
			
		}
		// Bad CRC
		else if (error == 0x00000008){
			num_crc_errors++;
			RFCONTROLLER_REG__ERROR_CLEAR = error;
						
			// Exit RX mode (so can reprogram on FPGA version)
			//analog_scan_chain_load_3B_fromFPGA();
			
			printf("bad crc\n");
			
			// If packet was a failure, turn thfe radio off
			if(doing_initial_packet_search == 0){

				// Exit RX mode (so can reprogram on FPGA version)
				analog_scan_chain_load();
				
				radio_rfOff();
			}
			else{
				
				// Keep listening if doing initial search
				radio_rxEnable();
				radio_rxNow();
			}
		
		// Packet has good CRC value and is the correct length
		} else {
			
			GPIO_REG__OUTPUT |= 0x1;
			GPIO_REG__OUTPUT |= 0x1;
			GPIO_REG__OUTPUT &= ~(0x1);
			
			// This is the first valid packet received, start timers for next expected packet
			if(doing_initial_packet_search == 1){
				
				// Clear this flag
				doing_initial_packet_search = 0;
			
				// Enable compare interrupts for RX
				RFTIMER_REG__COMPARE0_CONTROL = 0x3;
				RFTIMER_REG__COMPARE1_CONTROL = 0x3;
				RFTIMER_REG__COMPARE2_CONTROL = 0x3;
				
				// Enable timer ISRs in the NVIC
				rftimer_enable_interrupts();
				
				analog_scan_chain_load();
				
				radio_rfOff();
				

			}	
			// Already locked onto packet rate
			else{
			
				// Only record IF estimate, LQI, and CDR tau for valid packets
				IF_estimate = read_IF_estimate();
				LQI_chip_errors	= ANALOG_CFG_REG__21 & 0xFF; //read_LQI();	
				
				// Read the value of tau debug at end of packet
				// Do this later in the ISR to make sure this register has settled before trying to read it
				// (the register is on the adc clock domain)
				cdr_tau_value = ANALOG_CFG_REG__25;
					
				num_valid_packets_received++;
				
				// Prepare ack	
				// Data is stored in send_packet[]
				radio_loadPacket(30);
		
				// Transmit packet at this time
				RFTIMER_REG__COMPARE5 = RX_DONE_timestamp + ack_turnaround_time;	
				
				// Turn on transmitter 
				RFTIMER_REG__COMPARE4 = RFTIMER_REG__COMPARE5 - radio_startup_time;
					
				// Enable TX timers
				RFTIMER_REG__COMPARE4_CONTROL = 0x3;
				RFTIMER_REG__COMPARE5_CONTROL = 0x3;
				
				analog_scan_chain_load();
			}
		
		// Keep listening if not locked yet
		//if(doing_initial_packet_search==1) radio_rxNow();
			
				
		// Do this no matter whether the packet was valid or not:
		// Load the ASC to prep for TX
		//analog_scan_chain_load_3B_fromFPGA();
			
			//	IF_estimate = read_IF_estimate();
				//LQI_chip_errors	= ANALOG_CFG_REG__21 & 0xFF; //read_LQI();	
//				
//				// Read the value of tau debug at end of packet
//				// Do this later in the ISR to make sure this register has settled before trying to read it
//				// (the register is on the adc clock domain)
			//	cdr_tau_value = ANALOG_CFG_REG__25;
			
	//		printf("IF=%d, LQI=%d, CDR=%d, len=%d, SFD=%d, LC=%d\n",IF_estimate,LQI_chip_errors,cdr_tau_value,recv_packet[0],packet_interval,LC_code);
	//		radio_rxEnable();
	//		radio_rxNow();
	//		rftimer_disable_interrupts();

		}
	}
	
	//if (error != 0) {
	//	if (error & 0x00000001) printf("TX OVERFLOW ERROR\n");
	//	if (error & 0x00000002) printf("TX CUTOFF ERROR\n");
	//	if (error & 0x00000004) printf("RX OVERFLOW ERROR\n");
	//	if (error & 0x00000008) printf("RX CRC ERROR\n");
	//	if (error & 0x00000010) printf("RX CUTOFF ERROR\n");
		RFCONTROLLER_REG__ERROR_CLEAR = error;
	//}
	
	RFCONTROLLER_REG__INT_CLEAR = interrupt;
}

void RFTIMER_ISR() {



 
unsigned int interrupt = RFTIMER_REG__INT;

if (interrupt & 0x00000001){ //printf("COMPARE0 MATCH\n");
	
	
//RFTIMER_REG__COMPARE5 = 0x000001F4;
	
counter +=1;

if (state==0 && counter == 100){

state = 1;
GPIO_REG__OUTPUT = 0x0830;
RFTIMER_REG__COMPARE1_CONTROL = 0x0;
RFTIMER_REG__COMPARE2_CONTROL = 0x0;
RFTIMER_REG__COMPARE3_CONTROL = 0x0;
RFTIMER_REG__COMPARE4_CONTROL = 0x0;
counter = 0;  
//ICER = 0x2000;
return;
}
if (state==1 && counter == 100){
GPIO_REG__OUTPUT = 0x0800;
state = 2;
counter = 0;
return;
}
if (state==2 && counter == 100){
state = 3;
counter = 0;
return;
}
if (state==3 && counter == 100){
state = 0;
RFTIMER_REG__COMPARE1_CONTROL = 0x03;
RFTIMER_REG__COMPARE2_CONTROL = 0x03;
RFTIMER_REG__COMPARE3_CONTROL = 0x03;
RFTIMER_REG__COMPARE4_CONTROL = 0x03;
counter = 0;
ISER = 0x2000; 
return;
}
}

if (interrupt & 0x00000002){// printf("COMPARE1 MATCH\n");

GPIO_REG__OUTPUT ^= 0x10;


}
if (interrupt & 0x00000004){// printf("COMPARE2 MATCH\n");

GPIO_REG__OUTPUT ^= 0x10;

}
if (interrupt & 0x00000008){// printf("COMPARE3 MATCH\n");

GPIO_REG__OUTPUT ^=0x20;

}
if (interrupt & 0x00000010){// printf("COMPARE4 MATCH\n");

GPIO_REG__OUTPUT ^=0x20;
}
if (interrupt & 0x00000020){// printf("COMPARE5 MATCH\n");

	
//GPIO_REG__OUTPUT ^= 0x0800;
//
//	RFTIMER_REG__COMPARE5 += 0x000001F4;

}
if (interrupt & 0x00000040) printf("COMPARE6 MATCH\n");
if (interrupt & 0x00000080) printf("COMPARE7 MATCH\n");
if (interrupt & 0x00000100) printf("CAPTURE0 TRIGGERED AT: 0x%x\n", RFTIMER_REG__CAPTURE0);
if (interrupt & 0x00000200) printf("CAPTURE1 TRIGGERED AT: 0x%x\n", RFTIMER_REG__CAPTURE1);
if (interrupt & 0x00000400) printf("CAPTURE2 TRIGGERED AT: 0x%x\n", RFTIMER_REG__CAPTURE2);
if (interrupt & 0x00000800) printf("CAPTURE3 TRIGGERED AT: 0x%x\n", RFTIMER_REG__CAPTURE3);
if (interrupt & 0x00001000) printf("CAPTURE0 OVERFLOW AT: 0x%x\n", RFTIMER_REG__CAPTURE0);
if (interrupt & 0x00002000) printf("CAPTURE1 OVERFLOW AT: 0x%x\n", RFTIMER_REG__CAPTURE1);
if (interrupt & 0x00004000) printf("CAPTURE2 OVERFLOW AT: 0x%x\n", RFTIMER_REG__CAPTURE2);
if (interrupt & 0x00008000) printf("CAPTURE3 OVERFLOW AT: 0x%x\n", RFTIMER_REG__CAPTURE3);

RFTIMER_REG__INT_CLEAR = interrupt;
}

// This ISR goes off when the raw chip shift register interrupt goes high
// It reads the current 32 bits and then prints them out after N cycles
void RAWCHIPS_32_ISR() {
	
	unsigned int jj;
	unsigned int rdata_lsb, rdata_msb;
	
	// Read 32bit val
	rdata_lsb = ANALOG_CFG_REG__17;
	rdata_msb = ANALOG_CFG_REG__18;
	chips[chip_index] = rdata_lsb + (rdata_msb << 16);	
		
	chip_index++;
	
	//printf("x1\n");
	
	// Clear the interrupt
	//ANALOG_CFG_REG__0 = 1;
	//ANALOG_CFG_REG__0 = 0;
	acfg3_val |= 0x20;
	ANALOG_CFG_REG__3 = acfg3_val;
	acfg3_val &= ~(0x20);
	ANALOG_CFG_REG__3 = acfg3_val;

	if(chip_index == 10){	
		for(jj=1;jj<10;jj++){
			printf("%X\n",chips[jj]);
		}

		ICER = 0x0100;
		ISER = 0x0200;
		chip_index = 0;
		
		// Wait for print to complete
		for(jj=0;jj<10000;jj++);
		
		// Execute soft reset
		*(unsigned int*)(0xE000ED0C) = 0x05FA0004;
	}
}

// With HCLK = 5MHz, data rate of 1.25MHz tested OK
// For faster data rate, will need to raise the HCLK frequency
// This ISR goes off when the input register matches the target value
void RAWCHIPS_STARTVAL_ISR() {
	
	unsigned int rdata_lsb, rdata_msb;
	
	// Clear all interrupts
	acfg3_val |= 0x60;
	ANALOG_CFG_REG__3 = acfg3_val;
	acfg3_val &= ~(0x60);
	ANALOG_CFG_REG__3 = acfg3_val;
		
	// Enable the interrupt for the 32bit 
	ISER = 0x0200;
	ICER = 0x0100;
	ICPR = 0x0200;
	
	// Read 32bit val
	rdata_lsb = ANALOG_CFG_REG__17;
	rdata_msb = ANALOG_CFG_REG__18;
	chips[chip_index] = rdata_lsb + (rdata_msb << 16);
	chip_index++;

}


// This interrupt goes off every time 32 new bits of data have been shifted into the optical register
// Do not recommend trying to do any CPU intensive actions while trying to receive optical data
// ex, printf will mess up the received data values
void OPTICAL_32_ISR(){
	// printf("Optical 32-bit interrupt triggered\n");
	
	//unsigned int LSBs, MSBs, optical_shiftreg;
	//int t;
	
	// 32-bit register is analog_rdata[335:304]
	//LSBs = ANALOG_CFG_REG__19; //16 LSBs
	//MSBs = ANALOG_CFG_REG__20; //16 MSBs
	//optical_shiftreg = (MSBs << 16) + LSBs;	
	
	// Toggle GPIO 0
	//GPIO_REG__OUTPUT ^= 0x1;
	
}

// This interrupt goes off when the optical register holds the value {221, 176, 231, 47}
// This interrupt can also be used to synchronize to the start of an optical data transfer
// Need to make sure a new bit has been clocked in prior to returning from this ISR, or else it will immediately execute again
void OPTICAL_SFD_ISR(){
	unsigned int rdata_lsb, rdata_msb; 
	unsigned int count_LC, count_32k, count_2M, count_HFclock, count_IF;
	// Disable all counters
	ANALOG_CFG_REG__0 = 0x007F;
	
	// Keep track of how many calibration iterations have been completed
	optical_cal_iteration++;
		
	// Read 32k counter
	rdata_lsb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x000000);
	rdata_msb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x040000);
	count_32k = rdata_lsb + (rdata_msb << 16);

	// Read HF_CLOCK counter
	rdata_lsb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x100000);
	rdata_msb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x140000);
	count_HFclock = rdata_lsb + (rdata_msb << 16);

	// Read 2M counter
	rdata_lsb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x180000);
	rdata_msb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x1C0000);
	count_2M = rdata_lsb + (rdata_msb << 16);
		
	// Read LC_div counter (via counter4)
	rdata_lsb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x280000);
	rdata_msb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x2C0000);
	count_LC = rdata_lsb + (rdata_msb << 16);
	
	// Read IF ADC_CLK counter
	rdata_lsb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x300000);
	rdata_msb = *(unsigned int*)(APB_ANALOG_CFG_BASE + 0x340000);
	count_IF = rdata_lsb + (rdata_msb << 16);
	
	// Reset all counters
	ANALOG_CFG_REG__0 = 0x0000;		
	
	// Enable all counters
	ANALOG_CFG_REG__0 = 0x3FFF;	
		
	// Don't make updates on the first two executions of this ISR
	if(optical_cal_iteration > 2){
		
		// Do correction on HF CLOCK
		// Fine DAC step size is about 6000 counts
		if(count_HFclock < 1997000) HF_CLOCK_fine--;
		if(count_HFclock > 2003000) HF_CLOCK_fine++;
		set_sys_clk_secondary_freq(HF_CLOCK_coarse, HF_CLOCK_fine);
		
		// Do correction on LC
		if(count_LC > (LC_target + 30)) LC_code -= 1;
		if(count_LC < (LC_target - 30))	LC_code += 1;
		LC_monotonic(LC_code);
			
		// Do correction on 2M RC
		// Coarse step ~1100 counts, fine ~150 counts, superfine ~25
		// Too fast
		if(count_2M > (200600)) RC2M_coarse += 1;
		else if(count_2M > (200080)) RC2M_fine += 1;
		else if(count_2M > (200015))	RC2M_superfine += 1;
		
		// Too slow
		if(count_2M < (199400))	RC2M_coarse -= 1;
		else if(count_2M < (199920)) RC2M_fine -= 1;
		else if(count_2M < (199985))	RC2M_superfine -= 1;
		set_2M_RC_frequency(31, 31, RC2M_coarse, RC2M_fine, RC2M_superfine);

		// Do correction on IF RC clock
		// Fine DAC step size is ~2800 counts
		if(count_IF > (1600000+1400)) IF_fine += 1;
		if(count_IF < (1600000-1400))	IF_fine -= 1;
		set_IF_clock_frequency(IF_coarse, IF_fine, 0);
		
		analog_scan_chain_write(&ASC[0]);
		analog_scan_chain_load();	
	}
	
	// Debugging output
	// printf("HF=%d-%d   2M=%d-%d,%d,%d   LC=%d-%d   IF=%d-%d\n",count_HFclock,HF_CLOCK_fine,count_2M,RC2M_coarse,RC2M_fine,RC2M_superfine,count_LC,LC_code,count_IF,IF_fine); 
	 
	//printf("%d\n",count_LC);
	if(optical_cal_iteration == 25){
		// Disable this ISR
		ICER = 0x0800;
		optical_cal_iteration = 0;
		optical_cal_finished = 1;
		
		// Store the last count values
		num_32k_ticks_in_100ms = count_32k;
		num_2MRC_ticks_in_100ms = count_2M;
		num_IFclk_ticks_in_100ms = count_IF;
		num_LC_ch11_ticks_in_100ms = count_LC;
		num_HFclock_ticks_in_100ms = count_HFclock;
		
		// Update the expected packet rate based on the measured HF clock frequency
		// Have an estimate of how many 20MHz clock ticks there are in 100ms
		// But need to know how many 20MHz/40 = 500kHz ticks there are in 125ms (if doing 8 Hz packet rate)
		// (125 / 100) / 40 = 1/32
		packet_interval = num_HFclock_ticks_in_100ms >> 5;
	
		//printf("LC_code=%d\n", LC_code);
		//printf("IF_fine=%d\n", IF_fine);
		
		printf("done\n");
		//printf("Building channel table...");
		
		//build_channel_table(LC_code);
		
		//printf("done\n");
		
		//radio_disable_all();
		
		// Halt all counters
		ANALOG_CFG_REG__0 = 0x0000;	
	
	}
}
	
	
// ISRs for external interrupts
void INTERRUPT_GPIO3_ISR(){
	printf("External Interrupt GPIO3 triggered\n");
}
void INTERRUPT_GPIO8_ISR(){
	printf("External Interrupt GPIO8 triggered\n");
}
void INTERRUPT_GPIO9_ISR(){
	printf("External Interrupt GPIO9 triggered\n");
	//state = 1; 
	if (state ==0){
	counter = 99; 
	
	RFTIMER_REG__COMPARE1_CONTROL = 0x0;
	RFTIMER_REG__COMPARE2_CONTROL = 0x0;
	RFTIMER_REG__COMPARE3_CONTROL = 0x0;
	RFTIMER_REG__COMPARE4_CONTROL = 0x0;
	
	//GPIO_REG__OUTPUT |= 0x0200; 
	//ICER = 0x2000; 
	GPIO_REG__OUTPUT ^= 0x0400;
	ICER = 0x2000;
	}
	else{
		ICER = 0x2000; 
	}
}
void INTERRUPT_GPIO10_ISR(){
	printf("External Interrupt GPIO10 triggered\n");
}


