# ipc-pubsub
Simplified IPC over POSIX shared memory, with a publisher/subscriber interface.

# Design Notes

## Topology
- Topology is hub-and-spoke with randomly chosen hub.
- The first Node to create the hub wins

### Hub
- Hub maintains a list of topology events that it sends to each new client.
- Hub is responsible for minting new Topology Events when a Client sends updates
  about themselves
- New hubs may be missing history (for instance if a just-started client wins
  the race to create a hub) or create history that conflicts with the spokes (
  for instance if a new client with no history attachs and registers itself
  befure the rest of the clients send the full history).
- Upon connection clients will send their full version of history which the hub
  will integrate into a complete history. The hub is the only source of truth on
  history so if it sends updates that contradict clients, the clients must accept
  the new history and trigger events for them. This should be rare though
- The hub 'mints' new history events by giving them a sequence (seq) and unique
  ID (uid) number. It then sends these events to all clients.

### Spoke / Client
- The purpose of the Topology Client is to call callbacks for topology update
  events. In general duplicate callbacks for events should be avoided, however
  during Hub transfer (the hub dies and is restarted) there may occasionaly be
  a duplicate.
- New clients will have no history and be sent the complete history upon
  connecting to the hub.
- Its possible through various fail conditions that a client could have conflicting
  history with the hub. In this case all conflicts are removed and any events
  sent by the Hub should trigger additional Topology Events
- During Hub transfer the Client will reconnect to the new hub and send 1. the
  entire history 2. and information about itself

## Point-To-Point Messages
- Messages consist of a shared memory segment, the associated file descriptor is
  then sent over Unix Domain Socket to all subscribers, then the segment (and fd)
  is closed at the sender
