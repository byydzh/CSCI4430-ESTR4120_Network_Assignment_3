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

static const int MAX_MESSAGE_SIZE = 256;

int main(int argc, const char **argv){
    string hostname = string(argv[1]);
    int port = atoi(argv[2]);
    int size = atoi(argv[3]);
    string input = string(argv[4]);
    string log = string(argv[5]);

    send_start(hostname,port, input,log, size);
    return 0;
}