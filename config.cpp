// 7: output data3		0x80
// 6: input  unused		0x40
// 5: input  unused		0x20
// 4: input  conf_done  0x10
// 3: input  nstatus    0x08
// 2: output nconfig	0x04
// 1: output data0		0x02
// 0: output dclk		0x01

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <ftd2xx.h>

#define MAX_DEVICES		16

#define DCLK_BIT		0x01
#define DATA_BIT		0x02
#define nCONFIG_BIT		0x04
#define nSTATUS_BIT		0x08
#define CONF_DONE_BIT	0x10

void Quit (int sig);
int ListConnectedDevices (void);
int ConfigureDevice (char *device, char *bitfile);
FT_STATUS PurgeReceiveBuffer (FT_HANDLE ftHandle);

FT_HANDLE ftHandle = NULL;


int main (int argc, char *argv[])
{
	int status = -1;
	int i;
	char *device;
	char *bitfile;

	// initialize globals
	ftHandle = NULL;

	// trap ctrl-c to call quit function 
    signal (SIGINT, Quit);       

	if (argc == 1) {
		// list connected devices
		status = ListConnectedDevices ();
		printf ("Use config <serial number> <bitfile (rbf)> to config device.\n");
	} else if (argc == 3) {
		// configure device with bitfile
		device = argv[1];
		bitfile = argv[2];
		status = ConfigureDevice (device, bitfile);
	} else {
		printf ("usage: list devices:   config\n");
		printf ("usage: config device:  config <serial number> <bitfile (rbf)>\n");
		status = -1;
	}

	return status;
}


void Quit (int sig)
{
	if (ftHandle != NULL) {
		FT_Close (ftHandle);
		ftHandle = NULL;
		printf ("Closed device.\n");
    }

	exit (-1);
}


int ListConnectedDevices (void)
{
	FT_STATUS ftStatus;
    char *pcBufLD[MAX_DEVICES + 1];
    char cBufLD[MAX_DEVICES][64];
	int i, iNumDevs;

    for (i = 0; i < MAX_DEVICES; i++) {
        pcBufLD[i] = cBufLD[i];
    }
    pcBufLD[MAX_DEVICES] = NULL;
	
    ftStatus = FT_ListDevices(pcBufLD, &iNumDevs, FT_LIST_ALL | FT_OPEN_BY_SERIAL_NUMBER);
	if (ftStatus != FT_OK) {
        printf ("Error: FT_ListDevices: %d\n", ftStatus);
        return -1;
    }

    for (i = 0; ((i <MAX_DEVICES) && (i < iNumDevs)); i++) {
        printf ("Device %d Serial Number - %s\n", i, cBufLD[i]);
    }
}


int ConfigureDevice (char *device, char *bitfile)
{
	FILE *fbitfile;
	int i;
	int bytes;
	int status = -1;
	FT_STATUS ftStatus;
	char rxBuffer[128];
	char txBuffer[128];
	unsigned int bytesToRead, bytesRead;
	unsigned int bytesToWrite, bytesWritten;
	unsigned char nSTATUS;
	unsigned char CONF_DONE;
 	char ch;

	do {

		// open device
		ftStatus = FT_OpenEx (device, FT_OPEN_BY_SERIAL_NUMBER, &ftHandle);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_OpenEX: %d\n", ftStatus);
			break;
		}
		printf ("Opened device.\n");

		// reset device
		ftStatus = FT_ResetDevice (ftHandle);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_ResetDevice: %d\n", ftStatus);
			break;
		}
		
		// purge receive buffer
		ftStatus = PurgeReceiveBuffer (ftHandle);
		if (ftStatus != FT_OK) {
			printf ("Error: PurgeReceiveBuffer: %d\n", ftStatus);
			break;
		}

		// set transfer sizes to 65536 bytes
		ftStatus = FT_SetUSBParameters (ftHandle, 65536, 65535);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetUSBParameters: %d\n", ftStatus);
			break;
		}

		// disable event and error characters
		ftStatus = FT_SetChars (ftHandle, false, 0, false, 0);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetChars: %d\n", ftStatus);
			break;
		}

		// set read and write timeouts in milliseconds
		ftStatus = FT_SetTimeouts (ftHandle, 0, 5000);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetTimeouts: %d\n", ftStatus);
			break;
		}

		// set latency timer to 1ms
		ftStatus = FT_SetLatencyTimer (ftHandle, 1);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetLatencyTimer: %d\n", ftStatus);
			break;
		}
		
		// disable flow control
		ftStatus = FT_SetFlowControl (ftHandle, FT_FLOW_NONE, 0x00, 0x00);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetFlowControl: %d\n", ftStatus);
			break;
		}

		// reset controller
		ftStatus = FT_SetBitMode (ftHandle, 0x0, 0x00);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetBitMode: %d\n", ftStatus);
			break;
		}

		// enable asynchronous big banged mode - oiii_iooo
		ftStatus = FT_SetBitMode (ftHandle, 0x87, 0x01);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetBitMode: %d\n", ftStatus);
			break;
		}

		// set baud rate
		ftStatus = FT_SetBaudRate (ftHandle, 12000000);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_SetBaudRate: %d\n", ftStatus);
			break;
		}
		printf ("FTDI async bit bang mode configured.\n");

		// set all output lines low
		txBuffer[0] = 0;
		ftStatus = FT_Write (ftHandle, txBuffer, 1, &bytesWritten);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_Write: %d\n", ftStatus);
			break;
		}

		// purge receive buffer
		PurgeReceiveBuffer (ftHandle);

		// wait for nSTATUS and CONF_DONE to be low
		do {
			ftStatus = FT_Read (ftHandle, rxBuffer, 1, &bytesRead);
			if (ftStatus != FT_OK) {
				printf ("Error: FT_Read: %d\n", ftStatus);
				break;
			}
			// printf ("Read %d bytes. Value: %02x\n", bytesRead, rxBuffer[0]);
			nSTATUS = (rxBuffer[0] & nSTATUS_BIT) ? 1 : 0;
			CONF_DONE = (rxBuffer[0] & CONF_DONE_BIT) ? 1 : 0;
		} while ((nSTATUS != 0) || (CONF_DONE != 0));
		if (ftStatus != FT_OK) {
			break;
		}
		printf ("Altera nSTATUS and CONF_DONE are low.\n");

		// set nCONFIG high
		txBuffer[0] |= nCONFIG_BIT;
		ftStatus = FT_Write (ftHandle, txBuffer, 1, &bytesWritten);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_Write: %d\n", ftStatus);
			break;
		}

		// purge receive buffer
		PurgeReceiveBuffer (ftHandle);

		// wait for nSTATUS to be high and CONF_DONE to be low
		do {
			ftStatus = FT_Read (ftHandle, rxBuffer, 1, &bytesRead);
			if (ftStatus != FT_OK) {
				printf ("Error: FT_Read: %d\n", ftStatus);
				break;
			}
			// printf ("Read %d bytes. Value: %02x\n", bytesRead, rxBuffer[0]);
			nSTATUS = (rxBuffer[0] & nSTATUS_BIT) ? 1 : 0;
			CONF_DONE = (rxBuffer[0] & CONF_DONE_BIT) ? 1 : 0;
		} while ((nSTATUS == 0) || (CONF_DONE != 0));
		if (ftStatus != FT_OK) {
			break;
		}
		printf ("Altera nSTATUS is high and CONF_DONE is low.\n");

		// open bitfile
		fbitfile = fopen (bitfile, "r");
		if (!fbitfile) {
			printf ("Unable to open FPGA configuration file.\n");
			break;
		}

		// send bitfile
		bytes = 0;
		printf ("Configuring FPGA");
		while (!feof (fbitfile)) {

			// read byte from file
			fread (&ch, 1, 1, fbitfile);
			if (feof (fbitfile)) {
				break;
			}

			// send byte to device
			for (i = 0; i < 8; i++) {

				// set data bit and set clock low
				txBuffer[2*i+0] = nCONFIG_BIT;
				txBuffer[2*i+0] &= ~DCLK_BIT;
				if (ch & 0x01) {
					txBuffer[2*i+0] |= DATA_BIT;
				} else {
					txBuffer[2*i+0] &= ~DATA_BIT;
				}

				// set data bit and set clock high
				txBuffer[2*i+1] = nCONFIG_BIT;
				txBuffer[2*i+1] |= DCLK_BIT;
				if (ch & 0x01) {
					txBuffer[2*i+1] |= DATA_BIT;
				} else {
					txBuffer[2*i+1] &= ~DATA_BIT;
				}

				// next bit
				ch = ch >> 1;
			}

			// write two bytes
			ftStatus = FT_Write (ftHandle, txBuffer, 16, &bytesWritten);
			if (ftStatus != FT_OK) {
				printf ("\nError: FT_Write: %d\n", ftStatus);
				break;
			}

			bytes++;

			if ((bytes & 0x3ff) == 0x3FF) {
				printf ("."); 
				fflush (stdout);
			}
		}
		printf ("\n");

		// clsoe bitfile
		fclose (fbitfile);

		// send one extra bit and end with clock low
		txBuffer[0] = nCONFIG_BIT;
		txBuffer[1] = nCONFIG_BIT;
		txBuffer[1] |= DCLK_BIT;
		txBuffer[2] = nCONFIG_BIT;
		ftStatus = FT_Write (ftHandle, txBuffer, 3, &bytesWritten);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_Write: %d\n", ftStatus);
			break;
		}

		// check state of CONF_DONE pin
		printf ("Device configured.\n");

		// return status
		status = 0;

	} while (0);

	// if device is opened, close it
	if (ftHandle != NULL) {
		FT_Close (ftHandle);
		ftHandle = NULL;
		printf ("Closed device.\n");
	}

	return status;
}


FT_STATUS PurgeReceiveBuffer (FT_HANDLE ftHandle)
{
	FT_STATUS ftStatus;
	char rxBuffer[128];
	unsigned int bytesToRead, bytesRead;

	do {

		// purge receive buffer
		ftStatus = FT_GetQueueStatus (ftHandle, &bytesToRead);
		if (ftStatus != FT_OK) {
			printf ("Error: FT_GetQueueStatus: %d\n", ftStatus);
			break;
		}
		while (bytesToRead > 0) {
			ftStatus = FT_Read (ftHandle, rxBuffer, 1, &bytesRead);
			if (ftStatus != FT_OK) {
				break;
			}
			bytesToRead -= bytesRead;
		}
		if (ftStatus != FT_OK) {
			printf ("Error: FT_Read: %d\n", ftStatus);
			break;
		}
		printf ("Purged device RX channel.\n");

	} while (0);

	return ftStatus;
}
