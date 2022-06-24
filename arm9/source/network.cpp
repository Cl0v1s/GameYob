#include <nds.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "console.h"
#include "network.h"

int sock;
struct sockaddr_in selfAddress = { 0 };
struct sockaddr_in pairAddress = { 0 };


void init() {

    // Let's send a simple HTTP request to a server and print the results!

    // store the HTTP request for later
    const char * request_text = 
        "GET /dswifi/example1.php HTTP/1.1\r\n"
        "Host: www.akkit.org\r\n"
        "User-Agent: Nintendo DS\r\n\r\n";

    // Find the IP address of the server, with gethostbyname
    struct hostent * myhost = gethostbyname("www.akkit.org");
    iprintf("Found IP Address!\n");
 
    // Create a TCP socket
    int my_socket;
    my_socket = socket( AF_INET, SOCK_STREAM, 0 );
    iprintf("Created Socket!\n");

    // Tell the socket to connect to the IP address we found, on port 80 (HTTP)
    struct sockaddr_in sain;
    sain.sin_family = AF_INET;
    sain.sin_port = htons(80);
    sain.sin_addr.s_addr= *( (unsigned long *)(myhost->h_addr_list[0]) );
    connect( my_socket,(struct sockaddr *)&sain, sizeof(sain) );
    iprintf("Connected to server!\n");

    // send our request
    send( my_socket, request_text, strlen(request_text), 0 );
    iprintf("Sent our request!\n");

    // Print incoming data
    iprintf("Printing incoming data:\n");

    int recvd_len;
    char incoming_buffer[256];

    while( ( recvd_len = recv( my_socket, incoming_buffer, 255, 0 ) ) != 0 ) { // if recv returns 0, the socket has been closed.
        if(recvd_len>0) { // data was received!
            incoming_buffer[recvd_len] = 0; // null-terminate
            iprintf(incoming_buffer);
		}
	}

	iprintf("Other side closed connection!");

	shutdown(my_socket,0); // good practice to shutdown the socket.

	closesocket(my_socket); // remove the socket.
    /*
  if(!Wifi_InitDefault(WFC_CONNECT)) {
		printMenuMessage("Failed to connect!");
        return;
    }

    struct in_addr ip, gateway, mask, dns1, dns2;
    ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
    printMenuMessage(inet_ntoa(ip));

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    /*selfAddress.sin_family = AF_INET;
    selfAddress.sin_addr.s_addr = INADDR_ANY;
    selfAddress.sin_port = htons(8080); 

    pairAddress.sin_family = AF_INET;
    pairAddress.sin_addr.s_addr = inet_addr("192.168.1.36");
    pairAddress.sin_port = htons(8080);   

    // char nonblock = 1;
    // ioctl(sock, FIONBIO, &nonblock);

    //if(bind(sock,(sockaddr*)&selfAddress, sizeof(selfAddress)) >= 0) printMenuMessage(inet_ntoa(ip));
    //else printMenuMessage("Unable to init networking\n");

    */

}

void send(BGBPacket packet) {
    return;
    printLog("Sending %d / %d\n", packet.b1, packet.b2);
    sendto(sock, &packet, sizeof(BGBPacket), 0, (sockaddr*)&pairAddress, sizeof(pairAddress));
    printLog("%s\n", inet_ntoa(pairAddress.sin_addr));
}


BGBPacket receive() {
    BGBPacket packet = { 0, 0, 0, 0, 0};
    int size = sizeof(pairAddress);
    //int received = recvfrom(sock, &packet, sizeof(BGBPacket), ~MSG_PEEK, (sockaddr*)&pairAddress, &size);
    //if(received == -1) return packet;
    //printLog("Receiving %d / %d %d\n", packet.b1, packet.b2, received);
    return packet;
}


void stop() {
    closesocket(sock);
}