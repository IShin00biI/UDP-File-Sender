#ifndef SIK_ZADANIE1_MY_EXCEPTIONS_H
#define SIK_ZADANIE1_MY_EXCEPTIONS_H


#define FIRST_WRONG_STAMP 71728934400ULL
#define CLIENT_MESSAGE_LEN 9
#define SERVER_MESSAGE_LEN 65536

#include<iostream>
#include<stdexcept>
#include<string>
#include<cstring>
#include<cerrno>
#include<memory.h>
#include<netdb.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>

using timestamp_t = uint64_t;

class UDPSocketWrap {
  public:
    int fd; // socket's file descriptor

    UDPSocketWrap(bool nonblocking) {
        if((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
            throw std::runtime_error
                {"Error on creating socket: " + std::string{strerror(errno)}};
        if(nonblocking && (fcntl(fd, F_SETFL, O_NONBLOCK) < 0))
            throw std::runtime_error
                {"Error on setting socket to non-blocking: " + std::string{strerror(errno)}};
    }

    ~UDPSocketWrap() {
        if(close(fd) < 0) {
            perror("Closing socket");
        }
    }
};



#endif //SIK_ZADANIE1_MY_EXCEPTIONS_H
