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
            unsigned int length, unsigned int checksum, int clear_flag=0){
    FILE *fp;
    if(clear_flag == 1){
        fp = fopen(log_name.c_str(), "w+");
        return;
    }
    else
        fp = fopen(log_name.c_str(), "a");
    fprintf(fp, "%u %u %u %u\n",type ,seqNum ,length ,checksum);
    fclose(fp);
}

int main(int argc, const char **argv){
    // handle input command
    int listen_port = atoi(argv[1]);
    int window_size = atoi(argv[2]);
    string output_dir = string(argv[3]);
    string log_dir = string(argv[4]);

    //clear the log file
    out2log(log_dir, 0, 0, 0, 0, 1);

    // create a UDP socket
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == -1)
    {
        perror("Fail to create socket.\n");
        exit(1);
    }

    // create address structure for sender and receiver
    struct sockaddr_in add_server, add_client;
    socklen_t slen = sizeof(add_client);
    memset((char *) &add_server, 0, sizeof(add_server));

    add_server.sin_family = AF_INET;
    add_server.sin_addr.s_addr = htonl(INADDR_ANY); // ip address
    add_server.sin_port = htons(listen_port); // port adress

    // bind socket to port
    if(bind(s, (struct sockaddr*)&add_server, sizeof(add_server)) == -1)
    {
        perror("Fail to bind.\n");
        exit(1);
    }

    // ready to receive data
    int byte_count, file_count = 0;
    while (1)
    { 
        char data_file_dir[256] = {0};
        sprintf(data_file_dir, "%s/FILE-%d.out", output_dir.c_str(), file_count);

        // wait for a new file
        char buffer[1510] = {0};
        byte_count = recvfrom(s, buffer, 1500, 0, (struct sockaddr *) &add_client, &slen);
        if (byte_count == -1)
        {
            perror("error with recvfrom().\n");
            exit(1);
        }
        buffer[byte_count] = '\0';

        // hanlde start header
        PacketHeader *header = (PacketHeader*)buffer;
        printf("%u\n", header->seqNum);
        out2log(log_dir, header->type, header->seqNum, header->length, header->checksum, 0);

        //generate ack header
        header->type = 3; // now header can be used as ack header
        char ack_buffer[1510] = {0};
        int header_length = sizeof(*header);
        memcpy(ack_buffer, header, sizeof(*header));
        sendto(s, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr*) &add_client, slen);
        out2log(log_dir, header->type, header->seqNum, header->length, header->checksum, 0);

        // receive actual data
        char data[2000][1510] = {0}; // assume seqNum < 3000, packetSize < 1510
        int seq_length[3000] = {0};
        int current_seq = -1, end_seq = -1;
        unsigned int my_checksum;
        char data_header_buffer[20] = {0};
        int packet_count = 0;
        // prepare ack header
        PacketHeader ack_header;
        ack_header.type = 3;
        ack_header.length = 0;
        memset(ack_buffer, 0, 1510);
        while (1)
        {
            memset(buffer, 0, 1510);
            byte_count = recvfrom(s, (char *)buffer, 1472, MSG_DONTWAIT, (struct sockaddr *) &add_client, &slen);
            if (byte_count != -1)
            {
                //printf("byte_count: %d\n[%s]\n", byte_count, buffer);
                //packet_count++;
                buffer[byte_count] = '\0';
                // get header info
                for (int j = 0; j < header_length; j++)
                {
                    data_header_buffer[j] = buffer[j];                    
                }
                PacketHeader *data_header = (PacketHeader*)data_header_buffer;
                out2log(log_dir, data_header->type, data_header->seqNum, data_header->length, data_header->checksum, 0);

                // if end type, break
                if(data_header->type == 1)
                {
                    end_seq = data_header->seqNum;
                    printf("end condition\n");
                    ack_header.seqNum = end_seq;
                    ack_header.checksum = 0;
                    memcpy(ack_buffer, &ack_header, header_length);
                    sendto(s, ack_buffer, 100, 0, (struct sockaddr*) &add_client, slen);
                    out2log(log_dir, ack_header.type, ack_header.seqNum, ack_header.length, ack_header.checksum, 0);
                    break;
                }

                // else handle data part
                if(data_header->type == 2 && data_header->seqNum > current_seq && data_header->seqNum <= current_seq+window_size)
                {
                    // store data to buffer
                    for (int j = 0; j < data_header->length; j++)
                    {
                        data[data_header->seqNum][j] = buffer[header_length+j];
                    }
                    data[data_header->seqNum][data_header->length] = '\0';

                    // calculate local checksum
                    my_checksum = data_header->checksum;
                    my_checksum = crc32(data[data_header->seqNum], data_header->length);
                    // if not match, drop packet
                    if (my_checksum != data_header->checksum)
                    {
                        memset(data[data_header->seqNum], 0, 1510);
                        continue;
                            //printf("checksum not match: seq%d, current_seq%d, expected checksum: %u, my checksum=%u\n",
                            //data_header->seqNum, current_seq, data_header->checksum, my_checksum);
                            //printf("[%s] \n\n %u",data[data_header->seqNum], data_header->length);
                            //return 0;
                    }
                    //if match, update current_seq
                    else
                    {
                        packet_count++;
                        seq_length[data_header->seqNum] = data_header->length;
                        ack_header.seqNum = data_header->seqNum;
                        ack_header.checksum = 0;
                        memcpy(ack_buffer, &ack_header, header_length);
                        sendto(s, ack_buffer, 100, 0, (struct sockaddr*) &add_client, slen);
                        out2log(log_dir, ack_header.type, ack_header.seqNum, ack_header.length, ack_header.checksum, 0);
                        for (int j = current_seq; j <= ack_header.seqNum; j++)
                        {
                            if (j == -1 && seq_length[0] == 0)
                            {
                                current_seq = -1;
                                break;
                            }
                            if (j != -1 && seq_length[j] == 0)
                            {
                                current_seq = j;
                                break;
                            }
                        }
                    }
                }
            }

        }

        // now store data to output file
        ofstream output_stream;
        output_stream.open(data_file_dir, std::ios_base::app);
        if ( !output_stream )
        {
            perror("Fail to open file.\n");
            exit(1);
        }
        for (int i = 0; i <= current_seq; i++)
        {
            output_stream.write(data[i], seq_length[i]);
        }
        if ( !output_stream )
        {
            perror("Fail to write file.\n");
            exit(1);
        }
        output_stream.close();
        file_count++;
    }


    return 0;
}
