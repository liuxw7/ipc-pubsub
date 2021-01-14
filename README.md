# ipc-pubsub
Simplified IPC over POSIX shared memory, with a publisher/subscriber interface.

# Design Notes

## Thoughts
- Unix / Posix queues need tuning to have a reasonable amount of file descriptors available. I would prefer not to require that.
- UDP requires proper networking setup which doesn't seem desirable
- Semaphores don't allow any data through which means all actual payloads must
  be sent through complex shared memory structures (even metadata about what
  nodes are available)
- Pipes have somewhat complex state that can result in spurious blocking of there
  is too much data in the pipes

## Design
- A single shared memory object is kept with an index and in-flight messages

### Adding a Node
- write-lock the metadata file
- read the current metadata
- append node information to data
- write metadata
- unlock the metadata file

### Sending a Message
- (optional) retrieve and use a shm buffer ::Allocate() -> ShmBuffer();
- Send(ShmBuffer) or Send(data, len)
- write-lock the metadata file
- read the current metadata
- append to the appropriate in-flight queues
- unlock the metadata file
- sem_up() on all the readers

### Reading a Message
- While true, wait for sem_down()
- write-lock the metadata file
- update current metadata
- pop all the in-flight shm objects
- unlock the metadata file
- pass off shm objects to handler thread
