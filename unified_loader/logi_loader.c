#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <linux/types.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <string.h> 
#include <math.h>
#include "logi_loader.h"
#include "i2c_loader.h"

//PROCESS
#define CONFIG_CYCLES 1
#define SSI_DELAY 1



//FILE DESCRIPTORS
int spi_fd ;
int fd;


//SPI DEVICE SETUP
#define SPI_MAX_LENGTH 4096
static unsigned int spi_mode = 0 ;
static unsigned int spi_bits = 8 ;
static unsigned long spi_speed = 8000000UL ;
static unsigned int delay = 0;

//CONFIG BIT ARRAY
unsigned char configBits[1024*1024*4], configDummy[1024*1024*4];

//PROTOTYPES

int init_spi(char * path);
char serialConfig(unsigned char * buffer, unsigned int length);
void serialConfigWriteByte(unsigned char val);

//PRINT COMMAND LINE HELP OPTIONS
void printHelp(){
	printf("Usage : logi_loader -[r|h] <bitfile> \n");
	printf("-r	will put the FPGA in reset state (lower power consumption)\n");
	printf("-h      will print the help \n");
}

//DELAY N CYCLES
void __delay_cycles(unsigned long cycles){
	while(cycles != 0){
		cycles -- ;	
	}
}

//MIN FUNTION
static inline unsigned int min(unsigned int a, unsigned int b){
	if(a < b) return a ;
	return b ;
}

//INIT SPI DEVICE
int init_spi(char * path){
	int ret ;
	spi_fd = open(path, O_RDWR);
	if (spi_fd < 0){
		printf("can't open SPI spi_device\n");
		return -1 ;
	}

	ret = ioctl(spi_fd, SPI_IOC_WR_MODE, &spi_mode);
	if (ret == -1){
		printf("can't set spi mode \n");
		return -1 ;
	}

	ret = ioctl(spi_fd, SPI_IOC_RD_MODE, &spi_mode);
	if (ret == -1){
		printf("can't get spi mode \n ");
		return -1 ;
	}

	/*
	 * bits per word
	 */
	ret = ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &spi_bits);
	if (ret == -1){
		printf("can't set bits per word \n");
		return -1 ;
	}

	ret = ioctl(spi_fd, SPI_IOC_RD_BITS_PER_WORD, &spi_bits);
	if (ret == -1){
		printf("can't get bits per word \n");
		return -1 ;
	}

	/*
	 * max speed hz
	 */
	ret = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
	if (ret == -1){
		printf("can't set max speed hz \n");
		return -1 ;
	}

	ret = ioctl(spi_fd, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
	if (ret == -1){
		printf("can't get max speed hz \n");
		return -1 ;
	}
	

	return 1;
}

//RESET THE FPGA - COMMNAD LINE OPTION
void resetFPGA(){
	set_i2c_progb();
}

//CONFIGURE THE FPGA USING SLAVE SERIAL CONFIGURATION INTERFACE
char serialConfig(unsigned char * buffer, unsigned int length){
	int iCfg;
	unsigned long int i;
	unsigned long int timer = 0;
	unsigned char * bitBuffer;
	unsigned char i2c_buffer[4];
	unsigned int write_length, write_index ;


	__delay_cycles(100 * SSI_DELAY);

	set_i2c_progb();
	__delay_cycles(10 * SSI_DELAY);
	clear_i2c_progb();
	__delay_cycles(5 * SSI_DELAY);

	while (get_i2c_init()> 0 && timer < 200)
		timer++; // waiting for init pin to go down

	if (timer >= 200) {
		printf("FPGA did not answer to prog request, init pin not going low \n");
		set_i2c_progb();
		return -1;
	}

	timer = 0;
	__delay_cycles(5 * SSI_DELAY);
	set_i2c_progb();

	
	while (get_i2c_init() == 0 && timer < 256) { // need to find a better way ...
		timer++; // waiting for init pin to go up
	}
	

	if (timer >= 256) {
		printf("FPGA did not answer to prog request, init pin not going high \n");
		return -1;
	}

	timer = 0;
	write_length = min(length, SPI_MAX_LENGTH);
	write_index = 0 ;
	while(length > 0){
		if(write(spi_fd, &buffer[write_index], write_length) < write_length){
			printf("spi write error \n");
		}
		write_index += write_length ;
		length -= write_length ;
		write_length = min(length, SPI_MAX_LENGTH);
	}

	if (get_i2c_done() == 0) {
		printf("FPGA prog failed, done pin not going high \n");
		return -1;
	}
	/*
	i2c_buffer[0] = fpga_loader->expander_cfg;
	i2c_buffer[1] = 0xFF;
	i2c_buffer[1] &= ~((1 << fpga_loader->prog_pin) | (1 << fpga_loader->mode1_pin));
	if(fpga_loader->flash_rst_pin >= 0){
		i2c_buffer[1] &= ~(fpga_loader->flash_rst_pin);
	}
	write(i2c_fd, i2c_buffer, 2); // set all unused config pins as input (keeping mode pins and PROG as output)
	*/		
	/*i2c_set_pin(fpga_loader, fpga_loader->mode1_pin, 1);
	i2c_set_pin(fpga_loader, fpga_loader->prog_pin, 1);*/
	return length;
}


int init_loader(){
	struct i2c_loader_struct * fpga_loader ; 
#ifdef LOGIBONE
	int success = 0;
	int retry = 0 ;
	char dummy = 0 ;
	
	while(!success & retry < sizeof(logi_variants) ){
		if(init_i2c_loader(logi_variants[retry]) < 0){
			retry ++ ;	
		}else{
			fpga_loader = logi_variants[retry] ;
			success = 1 ;		
		}
	}
	if(retry >= sizeof(logi_variants)){
		printf("Cannot detect LOGI-BONE \n");
	 	return -1 ;
	}
#else
	fpga_loader = &logipi_r1_5_loader ; // if not logibone, board is logipi
	if(init_i2c_loader(fpga_loader) < 0){
		printf("Cannot detect LOGI-PI \n");
	 	return -1 ;
	}
#endif
	printf("Board variant is %s \n", fpga_loader-> name);
	if(init_spi(fpga_loader->spi_path) < 0){
		 printf("can't open SPI bus \n");
		 return -1 ;
	}
}

//MAIN FUNCTION******************************************************
int main(int argc, char ** argv){
	char c ;
	unsigned int i = 1;
	FILE * fr;
	long start_time, end_time ;
	double diff_time ;
	struct timespec cpu_time ;
	unsigned int size = 0 ;	
	#ifdef LOGIBONE
		printf("Compiled for LOGI-BONE \n");
	#else
		printf("Compiled for LOGI-PI \n");
	#endif
	if(init_loader() < 0){
		printf("No logi-board detected !! \n");
		exit(0);
	}
	/*
	//parse programm args
	for(i = 1 ; i < argc ; ){
		if(argv[i][0] == '-'){
			switch(argv[i][1]){
				case '\0': 
					i ++ ;
					break ;
				case 'r' :
					resetFPGA(); 
					closeGPIOs();
					close(i2c_fd);
					return 1 ;
					break ;
				case 'h' :
					printHelp();
					return 1 ;
					break;
				default :
					printHelp();
					return 1 ;
					break ;
			}
		}else{
			//last argument is file to load
			break ;
		}
	}
	*/
	fr = fopen (argv[i], "rb");  /* open the file for reading bytes*/
	if(fr < 0){
		printf("cannot open file %s \n", argv[1]);
		return -1 ;	
	}
	memset((void *) configBits, (int) 0, (size_t) 1024*1024);
	size = fread(configBits, 1, 1024*1024, fr);
	printf("bit file size : %d \n", size);
	
	//8*5 clock cycle more at the end of config
	if(serialConfig(configBits, size+5) < 0){
		printf("config error \n");

	}else{
		printf("config success ! \n");	
	}
	
	fclose(fr);
	close_i2c_loader();
	close(spi_fd);
	return 1;
}