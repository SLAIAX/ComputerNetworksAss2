//159.334 - Networks
// SERVER: prototype for assignment 2.
//Note that this progam is not yet cross-platform-capable
// This code is different than the one used in previous semesters...
//************************************************************************/
//RUN WITH: Rserver_UDP 1235 0 0
//RUN WITH: Rserver_UDP 1235 1 0 
//RUN WITH: Rserver_UDP 1235 0 1 
//RUN WITH: Rserver_UDP 1235 1 1  
//************************************************************************/

//---
#if defined __unix__ || defined __APPLE__
	#include <unistd.h>
	#include <errno.h>
	#include <stdlib.h>
	#include <stdio.h> 
	#include <string.h>
	#include <sys/types.h> 
	#include <sys/socket.h> 
	#include <arpa/inet.h>
	#include <netinet/in.h> 
	#include <netdb.h>
    #include <iostream>
#elif defined _WIN32 


	//Ws2_32.lib
	#define _WIN32_WINNT 0x501  //to recognise getaddrinfo()

	//"For historical reasons, the Windows.h header defaults to including the Winsock.h header file for Windows Sockets 1.1. The declarations in the Winsock.h header file will conflict with the declarations in the Winsock2.h header file required by Windows Sockets 2.0. The WIN32_LEAN_AND_MEAN macro prevents the Winsock.h from being included by the Windows.h header"
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif

	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <stdio.h> 
	#include <string.h>
	#include <stdlib.h>
	#include <iostream>
#endif

#include "myrandomizer.h" 

using namespace std;

#define BUFFER_SIZE 80  //used by receive_buffer and send_buffer
                        //the BUFFER_SIZE needs to be at least big enough to receive the packet
#define SEGMENT_SIZE 78

const int ARG_COUNT=4;
int numOfPacketsDamaged=0;
int numOfPacketsLost=0;
int numOfPacketsUncorrupted=0;

int packets_damagedbit=0;
int packets_lostbit=0;

#define GENERATOR 0x8005 //0x8005, generator for polynomial division

//for the assignments you can copy and past CRCpolynomial() directly in your C code.
unsigned int CRCpolynomial(char *buffer){
	unsigned char i;
	unsigned int rem=0x0000;
    unsigned int bufsize=strlen(buffer);
	
	while(bufsize--!=0){
		for(i=0x80;i!=0;i/=2){
			if((rem&0x8000)!=0){
				rem=rem<<1;
				rem^=GENERATOR;
			} else{
	   	       rem=rem<<1;
		    }
	  		if((*buffer&i)!=0){
			   rem^=GENERATOR;
			}
		}
		buffer++;
	}
	rem=rem&0xffff;
	return rem;
}

//*******************************************************************
//Function to save lines and discard the header
//*******************************************************************
//You are allowed to change this. You will need to alter the NUMBER_OF_WORDS_IN_THE_HEADER if you add a CRC
#define NUMBER_OF_WORDS_IN_THE_HEADER 3

void extractTokens(char *str, int &CRC, char *command, int &packetNumber, char *data){
	char * pch;
     
  int tokenCounter=0;
  printf ("Splitting string \"%s\" into tokens:\n\n",str);
  
  while (1)
  {
	if(tokenCounter ==0){ 
        pch = strtok (str, " ,.-'\r\n'");
    } else if(tokenCounter==3){
    	pch = strtok (NULL, ",.-'\r\n'");
    } else {
		pch = strtok (NULL, " ,.-'\r\n'");
	}
	if(pch == NULL) break;
	printf ("Token[%d], with %d characters = %s\n",tokenCounter,int(strlen(pch)),pch);
	 
    switch(tokenCounter){
      	case 0: 
      		CRC = atoi(pch);
			break;
      	case 1: 	
      		//command = new char[strlen(pch)];
			strcpy(command, pch);
		    printf("command = %s, %d characters\n", command, int(strlen(command)));
            break;			
		case 2: 
	  		packetNumber = atoi(pch);
	        break;
		case 3: 
			//data = new char[strlen(pch)];
		    strcpy(data, pch);
	        printf("data = %s, %d characters\n", data, int(strlen(data)));
          	break;			
    }	
	tokenCounter++;
  }
}

void save_data(char* data, FILE *fout){
	printf("Saving Data\nData is: %s\n", data);
	if (fout!=NULL) fprintf(fout,"%s\n",data); //write to file
	else {
		printf("Error in writing to write...\n");
		exit(1);
	}
}

void save_line_without_header(char * receive_buffer,FILE *fout){
	//char *sep = " "; //separator is the space character
	
	char sep[3];
	
    strcpy(sep," "); //separator is the space character
	char *word;
	int wcount=0;
	char dataExtracted[BUFFER_SIZE]="\0";
	
	// strtok remembers the position of the last token extracted.
	// strtok is first called using a buffer as an argument.
	// successive calls requires NULL as an argument.
	// the function remembers internally where it stopped last time
	
	//loop while word is not equal to NULL.
	for (word = strtok(receive_buffer, sep); word; word = strtok(NULL, sep))
	{
		wcount++;
		if(wcount > NUMBER_OF_WORDS_IN_THE_HEADER){ //if the word extracted is not part of the header anymore
			strcat(dataExtracted, word); //extract the word and store it as part of the data
			strcat(dataExtracted, " "); //append space
		}
	}
	
	dataExtracted[strlen(dataExtracted)-1]=(char)'\0'; //get rid of last space appended
	printf("DATA: %s, %d elements\n",dataExtracted,int(strlen(dataExtracted)));

	//make sure that the file pointer has been properly initialised
	if (fout!=NULL) fprintf(fout,"%s\n",dataExtracted); //write to file
	else {
		printf("Error in writing to write...\n");
		exit(1);
	}
}

#define WSVERS MAKEWORD(2,0)
WSADATA wsadata;

//*******************************************************************
//MAIN
//*******************************************************************
int main(int argc, char *argv[]) {
//********************************************************************
// INITIALIZATION
//********************************************************************
	struct sockaddr_storage clientAddress; //IPv4 & IPv6 -compliant
	struct addrinfo *result = NULL;
    struct addrinfo hints;
	int iResult;



	/// Our work
	int CRC;
	char command[256];
	int packetNumber;
	char data[256];
	int prevPacket = 99;
	int expectedSeqNum = 0;

	memset(data, 0, sizeof(data));
	memset(command, 0, sizeof(command));
	
    SOCKET s;
    char send_buffer[BUFFER_SIZE],receive_buffer[BUFFER_SIZE];
    int n,bytes,addrlen;

	memset(&hints, 0, sizeof(struct addrinfo));
	 
	hints.ai_family = AF_INET; //AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE; // For wildcard IP address 
	
	
    randominit();
//********************************************************************
// WSSTARTUP
//********************************************************************
   if (WSAStartup(WSVERS, &wsadata) != 0) {
      WSACleanup();
      printf("WSAStartup failed\n");
   }
	
	if (argc != ARG_COUNT) {
	   printf("USAGE: Rserver_UDP localport allow_corrupted_bits(0 or 1) allow_packet_loss(0 or 1)\n");
	   exit(1);
   }
   
	iResult = getaddrinfo(NULL, argv[1], &hints, &result); //converts human-readable text strings representing hostnames or IP addresses 
	                                                        //into a dynamically allocated linked list of struct addrinfo structures
																			  //IPV4 & IPV6-compliant
 
	if (iResult != 0) {
    printf("getaddrinfo failed: %d\n", iResult);
    WSACleanup();
    return 1;
   }	
	
//********************************************************************
//SOCKET
//********************************************************************
   s = INVALID_SOCKET; //socket for listening
	// Create a SOCKET for the server to listen for client connections

	s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	//check for errors in socket allocation
	if (s == INVALID_SOCKET) {
		 printf("Error at socket(): %d\n", WSAGetLastError());
		 freeaddrinfo(result);
		 WSACleanup();
		 exit(1);//return 1;
	}
	
   
   
	
   packets_damagedbit=atoi(argv[2]);
   packets_lostbit=atoi(argv[3]);
   if (packets_damagedbit < 0 || packets_damagedbit > 1 || packets_lostbit < 0 || packets_lostbit > 1){
	   printf("USAGE: Rserver_UDP localport allow_corrupted_bits(0 or 1) allow_packet_loss(0 or 1)\n");
	   exit(0);
   }
      
//********************************************************************
//BIND
//********************************************************************
   iResult = bind( s, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
		 
        closesocket(s);
        WSACleanup();
        return 1;
    }

    cout << "==============<< UDP SERVER >>=============" << endl;
    cout << "channel can damage packets=" << packets_damagedbit << endl;
    cout << "channel can lose packets=" << packets_lostbit << endl;
	 
	 freeaddrinfo(result); //free the memory allocated by the getaddrinfo 
	                       //function for the server's address, as it is 
	                       //no longer needed
//********************************************************************
// Open file to save the incoming packets
//********************************************************************
//In text mode, carriage return�linefeed combinations 
//are translated into single linefeeds on input, and 
//linefeed characters are translated to carriage return�linefeed combinations on output.    
	 FILE *fout=fopen("data_received.txt","w"); 
	 
//********************************************************************
	 
//INFINITE LOOP
//********************************************************************
    while (1) {
//********************************************************************
//RECEIVE
//********************************************************************
//printf("Waiting... \n");		
		addrlen = sizeof(clientAddress); //IPv4 & IPv6-compliant
		memset(receive_buffer,0,sizeof(receive_buffer));
		bytes = recvfrom(s, receive_buffer, SEGMENT_SIZE, 0, (struct sockaddr*)&clientAddress, &addrlen);

//********************************************************************
//IDENTIFY UDP client's IP address and port number.     
//********************************************************************      
		char clientHost[NI_MAXHOST]; 
	    char clientService[NI_MAXSERV];	
	    memset(clientHost, 0, sizeof(clientHost));
	    memset(clientService, 0, sizeof(clientService));


	    getnameinfo((struct sockaddr *)&clientAddress, addrlen,
	                  clientHost, sizeof(clientHost),
	                  clientService, sizeof(clientService),
	                  NI_NUMERICHOST);



	    printf("\nReceived a packet of size %d bytes from <<<UDP Client>>> with IP address:%s, at Port:%s\n",bytes,clientHost, clientService); 	   
		
//********************************************************************
//PROCESS RECEIVED PACKET
//********************************************************************
		
		//Remove trailing CR and LF
		n=0;
		while(n < bytes){
			n++;
			if ((bytes < 0) || (bytes == 0)) break;
			if (receive_buffer[n] == '\n') { /*end on a LF*/
				receive_buffer[n] = '\0';
				break;
			}
			if (receive_buffer[n] == '\r') /*ignore CRs*/
				receive_buffer[n] = '\0';
		}
		
		if ((bytes < 0) || (bytes == 0)) break;
		
		printf("\n================================================\n");	
		printf("RECEIVED --> %s \n",receive_buffer);


		extractTokens(receive_buffer, CRC, command, packetNumber, data);
		char packetInfo[1024];
		memset(packetInfo, 0, sizeof(packetInfo));
		sprintf(packetInfo, "%s %d %s", command, packetNumber, data);
		char temp_buffer[BUFFER_SIZE];
		int recvdCRC = CRCpolynomial(packetInfo);
		int sendCRC;
		if(recvdCRC != CRC){
			sprintf(temp_buffer,"NAK %d",packetNumber);
			sendCRC = CRCpolynomial(temp_buffer);
			sprintf(send_buffer, "%d %s \r\n", sendCRC, temp_buffer);
			send_unreliably(s,send_buffer,(sockaddr*)&clientAddress );
			continue;
		} else {
			if(expectedSeqNum == packetNumber){
				expectedSeqNum++;
				sprintf(temp_buffer,"ACK %d",packetNumber);
			} else if(packetNumber < expectedSeqNum){
				sprintf(temp_buffer,"ACK %d",(packetNumber));
				sendCRC = CRCpolynomial(temp_buffer);
				sprintf(send_buffer, "%d %s \r\n", sendCRC, temp_buffer);
				send_unreliably(s,send_buffer,(sockaddr*)&clientAddress);
				continue;
			}	else {
				sprintf(temp_buffer,"ACK %d",(expectedSeqNum - 1));
				sendCRC = CRCpolynomial(temp_buffer);
				sprintf(send_buffer, "%d %s \r\n", sendCRC, temp_buffer);
				send_unreliably(s,send_buffer,(sockaddr*)&clientAddress);
				continue;
			}	
		}
		sendCRC = CRCpolynomial(temp_buffer);
		sprintf(send_buffer, "%d %s \r\n", sendCRC, temp_buffer);
		send_unreliably(s,send_buffer,(sockaddr*)&clientAddress);
		// Check for order 
		if (strcmp(command, "PACKET")==0)  {
//********************************************************************
//SEND ACK
//********************************************************************
			if(prevPacket != packetNumber){
				save_data(data,fout);
				prevPacket = packetNumber;
			}
		}
		else if(strcmp(command, "CLOSE")==0)  {//if client says "CLOSE", the last packet for the file was sent. Close the file
			//Remember that the packet carrying "CLOSE" may be lost or damaged as well!
			//sprintf(send_buffer,"ACK %d \r\n",packetNumber);
			//send_unreliably(s,send_buffer,(sockaddr*)&clientAddress ); 
			fclose(fout);
			closesocket(s);
			printf("Server saved data_received.txt \n");//you have to manually check to see if this file is identical to file1_Windows.txt
			printf("Closing the socket connection and Exiting...\n");
			break;
		}
   }
   closesocket(s);

   cout << "==============<< STATISTICS >>=============" << endl;
   cout << "numOfPacketsDamaged=" << numOfPacketsDamaged << endl;
   cout << "numOfPacketsLost=" << numOfPacketsLost << endl;
   cout << "numOfPacketsUncorrupted=" << numOfPacketsUncorrupted << endl;
   cout << "===========================================" << endl;

   exit(0);
}
