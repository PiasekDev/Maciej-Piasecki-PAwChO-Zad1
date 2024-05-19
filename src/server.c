#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 80
#define AUTHOR "Maciej Krzysztof Piasecki (97701)"

#define TIME_SERVICE_ADDRESS "34.118.113.224"
#define TIME_SERVICE_PORT 81

#define ERROR -1

/**
 * Create a new socket object, that uses IPv4 (AF_INET) and the TCP protocol.
 * @return The file descriptor of the created socket
 */
int create_socket() {
	int socket_file_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_file_desc == ERROR) {
		perror("socket");  // Print info about error stored in errno
		exit(EXIT_FAILURE);
	}
	return socket_file_desc;
}

void print_current_utc_time() {
	time_t current_time;
	struct tm *time_info;
	time(&current_time);								 		// store the current time in current_time
	time_info = localtime(&current_time);				 	// convert current_time to the local time and store its data in time_info
	printf("Current UTC time: %s", asctime(time_info));	// print the time in the format "Day Month Date Hours:Minutes:Seconds Year\n"
}

void print_author() {
	printf("Author: %s\n", AUTHOR);
}

/**
 * Create a new socket address object, that uses the IPv4 protocol.
 * @param ip_address the internet address
 * @param port the port number
 * @return a pointer to the created socket address object
 */
struct sockaddr *create_socket_address(in_addr_t ip_address, int port) {
	struct sockaddr_in *internet_socket_address = malloc(sizeof(struct sockaddr_in));  // Allocate memory for the socket address
	internet_socket_address->sin_family = AF_INET;											// Set the address family to IPv4
	internet_socket_address->sin_port = htons(port);								 // Set the port number
	internet_socket_address->sin_addr.s_addr = ip_address;									// Set the internet address
	return (struct sockaddr *)internet_socket_address;										// Cast the socket address to a generic sockaddr pointer and return it
}

/**
 * Run a healthcheck on the server by connecting to it.
 * If the connection is successful, print "Server is up" and exit with success status.
 * If the connection fails, print an error message and exit with failure status.
 */
void run_healthcheck() {
	int socket = create_socket();
	struct sockaddr *target_server_address = create_socket_address(inet_addr("127.0.0.1"), PORT);

	int connect_status = connect(socket, target_server_address, sizeof(*target_server_address));
	if (connect_status == ERROR) {
		perror("connect");
		exit(EXIT_FAILURE);
	} else {
		printf("Server is up\n");
		exit(EXIT_SUCCESS);
	}
}

void extract_time_response(char *time_response, char *buffer) {
	char *body_start = strstr(time_response, "\r\n\r\n");			 // Find the start of the body (according to the HTTP protocol)
	if (body_start) {																// If the body was found
		body_start += 4;															// Skip past the "\r\n\r\n"
		char *body_end = strchr(body_start, '\0');							 // Find the end of the response body
		if (body_end) {																// If the end of the body was found
			memmove(buffer, body_start, body_end - body_start + 1);	  // Copy the body to the buffer
		}
	}
}

int main(int argc, char *argv[]) {
	setbuf(stdout, NULL);  // Disable buffering for stdout - without this, the output would be fully-buffered and logs would be printed only after the buffer is full

	// Run a healthcheck if the program was started with the "healthcheck" argument instead of starting the server
	if (argc == 2 && strcmp(argv[1], "healthcheck") == 0) {
		run_healthcheck();
	}

	int server_socket = create_socket();
#ifdef DEBUG
	printf("Server socket created successfully\n");
#endif

	// Bind the server socket to the specified port, the given INADDR_ANY address means that the server will accept connections on any of the host's IP addresses
	struct sockaddr *server_address = create_socket_address(INADDR_ANY, PORT);
	int bind_status = bind(server_socket, server_address, sizeof(*server_address));
	if (bind_status == ERROR) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
#ifdef DEBUG
	printf("Server socket bound successfully\n");
#endif

	print_current_utc_time();
	print_author();

	// Start listening for incoming connections, the second argument is the maximum number of pending connections in the listen queue (because this is supposed to be a simple server its set to 1)
	int listen_status = listen(server_socket, 1);
	if (listen_status == ERROR) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	printf("Server listening on TCP port: %d\n", PORT);

	// The server will run indefinitely, accepting connections and sending responses to clients
	while (1) {
		struct sockaddr_in client_address; // Allocate memory for the client address
		socklen_t client_address_size = sizeof(client_address); // Get the size of the client address

		// Accept a connection from a client, storing the client's address in client_address
		int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_size);
		if (client_socket == ERROR) {
			perror("accept");
			exit(EXIT_FAILURE);
		}
#ifdef DEBUG
		printf("Client connected\n");
#endif

		// Get client's ip address
		char ip_address[INET_ADDRSTRLEN]; // Allocate memory for the client's IP address
		inet_ntop(AF_INET, &client_address.sin_addr, ip_address, sizeof(ip_address)); // Convert the client's IP address to a human-readable string
#ifdef DEBUG
		printf("Client IP address: %s\n", ip_address);
#endif

		// Prepare the request to get the clients local time
		char request_data[256]; // Allocate memory for the request data
		sprintf(request_data, "POST / HTTP/1.0\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", strlen(ip_address), ip_address); // Store the formatted string in request_data

		// Prepare the connection to the time service server
		int connection_socket = create_socket(); // Create a new socket for the connection
		struct sockaddr *target_address = create_socket_address(inet_addr(TIME_SERVICE_ADDRESS), TIME_SERVICE_PORT); // Create a socket address for the target time service server

		// Connect to the time service server
		int connection_status = connect(connection_socket, target_address, sizeof(*target_address));
		if (connection_status == ERROR) {
			perror("connect");
			exit(EXIT_FAILURE);
		}

		// Send request data to the server (write to the socket)
		if (write(connection_socket, request_data, strlen(request_data)) == ERROR) {
			perror("write");
			exit(EXIT_FAILURE); // Exit the program if write fails
		}

		// Read the response from the server
		// The service will respond with the time correctly formatted, however if it cannot determine the timezone from the given IP address it will respond with time in Europe/Warsaw timezone
		char time_response[256]; // Allocate memory for the response
		int read_status = read(connection_socket, time_response, sizeof(time_response) - 1); // Read the response into time_response
		if (read_status == ERROR) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		time_response[read_status] = '\0';	// Null-terminate the response (read_status stores the number of bytes read, so the byte after that is where we should put the null terminator)

		// Close the connection socket
		close(connection_socket);

		// Extract the time from the response
		char time[128]; // Allocate memory for the time
		extract_time_response(time_response, time); // Extract the time from the response into time buffer
#ifdef DEBUG
		printf("Time received from server: %s\n", time);
#endif

		// Send the response to the original client
		char response[256]; // Allocate memory for the response
		sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nAdres ip klienta: %s\r\nCzas: %s", ip_address, time); // Store the formatted response in response buffer
		int write_status = write(client_socket, response, strlen(response)); // Write the response to the client socket
		if (write_status == ERROR) {
			perror("write");
			exit(EXIT_FAILURE);
		}
#ifdef DEBUG
		printf("Message sent to client\n");
#endif
		close(client_socket); // Close the client socket (end the connection)
	}
	close(server_socket);
	return EXIT_SUCCESS;
}
