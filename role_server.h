#include <algorithm>

#include "structures/iht_ds.h"
#include "structures/hashtable.h"
#include "structures/test_map.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common.h"
#include "protos/experiment.pb.h"

class Server {
public:
  /// @brief Start the server
  /// @param done a bool for inter-thread communication
  /// @param runtime_s how long to wait before listening for finishing messages
  /// @param cleanup a cleanup script to run every 100ms
  /// @return the status
  static absl::Status Launch(volatile bool* done, int runtime_s, std::function<void()> cleanup) {
    // Sleep while clients are running if there is a set runtime.
    if (runtime_s > 0) {
      ROME_INFO("SERVER :: Sleeping for {}", runtime_s);

      for(int it = 0; it < runtime_s * 10; it++){
        // We sleep for 1 second, runtime times, and do intermitten cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check for server related cleanup
        cleanup();
      }
    }

    // Sync with the clients
    while(!(*done)){
      // Sleep for 1/10 second while not done
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // Check for server related cleanup
      cleanup();
    }

    // Wait for all clients to be done.
    tcp::SocketManager* socket_handle = tcp::SocketManager::getInstance();
    tcp::message recv_buffer[socket_handle->num_clients()];
    socket_handle->recv_from_all(recv_buffer);
    ROME_INFO("SERVER :: received ack");
    
    tcp::message send_buffer;
    socket_handle->send_to_all(&send_buffer);
    ROME_INFO("SERVER :: sent ack");
    return absl::OkStatus();
  }
};