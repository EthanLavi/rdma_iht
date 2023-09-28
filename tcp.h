#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <cstdio>
#include <string>
#include <string.h>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <vector>

#define PORT 8002
#define MESSAGE_SIZE 32

namespace tcp {
struct message {
  union {
    struct {
      uint64_t first;
      uint64_t second;
      uint64_t third;
      uint64_t fourth;
    } ints;
    char data[MESSAGE_SIZE];
  } content;
  char padding = '\0';

  message(uint64_t first_ = 0, uint64_t second_ = 0, uint64_t third_ = 0, uint64_t fourth_ = 0){
    content.ints.first = first_;
    content.ints.second = second_;
    content.ints.third = third_;
    content.ints.fourth = fourth_;
  }

  uint64_t get_first(){
    return content.ints.first;
  }
  uint64_t get_second(){
    return content.ints.second;
  }
  uint64_t get_third(){
    return content.ints.third;
  }
  uint64_t get_fourth(){
    return content.ints.fourth;
  }
};

void error(const char* message){
    fprintf(stderr, "%s", message);
    fprintf(stderr, "\n");
    exit(1);
}

class SocketManager {
private:
  int sockfd;
  struct sockaddr_in address;
  std::vector<int> client_sockets;
  static SocketManager* self;

  // Secret true constructor
  SocketManager(){
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int result = 0;
    if (sockfd == -1) error("Cannot open socket");
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;
    result = bind(sockfd, (struct sockaddr*) &address, sizeof(address));
    if (result == -1){ close(sockfd); error("Cannot bind to socket"); }
    result = listen(sockfd, 10);
    if (result == -1){ close(sockfd); error("Cannot listen on socket"); }
  }
public:
  // Cannot be copied
  SocketManager(const SocketManager& obj) = delete;

  bool accept_conn(){
    int address_size = sizeof(address);
    int client_sockfd = accept(sockfd, (struct sockaddr*) &address, (socklen_t*) &address_size);
    if (client_sockfd == -1) return false;
    client_sockets.push_back(client_sockfd);
    return true;
  }

  void send_to_all(message* send_buffer){
    for(int i = 0; i < client_sockets.size(); i++){
      write(client_sockets[i], send_buffer->content.data, MESSAGE_SIZE + 1);
    }
  }

  int get_num_clients(){
    return client_sockets.size();
  }

  void recv_from_all(message** recv_buffer){
    for(int i = 0; i < client_sockets.size(); i++){
      read(client_sockets[i], recv_buffer[i]->content.data, MESSAGE_SIZE + 1);
    }
  }

  static SocketManager* getInstance(){
    if (self == NULL){
      self = new SocketManager();
    }
    return self;
  }

  static void releaseResources(){
    for(int i = 0; i < self->client_sockets.size(); i++){
      close(self->client_sockets[i]);
    }
    close(self->sockfd);
    delete self;
  }
};

class EndpointManager {
private:
  int sockfd;
  static EndpointManager* self;

  // Secret true constructor
  EndpointManager(const char* ip){
    int result = 0;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) error("Cannot open socket");
    struct hostent* server = gethostbyname(ip);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    // Check if server is null (means could not resolve hostname)
    if (server == NULL || server->h_addr_list == NULL){ close(sockfd); error("Could not resolve hostname"); }
    struct in_addr** host_ip_list = (struct in_addr**) server->h_addr_list;
    auto host_ip = *host_ip_list[0];
    result = inet_pton(AF_INET, inet_ntoa(host_ip), &address.sin_addr);  // Pointer (to String) to Number.
    if (result <= 0){ close(sockfd); error("Cannot inet pton"); }
    result = connect(sockfd, (struct sockaddr*) &address, sizeof(address)); 
    if (result == -1){ close(sockfd); error("Cannot connect to foreign socket"); }
  }

public:
  // Cannot be copied
  EndpointManager(const EndpointManager& obj) = delete;

  void send_server(message* send_buffer){
    int status = write(sockfd, send_buffer->content.data, MESSAGE_SIZE + 1);
    if (status == -1) error("Cannot send data over socket");
  }

  void recv_server(message* recv_buffer){
    int status = read(sockfd, recv_buffer->content.data, MESSAGE_SIZE + 1);
    if (status == -1) error("Cannot read data over socket");
  }

  static EndpointManager* getInstance(const char* ip){
    if (self == NULL){
      if (ip == NULL) error("Initial IP is null");
      self = new EndpointManager(ip);
    }
    return self;
  }

  static void releaseResources(){
    delete self;
  }
};

/// @brief Return a socket descripter
/// @param imserver If this is the server in the connection management
/// @param ip IP that identifies the target. If we are the server, the IP is irrelevant and we can just pass our own
/// @return socket descriptor
int link_with(bool imserver, const char* ip){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0), result = 0;
    if (sockfd == -1) error("Cannot open socket");
    struct hostent* server = gethostbyname(ip);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);

    // Check if server is null (means could not resolve hostname)
    if (server == NULL || server->h_addr_list == NULL){ close(sockfd); error("Could not resolve hostname"); }

    // Load socket information
    if (imserver){
        address.sin_addr.s_addr = INADDR_ANY;
        result = bind(sockfd, (struct sockaddr*) &address, sizeof(address));
        if (result == -1){ close(sockfd); error("Cannot bind to socket"); }
        result = listen(sockfd, 1);
        if (result == -1){ close(sockfd); error("Cannot listen on socket"); }
        int address_size = sizeof(address);
        result = accept(sockfd, (struct sockaddr*) &address, (socklen_t*) &address_size);
        if (result == -1){ close(sockfd); error("Cannot accept on new socket"); }
        close(sockfd);
        return result;
    } else {
        struct in_addr** host_ip_list = (struct in_addr**) server->h_addr_list;
        auto host_ip = *host_ip_list[0];
        ROME_INFO("{}", inet_ntoa(host_ip));
        result = inet_pton(AF_INET, inet_ntoa(host_ip), &address.sin_addr);  // Pointer (to String) to Number.
        if (result <= 0){ close(sockfd); error("Cannot inet pton"); }
        result = connect(sockfd, (struct sockaddr*) &address, sizeof(address)); 
        if (result == -1){ close(sockfd); error("Cannot connect to foreign socket"); }
        return sockfd;
    }
}

void Write(int sockfd, message* send_buffer){
    int status = write(sockfd, send_buffer->content.data, MESSAGE_SIZE + 1);
    if (status == -1) error("Cannot send data over socket");
}

void Read(int sockfd, message* recv_buffer){
    int status = read(sockfd, recv_buffer->content.data, MESSAGE_SIZE + 1);
    if (status == -1) error("Cannot read data over socket");
}

}