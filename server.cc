#include<iostream>
#include<fstream>
#include<memory>
#include<poll.h>
#include<queue>
#include<boost/lexical_cast.hpp>
#include"helper.h"

#define MAX_MESS_LEN SERVER_MESSAGE_LEN
#define RCV_BUFFER_SIZE (CLIENT_MESSAGE_LEN+1)
#define DGRAM_QUEUE_LEN 4096
#define MAX_CLIENTS 42
#define TIMEOUT 120

class DgramQueue {
    size_t beg;
    size_t len;
    char message[DGRAM_QUEUE_LEN][CLIENT_MESSAGE_LEN];
    std::queue<struct sockaddr_in> queue[DGRAM_QUEUE_LEN];

    inline size_t end() { return (beg + len - 1) % DGRAM_QUEUE_LEN; }

  public:

    DgramQueue() {
        beg = len = (size_t)0;
    }

    // Cannot add new message without at least one receiver
    void addMessage(char* new_mess, const struct sockaddr_in& addr) {
        if(len < DGRAM_QUEUE_LEN) {
            ++len;
            memcpy(message[end()], new_mess, CLIENT_MESSAGE_LEN);
            std::cout << "added char: " << message[end()][8];
        }
        else {
            memcpy(message[beg], new_mess, CLIENT_MESSAGE_LEN);
            ++beg;
            beg %= DGRAM_QUEUE_LEN;
        }
        std::queue<struct sockaddr_in>{}.swap(queue[end()]);
        queue[end()].push(addr);
    }

    void addAddr(const struct sockaddr_in& addr) {
        queue[end()].push(addr);
    }

    bool empty() {
        return len == 0;
    }

    void top(char* next_message, struct sockaddr_in* next_addr) {
        if(empty()) throw std::runtime_error{"Trying to read from an empty queue!"};
        memcpy(next_message, message[beg], CLIENT_MESSAGE_LEN);
        *next_addr = queue[beg].front();

    }

    void pop() {
        if(empty()) throw std::runtime_error{"Trying to pop from an empty queue!"};
        queue[beg].pop();
        if(queue[beg].empty()) {
            --len;
            ++beg;
            beg %= DGRAM_QUEUE_LEN;
        }

    }
};

int main(int argc, char** argv) {
    int sock;
    uint16_t port;
    char message[MAX_MESS_LEN];
    size_t mess_size;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    struct pollfd poll_sock;

    char rcv_buffer[RCV_BUFFER_SIZE];
    timestamp_t timestamp;
    in_addr_t client_ip;
    uint16_t client_port;
    char client_name[INET_ADDRSTRLEN];

    struct sockaddr_in addr_list[MAX_CLIENTS];
    time_t time_list[MAX_CLIENTS];
    for(size_t i = 0; i < MAX_CLIENTS; ++i) {
        time_list[i] = 0;
    }



    try {
        if(argc != 3)
            throw std::invalid_argument{"Usage: " + std::string{argv[0]} + " port filename"};

        try {
            port = boost::lexical_cast<uint16_t>(argv[1]);
        } catch(boost::bad_lexical_cast& e) {
            throw std::invalid_argument{"Port must be a 16-bit unsigned integer!"};
        }

        std::ifstream file;
        file.open(argv[2]);
        if(!file.is_open()) throw std::runtime_error{"Failed to open the file!"};

        size_t mess_pos = 9;
        while((message[mess_pos]= (char)file.get()) != EOF) {
            ++mess_pos;
            if(mess_pos >= MAX_MESS_LEN - 1) break;
        }

        message[mess_pos] = '\0';
        mess_size = mess_pos + 1;
        auto socket_wrap = UDPSocketWrap{true};
        sock = socket_wrap.fd;

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htobe32(INADDR_ANY); // all interfaces
        server_addr.sin_port = htobe16(port);

        if(bind(sock, (struct sockaddr *) &server_addr, (socklen_t) sizeof(server_addr)) < 0)
            throw std::runtime_error
                {"Error on binding socket to address: " + std::string{strerror(errno)}};

        poll_sock.fd = sock;

        DgramQueue dgram_queue{};

        while(true) {
            poll_sock.revents = 0;
            poll_sock.events = POLLIN;
            if(!dgram_queue.empty()) {
                poll_sock.events |= POLLOUT;
            }
            if(poll(&poll_sock, (nfds_t)1, -1) < 0) {
                throw std::runtime_error{"Fatal error on poll attempt: " + std::string{strerror(errno)}};
            }


            if(poll_sock.revents & POLLERR) {
                throw std::runtime_error{"Fatal error occured on socket"};
            }

            if(poll_sock.revents & POLLOUT) {
                dgram_queue.top(message, &client_addr);

                ssize_t rc = sendto(sock, message, mess_size, 0,
                                    (struct sockaddr*)&client_addr, (socklen_t)sizeof(client_addr));
                if(rc < 0) {
                    if(errno & EMSGSIZE) {
                        perror("The message is too long to send");
                        dgram_queue.pop();
                    }
                    else if(errno & (EWOULDBLOCK | EAGAIN)) {}
                    else {
                        throw std::runtime_error{"Fatal error on sending file: " + std::string{strerror(errno)}};
                    }
                }
                else {
                    if(rc < (ssize_t)mess_size) {
                        std::cerr << "Data only partially send!" << std::endl;
                    }
                    dgram_queue.pop();
                }
            }


            if(poll_sock.revents & POLLIN) {
                client_addr_len = (socklen_t)sizeof(client_addr);
                ssize_t rc = recvfrom(sock, rcv_buffer, RCV_BUFFER_SIZE, 0,
                                      (struct sockaddr*)&client_addr, &client_addr_len);
                if(rc < 0) {
                    throw std::runtime_error{"Fatal error on reading from socket" + std::string(strerror(errno))};
                }
                else {
                    time_t current_time = time(nullptr);
                    client_ip = client_addr.sin_addr.s_addr;
                    client_port = client_addr.sin_port;
                    size_t found_index = MAX_CLIENTS;
                    for(size_t i = 0; i < MAX_CLIENTS; ++i) {
                        if(time_list[i] < current_time) found_index = i;
                        if(addr_list[i].sin_port == client_port && addr_list[i].sin_addr.s_addr == client_ip) {
                            found_index = i;
                            break;
                        }
                    }
                    addr_list[found_index] = client_addr;
                    time_list[found_index] = current_time + TIMEOUT;

                    inet_ntop(AF_INET, (void*)(&(client_addr.sin_addr)), client_name, INET_ADDRSTRLEN);
                    if(rc != 9) {
                        std::cerr << "Incorrect message received from: "<< client_name << std::endl;
                    }
                    // This line causes compile error, but we know what we are doing
                    timestamp = ((timestamp_t*)rcv_buffer)[0];
                    timestamp = be64toh(timestamp);
                    if (timestamp >= FIRST_WRONG_STAMP)
                        std::cerr << "Received too high timestamp from: " << client_name << std::endl;
                    else {
                        bool message_added = false;
                        for(size_t i = 0; i < MAX_CLIENTS; ++i) {
                            if(i != found_index && time_list[i] >= current_time) {
                                if(!message_added) {
                                    dgram_queue.addMessage(rcv_buffer, addr_list[i]);
                                    message_added = true;
                                }
                                else {
                                    dgram_queue.addAddr(addr_list[i]);
                                }
                            }
                        }
                    }
                }
            }
        }

    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }

    return 0;
}