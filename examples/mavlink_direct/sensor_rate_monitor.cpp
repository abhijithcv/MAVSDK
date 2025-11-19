//
// Example to monitor the update rate of optical flow and distance sensor
// MAVLink messages
//

#include <mavsdk/mavsdk.h>
#include <mavsdk/plugins/mavlink_direct/mavlink_direct.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <thread>
#include <map>
#include <mutex>

using namespace mavsdk;

void usage(const std::string& bin_name)
{
    std::cerr << "Usage : " << bin_name << " <connection_url>\n"
              << "Connection URL format should be :\n"
              << " For TCP server: tcpin://<our_ip>:<port>\n"
              << " For TCP client: tcpout://<remote_ip>:<port>\n"
              << " For UDP server: udpin://<our_ip>:<port>\n"
              << " For UDP client: udpout://<remote_ip>:<port>\n"
              << " For Serial : serial://</path/to/serial/dev>:<baudrate>]\n"
              << "For example, to connect to a serial device: serial:///dev/ttyUSB0:57600\n";
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    // Initialize MAVSDK with GroundStation component type
    mavsdk::Mavsdk mavsdk{mavsdk::Mavsdk::Configuration{mavsdk::ComponentType::GroundStation}};

    // Add connection
    mavsdk::ConnectionResult connection_result = mavsdk.add_any_connection(argv[1]);
    if (connection_result != mavsdk::ConnectionResult::Success) {
        std::cerr << "Connection failed: " << connection_result << std::endl;
        return 1;
    }

    // Wait for the system to connect
    std::cout << "Waiting for system to connect..." << std::endl;
    auto start_wait = std::chrono::steady_clock::now();
    while (mavsdk.systems().size() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_wait).count();
        if (elapsed > 10) {
            std::cout << "Note: No autopilot system detected after 10 seconds.\n";
            std::cout << "Continuing to listen for MAVLink messages anyway...\n";
            break;
        }
    }

    // Get the first system (if available)
    std::shared_ptr<System> system;
    if (mavsdk.systems().size() > 0) {
        system = mavsdk.systems().at(0);
        std::cout << "System connected!" << std::endl;
    } else {
        // Create a system manually to listen for messages even without a heartbeat
        std::cout << "Listening for MAVLink messages..." << std::endl;
        // Wait a bit more to see if any systems appear
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (mavsdk.systems().size() > 0) {
            system = mavsdk.systems().at(0);
        } else {
            std::cout << "Warning: No system detected. MAVLink messages may not be received.\n";
            std::cout << "Make sure the device is sending MAVLink messages.\n";
            return 1;
        }
    }

    // Instantiate the plugin
    auto mavlink_direct = mavsdk::MavlinkDirect{system};

    // Message statistics tracking
    std::map<std::string, unsigned> message_counts;
    std::map<std::string, std::chrono::steady_clock::time_point> last_message_time;
    std::mutex stats_mutex;
    auto start_time = std::chrono::steady_clock::now();

    // Messages we're interested in monitoring
    std::vector<std::string> monitored_messages = {
        "OPTICAL_FLOW",
        "OPTICAL_FLOW_RAD",
        "DISTANCE_SENSOR",
        "HEARTBEAT"
    };

    // Subscribe to all messages and filter for the ones we want
    auto handle = mavlink_direct.subscribe_message(
        "", [&](const mavsdk::MavlinkDirect::MavlinkMessage& message) {
            std::lock_guard<std::mutex> lock(stats_mutex);

            // Check if this is one of the messages we're monitoring
            bool is_monitored = false;
            for (const auto& msg_name : monitored_messages) {
                if (message.message_name == msg_name) {
                    is_monitored = true;
                    break;
                }
            }

            if (is_monitored) {
                message_counts[message.message_name]++;
                last_message_time[message.message_name] = std::chrono::steady_clock::now();
            }
        });

    std::cout << "\nMonitoring sensor messages. Press Ctrl+C to exit...\n" << std::endl;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::lock_guard<std::mutex> lock(stats_mutex);

        auto current_time = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();

        // Clear screen for better readability
        std::cout << "\033[2J\033[H";

        std::cout << "┌────────────────────────────────────────────────────────────────┐\n";
        std::cout << "│ Sensor Message Rate Monitor                                    │\n";
        std::cout << "│ Runtime: " << std::setw(3) << elapsed
                  << " seconds                                           │\n";
        std::cout << "├────────────────────────────┬───────┬───────────┬──────────────┤\n";
        std::cout << "│ Message Name               │ Total │ Rate (Hz) │ Last Seen    │\n";
        std::cout << "├────────────────────────────┼───────┼───────────┼──────────────┤\n";

        for (const auto& msg_name : monitored_messages) {
            unsigned count = message_counts[msg_name];
            double messages_per_second = (elapsed > 0) ? static_cast<double>(count) / elapsed : 0.0;

            // Calculate time since last message
            std::string last_seen = "Never";
            if (last_message_time.find(msg_name) != last_message_time.end()) {
                auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                    current_time - last_message_time[msg_name]).count();

                if (time_since_last < 1000) {
                    last_seen = std::to_string(time_since_last) + " ms ago";
                } else {
                    last_seen = std::to_string(time_since_last / 1000) + " s ago";
                }
            }

            std::cout << "│ " << std::left << std::setw(26) << msg_name << " │ "
                      << std::right << std::setw(5) << count << " │ "
                      << std::right << std::setw(9) << std::fixed << std::setprecision(2)
                      << messages_per_second << " │ "
                      << std::left << std::setw(12) << last_seen << " │\n";
        }

        std::cout << "└────────────────────────────┴───────┴───────────┴──────────────┘\n";

        // Show a status message if no messages have been received
        if (message_counts.empty()) {
            std::cout << "\n⚠ No monitored messages received yet.\n";
            std::cout << "  Waiting for: OPTICAL_FLOW, OPTICAL_FLOW_RAD, DISTANCE_SENSOR, HEARTBEAT\n";
        }

        std::cout << std::flush;
    }

    // Unsubscribe from all messages
    mavlink_direct.unsubscribe_message(handle);
    std::cout << "Unsubscribed from MAVLink messages, exiting." << std::endl;

    return 0;
}
