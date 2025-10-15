#pragma once

// The maximum forward rules allowed.
#define MAX_FWD_RULES 256

// The maximum bind IPs used.
// This is used to determine the size of the port map.
// If you plan on binding multiple IP addresses, set this accordingly.
#define MAX_BIND_IPS 1

// The port range to use when selecting an available source port.
// MAX_PORT - (MIN_PORT - 1) = The maximum amount of concurrent connections.
#define MIN_PORT 52000
#define MAX_PORT 52500

// Enables forward rule logging.
#define ENABLE_RULE_LOGGING

// Counts packets sent back to the client towards "forwarded" stat counter. 
#define STATS_COUNT_FWD_BACK

// If enabled, performs a FIB lookup on the route table when forwarding packets.
// Otherwise, the ethernet source and destination MAC addresses are swapped.
#define ENABLE_FIB_LOOKUPS

// Maximum interfaces the firewall can attach to.
#define MAX_INTERFACES 6

// Whether to recycle connections by last seen time.
// Otherwise, connections are recycled by least amount of packets per nanosecond.
#define RECYCLE_LAST_SEEN

// Adds packet and last seen counters to connections.
// This isn't used anywhere in the program right now which is why it's disabled by default.
//#define CONNECTION_COUNTERS

// If enabled, uses a newer bpf_loop() function when choosing a source port for a new connection.
// This allows for a much higher source port range. However, it requires a more recent kernel.
#define USE_NEW_LOOP