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
#include <random>
#include <functional>
#include <ctime>

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

vector<string> read_file_chunks(const string& file_path) {
    vector<string> result;

    ifstream file(file_path, ios::in | ios::binary);
    if (file.is_open()) {
        file.seekg(0, ios::end);
        streamsize file_size = file.tellg();
        file.seekg(0, ios::beg);

        int num_strings = (file_size + MAX_ACTUAL_SIZE - 1) / MAX_ACTUAL_SIZE;

        for (int i = 0; i < num_strings; i++) {
            string buffer(MAX_ACTUAL_SIZE, '\0');
            file.read(&buffer[0], MAX_ACTUAL_SIZE);
            streamsize bytes_read = file.gcount();
            buffer.resize(bytes_read);
            result.push_back(buffer);
        }

        file.close();
    } else {
        cerr << "wSender failed to open file: " << file_path << endl;
    }

    return result;
}

unsigned long long generate_seed() {
    auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();

    random_device rd;
    auto seed = (hash<unsigned long long>()(timestamp) ^ rd());

    return seed;
}

int main(int argc, const char **argv){
    string hostname = string(argv[1]);
    int port = atoi(argv[2]);
    int window_size = atoi(argv[3]);
    string input_dir = string(argv[4]);
    string log = string(argv[5]);


    //clear the log file
    out2log(log, 0, 0, 0, 0, 1);

    srand(generate_seed());
    PacketHeader myHeader;
    myHeader.checksum = 0;
    myHeader.length = 0; // Length of data; 0 for ACK, START and END packets
    myHeader.seqNum = rand()<<8;
    myHeader.type = 0; // 0: START; 1: END; 2: DATA; 3: ACK
    unsigned int main_seqNum = myHeader.seqNum;

    struct sockaddr_in addr;
    int sock;
    int valread;
    socklen_t slen = sizeof(addr);
    char buf[MAX_MESSAGE_SIZE] = {0};

    // create socket
    if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        perror("wSender fail to create socket.\n");
        exit(1);
    }

    // clear the memory for address structure
    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    struct hostent* sp = gethostbyname(hostname.c_str()); // get hostname
    memcpy(&addr.sin_addr, sp->h_addr, sp->h_length); // ip address
    addr.sin_port = htons(port); //port


    // Initiate:
    // send the <START, R, 0, 0> 
    slen = sizeof(addr);
    memcpy(buf, &myHeader, sizeof(myHeader));
    //printf("%u [%s]\n",myHeader.seqNum, buf);
    if (sendto(sock, buf, sizeof(buf), MSG_NOSIGNAL, (struct sockaddr *) &addr, slen)==-1)
    {
        perror("wSender error with first sendto().\n");
        exit(1);
    }
    out2log(log, myHeader.type, myHeader.seqNum, myHeader.length, myHeader.checksum);
    auto start = std::chrono::system_clock::now();

    // Receiver: <ACK, R, 0, 0>
    while(1){
        auto end = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (elapsed >= 500){
            memcpy(buf, &myHeader, sizeof(myHeader));
            sendto(sock, buf, sizeof(buf), MSG_NOSIGNAL, (struct sockaddr *) &addr, slen);
            out2log(log, myHeader.type, myHeader.seqNum, myHeader.length, myHeader.checksum);
            auto start = std::chrono::system_clock::now();
        }
        memset(buf, 0, MAX_MESSAGE_SIZE);
        valread = recvfrom(sock, buf, MAX_MESSAGE_SIZE, MSG_WAITALL, (struct sockaddr *) &addr, &slen);
        if (valread == -1)
            continue;
        else
            break;
    }
    buf[valread] = '\0';
    PacketHeader *ACK = (PacketHeader *)buf;
    out2log(log, ACK->type, ACK->seqNum, ACK->length, ACK->checksum);

    if (ACK->type == 3 && myHeader.seqNum == ACK->seqNum){
        printf("wSender connection start!\n");
    }
    else{
        printf("wSender start failed.\n");
        printf("%u %u %u %u",ACK->type, ACK->seqNum, ACK->length, ACK->checksum);
        close(sock);
        return 0;
    }


    // Data transmission:
    vector<string> chunks = read_file_chunks(input_dir);
    int chunks_num = (int)chunks.size();
    vector<int> chunks_flag(chunks_num);
    for (int i = 0; i < chunks_num; i++) {
        chunks_flag[i] = 0;
    }
    chunks_num--;

    int send_ptr = 0;
    int mem_ptr;
    while(1){
        // send window_size times <DATA, i, LEN, SUM>
        mem_ptr = send_ptr;
        for(int i=0; i<window_size; i++){
            if(send_ptr > chunks_num)
                break;
            if(chunks_flag[send_ptr] == 1)
                continue;
            myHeader.seqNum = send_ptr;
            myHeader.type = 2;
            myHeader.length = chunks[send_ptr].length();
            myHeader.checksum = crc32(chunks[send_ptr].c_str(), myHeader.length);
            //printf("\n[%s]\n\n%d\n",chunks[send_ptr].c_str(),  myHeader.length);
            char message[1472] = {0};
            memcpy(message, &myHeader, sizeof(myHeader));
            for(int k = 16;k<1472;k++){
                message[k] = chunks[send_ptr][k-16];
            }
            //memcpy(buf, buf_str.c_str(), myHeader.length + WTP_HEADER);
            //printf("\n[%s]\n\n%d\n",buf,  (int)sizeof(buf));
            sendto(sock, message, sizeof(message), MSG_NOSIGNAL, (struct sockaddr *) &addr, slen);
            out2log(log, myHeader.type, myHeader.seqNum, myHeader.length, myHeader.checksum);
            send_ptr++;
        }

        // receive the <ACK, j, 0, 0>
        auto start = std::chrono::system_clock::now();
        int recv_ACK = 0;
        while(1){
            auto ACK_elapsed = 251;
            memset(buf, 0, MAX_MESSAGE_SIZE);
            valread = recvfrom(sock, buf, MAX_MESSAGE_SIZE, MSG_DONTWAIT, (struct sockaddr *) &addr, &slen);
            if (valread >= 0){
                buf[valread] = '\0';
                PacketHeader *ACK = (PacketHeader *)buf;
                //send_ptr = ACK->seqNum;
                chunks_flag[ACK->seqNum] = 1;
                out2log(log, ACK->type, ACK->seqNum, ACK->length, ACK->checksum);
                recv_ACK += 1;
                /*if(recv_ACK == 1){
                    auto first_ACK = std::chrono::system_clock::now();
                    ACK_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(first_ACK - start).count();
                    continue;
                }*/
            }

            // timeout
            auto end = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            if (elapsed >= 500){
                //send_ptr = mem_ptr;
                break;
            }
            if( recv_ACK >= window_size ){
                break;
            }
        }

        send_ptr = mem_ptr + window_size;
        for(int i=mem_ptr; i < mem_ptr + window_size; i++){
            if (chunks_flag[i] == 0){
                send_ptr = i;
                break;
            }
        }
        //printf("send_ptr: %d \n", send_ptr);

        if(send_ptr > chunks_num)
            break;
    }

    // Finish: 
    // Sender: <END, R, 0, 0>
    myHeader.seqNum = main_seqNum;
    myHeader.type = 1;
    myHeader.length = 0;
    myHeader.checksum = 0;
    memcpy(buf, &myHeader, sizeof(myHeader));
    if (sendto(sock, buf, sizeof(buf), MSG_NOSIGNAL, (struct sockaddr *) &addr, slen)==-1)
    {
        perror("wSender error with last sendto().\n");
        exit(1);
    }
    out2log(log, myHeader.type, myHeader.seqNum, myHeader.length, myHeader.checksum);
    start = std::chrono::system_clock::now();

    // Receiver: <ACK, R, 0, 0>
    while(1){
        auto end = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (elapsed >= 500){
            memcpy(buf, &myHeader, sizeof(myHeader));
            sendto(sock, buf, sizeof(buf), MSG_NOSIGNAL, (struct sockaddr *) &addr, slen);
            out2log(log, myHeader.type, myHeader.seqNum, myHeader.length, myHeader.checksum);
            auto start = std::chrono::system_clock::now();
        }
        memset(buf, 0, MAX_MESSAGE_SIZE);
        valread = recvfrom(sock, buf, MAX_MESSAGE_SIZE, MSG_WAITALL, (struct sockaddr *) &addr, &slen);
        if (valread == -1)
            continue;
        else{
            buf[valread] = '\0';
            ACK = (PacketHeader *)buf;
            out2log(log, ACK->type, ACK->seqNum, ACK->length, ACK->checksum);
            if (ACK->type == 3 && myHeader.seqNum == ACK->seqNum)
                break;
        }

    }


    close(sock); // close socket

    return 0;
}