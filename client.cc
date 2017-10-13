#include<boost/lexical_cast.hpp>
#include"helper.h"


#define DEFAULT_PORT 20160
#define MESSAGE_LEN CLIENT_MESSAGE_LEN
#define RCV_BUFFER_LEN SERVER_MESSAGE_LEN

class AddrinfoWrap {
  public:
    struct addrinfo* addr_result;

    AddrinfoWrap(char* node, const struct addrinfo *hints) {
        int32_t rc;
        if((rc = getaddrinfo(node, nullptr, hints, &addr_result)) != 0) {
            throw std::runtime_error
                {"Error on getting address information: " + std::string{gai_strerror(rc)}};
        }
    }

    ~AddrinfoWrap() {
        freeaddrinfo(addr_result);
    }
};



void sendtoWrap(int fd, char* buffer, size_t buffer_len, struct sockaddr_in* dest_addr) {
    ssize_t rc = sendto(fd, buffer, buffer_len, 0, (struct sockaddr*) dest_addr, (socklen_t) sizeof(struct sockaddr_in));
    if(rc <= 0) perror("Failed to send any data");
    else if(rc < (ssize_t)buffer_len) std::cerr << "Data only partially sent!" << std::endl;
}

void consume_data(char* buffer, ssize_t len, struct sockaddr_in* rcvd_addr) {
    if(len < 0) throw std::runtime_error{"Error on receiving data: " + std::string{strerror(errno)}};

    char printable_addr[INET_ADDRSTRLEN];


    bool rcvd_incorrect_mess = (len <= MESSAGE_LEN) || buffer[len - 1] != '\0';
    for(size_t i = MESSAGE_LEN; i < size_t(len - 1); ++i) {
        if(buffer[i] == '\0') {
            rcvd_incorrect_mess = true;
            break;
        }
    }

    if(rcvd_incorrect_mess) std::cerr << "Invalid message received from: " <<
            inet_ntop(AF_INET, (const void*) &(rcvd_addr->sin_addr), printable_addr, INET_ADDRSTRLEN)
                            << ":" << be16toh(rcvd_addr->sin_port) << std::endl;

    else {
        timestamp_t timestamp_be = ((timestamp_t*) buffer)[0];
        std::cout << be64toh(timestamp_be) << " " <<
            buffer[MESSAGE_LEN - 1] << " " << buffer + MESSAGE_LEN << std::endl;

    }

}

void check_cl_args(int argc, char** argv) {
    if((argc != 4 && argc != 5) || strlen(argv[2]) != 1
       || *(argv[1]) == '-' || (argc == 5 && *(argv[4]) == '-'))
        throw std::invalid_argument
            {"Usage: " + std::string{argv[0]} + " timestamp character host [port=20160]"};
}

int main(int argc, char** argv) {
    int sock;
    timestamp_t timestamp, timestamp_be;
    char mess_char;
    char* host;
    uint16_t port = DEFAULT_PORT;

    struct addrinfo addr_hints;
    struct sockaddr_in server_addr;
    socklen_t server_addr_len;

    char message[MESSAGE_LEN];

    char rcv_buffer[RCV_BUFFER_LEN];
    ssize_t bytes_rcvd;

    try {
        check_cl_args(argc, argv);
        try {
            timestamp = boost::lexical_cast<timestamp_t>(argv[1]);
        } catch(boost::bad_lexical_cast& e) {
            throw std::invalid_argument{"Timestamp must be a 64-bit unsigned integer!"};
        }

        if (timestamp >= FIRST_WRONG_STAMP)
                throw std::invalid_argument{"The year in timestamp must be lower than 4243!"};

        mess_char = *(argv[2]);

        host = argv[3];

        try {
            if (argc == 5) port = boost::lexical_cast<uint16_t>(argv[4]);
        } catch(boost::bad_lexical_cast& e) {
            throw std::invalid_argument{"Port must be a 16-bit unsigned integer!"};
        }

        (void) memset(&addr_hints, 0, sizeof(struct addrinfo));
        addr_hints.ai_family = AF_INET;
        addr_hints.ai_socktype = SOCK_DGRAM;
        addr_hints.ai_protocol = IPPROTO_UDP;

        {
            auto wrapped_info = AddrinfoWrap{host, &addr_hints};
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr =
                ((struct sockaddr_in*)
                    (wrapped_info.addr_result->ai_addr))->sin_addr.s_addr; // IP address
            server_addr.sin_port = htobe16(port);
        } //dynamically allocated addrinfo freed by wrapper's dtor

        auto socket_wrap = UDPSocketWrap{false};
        sock = socket_wrap.fd;

        timestamp_be = htobe64(timestamp);
        // This line causes compile error, but we know what we are doing
        ((timestamp_t*)message)[0] = timestamp_be;
        message[8] = mess_char;

        sendtoWrap(sock, message, (size_t) MESSAGE_LEN, &server_addr);

        while(true) {
            (void) memset(rcv_buffer, 0, RCV_BUFFER_LEN);

            server_addr_len = (socklen_t) sizeof(server_addr);

            bytes_rcvd = recvfrom(sock, rcv_buffer, (size_t) RCV_BUFFER_LEN,
                                  0, (struct sockaddr*) &server_addr, &server_addr_len);

            consume_data(rcv_buffer, bytes_rcvd, &server_addr);
        }

    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }

    return 0;
}