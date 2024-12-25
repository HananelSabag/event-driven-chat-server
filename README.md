# Event-Driven Chat Server

## Project Overview
This project demonstrates the implementation of an event-driven chat server built in C. The server is designed to handle real-time communication between multiple clients, broadcasting messages from one client to all others while excluding the sender.

### Key Features:
- **Concurrency**: Supports up to 30 simultaneous client connections.
- **Non-blocking Design**: Utilizes the `select` system call for efficient event-driven architecture.
- **Text Manipulation**: Automatically converts incoming messages to uppercase before broadcasting.
- **Graceful Shutdown**: Implements signal handling to ensure all resources are freed during termination.
- **Scalability and Efficiency**: Avoids the overhead of multi-threading by leveraging non-blocking IO.

The server is implemented in a Linux environment and demonstrates essential concepts in network programming, including socket communication, dynamic memory management, and asynchronous IO.

---

## Features
- **Connection Handling**: Manages client connections dynamically, adding and removing clients as needed.
- **Message Broadcasting**: Efficiently sends messages to all clients except the sender.
- **Resource Management**: Ensures robustness by handling socket states and dynamic memory allocation properly.
- **Real-Time Performance**: Delivers high responsiveness suitable for real-time applications.

---

## Compilation and Usage

### Compile
To compile the project, use the following command:
```bash
gcc -o chat_server chat_server.c

Run

To start the server, use:

./chat_server <port_number>

Replace <port_number> with the desired port (e.g., 12345).

Requirements
	•	GCC compiler
	•	Linux environment

Example

Here is an example of the server in action:
	1.	Start the server:

./chat_server 12345


	2.	Connect clients using tools like telnet:

telnet 127.0.0.1 12345


	3.	Send messages from one client to see them broadcast to others.

Improvements

Future enhancements may include:
	•	Adding support for private messaging.
	•	Implementing client authentication.
	•	Introducing message encryption for secure communication.
