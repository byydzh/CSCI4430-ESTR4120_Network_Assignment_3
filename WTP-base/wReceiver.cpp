#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <vector>
#include <iostream>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <netinet/in.h>

#include "crc32.h"
#include "PacketHeader.h"

using namespace std;
using namespace chrono;

static const int MAX_MESSAGE_SIZE = 1500;
static const int MAX_AVAILABLE_SIZE = 1472;
static const int MAX_ACTUAL_SIZE = 1456;
static const int IP_HEADER = 20;
static const int UDP_HEADER = 8;
static const int WTP_HEADER = 16; 

// clear=0 -> append new information line to log file
// claer=1 -> clean the log file and do NOT write anything
void out2log(string log_name, unsigned int type, unsigned int seqNum,
            unsigned int length, unsigned int checksum, int clear=0){
    FILE *fp;
    if(clear){
        fp = fopen(log_name.c_str(), "w+");
        return;
    }
    else
        fp = fopen(log_name.c_str(), "a");
    fprintf(fp, "%u %u %u %u\n",type ,seqNum ,length ,checksum);
    fclose(fp);
}

int main(int argc, const char **argv){
    int listen_port = atoi(argv[1]);
    int window_size = atoi(argv[2]);
    string output_dir = string(argv[3]);
    string log_dir = string(argv[4]);

    
    return 0;
}