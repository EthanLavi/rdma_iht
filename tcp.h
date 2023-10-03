#pragma once

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
#include <mutex>
#include <thread>
#include <unordered_map>

#define PORT 8001
#define MESSAGE_SIZE 32

namespace tcp {
/// A struct as a standard for communicating as a server-client. It has the capability to store 4 64-bit ints.
/// In practice, I've only needed one, but the extra space comes in handy.
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
  char padding = '\0'; // a forceful null termination, in case I want a string

  /// Construct a message. And unused values are 0
  message(uint64_t first_ = 0, uint64_t second_ = 0, uint64_t third_ = 0, uint64_t fourth_ = 0){
    content.ints.first = first_;
    content.ints.second = second_;
    content.ints.third = third_;
    content.ints.fourth = fourth_;
  }

  /// @brief Unpack the first value
  uint64_t get_first(){
    return content.ints.first;
  }
  /// @brief Unpack the second value
  uint64_t get_second(){
    return content.ints.second;
  }
  /// @brief Unpack the third value
  uint64_t get_third(){
    return content.ints.third;
  }
  /// @brief Unpack the fourth value
  uint64_t get_fourth(){
    return content.ints.fourth;
  }
};

/// @brief Print error message and exit
/// @param message the message to print
void error(const char* message){
    ROME_INFO("{}", message);
    exit(1);
}

/// A class for acting as a server and managing client sockets
/// The interface is the server can only communicate with all the clients at once...
class SocketManager {
private:
  // socket for accepting new connections
  int sockfd = -1;
  // socket information
  struct sockaddr_in address;
  // Client sockets.
  std::vector<int> client_sockets;
  static std::mutex syncer;
  // self pointer to avoid double instantiation
  static SocketManager* self;

  // Hidden true constructor
  SocketManager(){
    // Create a new socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int result = 0;
    if (sockfd == -1) error("Cannot open socket");
    // Make sure we can re-use the socket. Sometimes our exit script doesn't properly close the socket and this will make it easier to re-run this program
    const int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
      error("setsockopt failed");
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0){
      error("setsockopt failed");
    }

    // Bind it
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;
    result = bind(sockfd, (struct sockaddr*) &address, sizeof(address));
    if (result == -1){ 
      close(sockfd); 
      error("Cannot bind to socket"); 
    }
    // I would likely want the capacity to listen to more. todo: Make this a parameter
    result = listen(sockfd, 10);
    if (result == -1){ 
      close(sockfd); 
      error("Cannot listen on socket"); 
    }
  }
public:
  // Cannot be copied
  SocketManager(const SocketManager& obj) = delete;

  /// @brief Accept a new client and add it to our record
  bool accept_conn(){
    // Accept new connection
    int address_size = sizeof(address);
    int client_sockfd = accept(sockfd, (struct sockaddr*) &address, (socklen_t*) &address_size);
    if (client_sockfd == -1) return false;
    // Record it
    client_sockets.push_back(client_sockfd);
    return true;
  }

  /// @brief Send a message to each client
  void send_to_all(message* send_buffer){
    for(int i = 0; i < client_sockets.size(); i++){
      write(client_sockets[i], send_buffer->content.data, MESSAGE_SIZE + 1);
    }
  }

  /// @brief The number of clients managed by the server
  int num_clients(){
    return client_sockets.size();
  }

  /// @brief Receive a message from all the clients. Make sure recv_buffer has enough room for all num_clients()
  void recv_from_all(message* recv_buffer){
    for(int i = 0; i < client_sockets.size(); i++){
      read(client_sockets[i], recv_buffer[i].content.data, MESSAGE_SIZE + 1);
    }
  }

  /// @brief Get the instance of the socket manager. Can return NULL
  static SocketManager* getInstance(bool no_create = false){
    if (self == NULL && !no_create){
      std::lock_guard<std::mutex> lock(syncer); // make sure we cant get double initialization
      self = new SocketManager();
    }
    return self;
  }

  /// @brief Destroy all the resources
  void releaseResources(){
    for(int i = 0; i < self->client_sockets.size(); i++){
      close(self->client_sockets[i]);
    }
    if (self->sockfd != -1){
      // Only close the socket if safe to do so
      close(self->sockfd);
    }
    if (self != NULL){
      // Only delete self if safe to do so
      delete self;
    }
  }
};
// Initialize pointers to manager as null
std::mutex SocketManager::syncer = std::mutex();
SocketManager* SocketManager::self = nullptr;

class EndpointContext {
  friend class EndpointManager;
  uint16_t id_;

public:
  EndpointContext() : id_(0xFFFF){}
  EndpointContext(uint16_t id) : id_(id){}
};

/// Class for managing an endpoint, (a client to receive stuff from the server)
class EndpointManager {
private:
  // Socket for communication with the server
  int sockfd = -1;
  // Self pointer to manage resources
  static std::mutex syncer;
  static std::unordered_map<uint16_t, EndpointManager*> self;

  // Hidden true constructor
  EndpointManager(const char* ip){
    // create a socket
    int result = 0;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) error("Cannot open socket");
    // Get host by name (i.e. node0)
    struct hostent* server = gethostbyname(ip);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    // Check if server is null (means could not resolve hostname)
    if (server == NULL || server->h_addr_list == NULL){ close(sockfd); error("Could not resolve hostname"); }
    // Get the host ip
    struct in_addr** host_ip_list = (struct in_addr**) server->h_addr_list;
    auto host_ip = *host_ip_list[0];
    // use the host ip for communication
    result = inet_pton(AF_INET, inet_ntoa(host_ip), &address.sin_addr);  // Pointer (to String) to Number.
    if (result <= 0){ close(sockfd); error("Cannot inet pton"); }
    // connect to host ip
    result = connect(sockfd, (struct sockaddr*) &address, sizeof(address)); 
    if (result == -1){ close(sockfd); error("Cannot connect to foreign socket"); }
  }

public:
  // Cannot be copied
  EndpointManager(const EndpointManager& obj) = delete;

  /// @brief send a message to the server
  void send_server(message* send_buffer){
    int status = write(sockfd, send_buffer->content.data, MESSAGE_SIZE + 1);
    if (status == -1) error("Cannot send data over socket");
  }

  /// @brief receive a message from the server
  void recv_server(message* recv_buffer){
    int status = read(sockfd, recv_buffer->content.data, MESSAGE_SIZE + 1);
    if (status == -1) error("Cannot read data over socket");
  }

  /// @brief Check if the endpoint has been initialized
  bool is_init(EndpointContext ctx){
    return self.find(ctx.id_) != self.end();
  }

  /// @brief Get the endpoint's instance. Will connect to host if IP is not null and the endpoint hasn't been initialized.
  static EndpointManager* getInstance(EndpointContext ctx, const char* ip){
    std::lock_guard<std::mutex> lock(syncer);
    uint16_t id = ctx.id_;
    if (id == 0xFFFF) return NULL;
    if (self.find(id) == self.end()){
      if (ip == nullptr) return NULL;
      self[id] = new EndpointManager(ip);
    }
    return self[id];
  }

  /// @brief release the resources
  static void releaseResources(){
    std::lock_guard<std::mutex> lock(syncer);
    for(auto it = self.begin(); it != self.end(); it++){
      int sockfd = (*it).second->sockfd;
      if (sockfd != -1){
        close(sockfd);
      }
      delete (*it).second;
    }
  }
};
std::unordered_map<uint16_t, EndpointManager*> EndpointManager::self(0);
std::mutex EndpointManager::syncer = std::mutex();

}