#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "proxy.h"

// comment this to hide extra debugging info
#define DEBUG

// list of black-listed websites
// each url in which any of these words occure, will be blocked
char *url_blacklist[] = {"facebook.com", "hulu.com", "netflix.com"};
int url_blacklist_len = 3;

// list of black-listed words for content filtering
// any content in which any of these words occure, will be blocked
char *word_blacklist[] = {"Arse", "fixer", "algorithms"};
int word_blacklist_len = 3;

int main(int argc, char **argv)
{
	pid_t chpid;
	struct sockaddr_in addr_in, cli_addr, serv_addr;
	struct hostent *hostent;
	int sockfd, newsockfd;
	int clilen = sizeof(cli_addr);
	struct stat st = {0};
	
	// the first argument shows the port-no to listen on
	if(argc != 2)
	{
		printf("Using:\n\t%s <port>\n", argv[0]);
		return -1;
	}
  
	printf("starting...\n");
	
	// checking if the cache directory exists
	if (stat("./cache/", &st) == -1) {
		mkdir("./cache/", 0700);
	}
   
	bzero((char*)&serv_addr, sizeof(serv_addr));
	bzero((char*)&cli_addr, sizeof(cli_addr));
	   
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[1]));
	serv_addr.sin_addr.s_addr = INADDR_ANY;
   
	// creating the listening socket for our proxy server
	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sockfd < 0)
	{
		perror("failed to initialize socket");
	}
   
	// binding our socket to the given port
	if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("failed to bind socket");
	}

	// start listening - w/ backlog = 50
	listen(sockfd, 50);
	
accepting:
	newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
	
	if((chpid = fork()) == 0)
	{
		struct sockaddr_in host_addr;
		int i, n;			// loop indices
		int rsockfd;		// remote socket file descriptor
		int cfd;			// cache-file file descriptor
		
		int port = 80;		// default http port - can be overridden	
		char type[256];		// type of request - e.g. GET/POST/HEAD
		char url[4096];		// url in request - e.g. facebook.com
		char proto[256];	// protocol ver in request - e.g. HTTP/1.0
		
		char datetime[256];	// the date-time when we last cached a url
		
		// break-down of a given url by parse_url function
		// e.g. http://www.yahoo.com/site/index.html
		//		url_host: www.yahoo.com
		//		url_path: /site/index.html
		char url_host[256], url_path[256];
		
		char url_encoded[4096];	// encoded url, used for cahce filenames
		char filepath[256]; 	// used for cache file paths
		
		char *dateptr;		// used to find the date-time in http response
		char buffer[4096];	// buffer used for send/receive
		int response_code;	// http response code - e.g. 200, 304, 301
		
		bzero((char*)buffer, 4096);			// let's play it safe!
		
		// recieving the http request
		recv(newsockfd, buffer, 4096, 0);
		
		// we only care about the first line in request
		// e.g. GET /facebook.com/ HTTP/1.0
		//		type:	GET
		//		url:	/facebook.com
		//		proto:	HTTP/1.0
		sscanf(buffer, "%s %s %s", type, url, proto);
		
		// adjusting the url -- some cleanup!
		if(url[0] == '/')
		{
			strcpy(buffer, &url[1]);
			strcpy(url, buffer);
		}
		
#ifdef DEBUG
		printf("-> %s %s %s\n", type, url, proto);
#endif
		
		// make sure the request is a valid request and we can process it!
		// we only accept GET requests
		// also some browsers send non-http requests -- this filters them out!
		if((strncmp(type , "GET", 3) != 0) || ((strncmp(proto, "HTTP/1.1", 8) != 0) && (strncmp(proto, "HTTP/1.0", 8) != 0)))
		{
#ifdef DEBUG
			printf("\t-> bad request format - we only accept HTTP GETs\n");
#endif
			// invalid request -- send the following line back to browser
			sprintf(buffer,"400 : BAD REQUEST\nONLY GET REQUESTS ARE ALLOWED");
			send(newsockfd, buffer, strlen(buffer), 0);
			
			// sometimes goto makes the code more readable!
			goto end;
		}
		
		// OK! now we are sure that we have a valid GET request
		
		// let's break down the url to know the host and path
		// e.g. url: www.yahoo.com:8080/site/index.php?k=uiu82&h=89y%56
		//		url_host:	www.yahoo.com
		//		port:		8080
		//		url_path:	/site/index.php?k=uiu82&h=89y%56
		parse_url(url, url_host, &port, url_path);
		
		// encoding the url for later use
		// e.g.
		//	http://meyerweb.com/eric/tools/dencoder/
		//	http%3A%2F%2Fmeyerweb.com%2Feric%2Ftools%2Fdencoder%2F
		url_encode(url, url_encoded);
		
#ifdef DEBUG
		printf("\t-> url_host: %s\n", url_host);
		printf("\t-> port: %d\n", port);
		printf("\t-> url_path: %s\n", url_path);
		printf("\t-> url_encoded: %s\n", url_encoded);
#endif

		// BLACK LIST CHECK
		// check if the given url is black-listed or not
		// for each entry in black-list
		for(i = 0; i < url_blacklist_len; i++)
		{
			// if url contains the black-listed word
			if(NULL != strstr(url, url_blacklist[i]))
			{
#ifdef DEBUG
				printf("\t-> url in blacklist: %s\n", url_blacklist[i]);
#endif
				// sorry! -- tell the browser that this url is forbidden
				sprintf(buffer,"400 : BAD REQUEST\nURL FOUND IN BLACKLIST\n%s", url_blacklist[i]);
				send(newsockfd, buffer, strlen(buffer), 0);
				
				// again, goto for clarity!
				goto end;
			}
		}
		
		// So, we know that the url is premissible, what else?
		// we need to find the ip for the host
		// ex. www.google.com : 173.194.43.46		
		if((hostent = gethostbyname(url_host)) == NULL)
		{
			fprintf(stderr, "failed to resolve %s: %s\n", url_host, strerror(errno));
			goto end;
		}
		
		bzero((char*)&host_addr, sizeof(host_addr));
		host_addr.sin_port = htons(port);
		host_addr.sin_family = AF_INET;
		bcopy((char*)hostent->h_addr, (char*)&host_addr.sin_addr.s_addr, hostent->h_length);

		// create a socket to connect to the remote host
		rsockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		
		if(rsockfd < 0)
		{
			perror("failed to create remote socket");
			goto end;
		}
				
		// try connecting to the remote host
		if(connect(rsockfd, (struct sockaddr*)&host_addr, sizeof(struct sockaddr)) < 0)
		{
			perror("failed to connect to remote server");
			goto end;
		}

#ifdef DEBUG		
		printf("\t-> connected to host: %s w/ ip: %s\n", url_host, inet_ntoa(host_addr.sin_addr));
#endif
				
		// CACHING CHECK
		// have we already cached this url? let's see...
		// if we have already seen it, we must have a cache-file for it
		// ex. if we have already seen http://meyerweb.com/eric/tools/dencoder/
		// we have a file by name http%3A%2F%2Fmeyerweb.com%2Feric%2Ftools%2Fdencoder%2F
		sprintf(filepath, "./cache/%s", url_encoded);
		if (0 != access(filepath, 0)) {
			// we don't have any file by this name
			// meaning: we should request it from the remote host
			sprintf(buffer,"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", url_path, url_host);
#ifdef DEBUG		
			printf("\t-> first access...\n");
#endif
			// jump into request
			goto request; 
		}
		
		// if we are here, it means that we have it already cached
		// but it may be stale! let's see when was it cached
		// using the "Date:" entry in http response
		
		// open the file
		sprintf(filepath, "./cache/%s", url_encoded);
		cfd = open (filepath, O_RDWR);
		bzero((char*)buffer, 4096);
		// reading the first chunk is enough
		read(cfd, buffer, 4096);
		close(cfd);
		
		// find the first occurunce of "Date:" in response -- NULL if none.
		// ex. Date: Fri, 18 Apr 2014 02:57:20 GMT
		dateptr = strstr(buffer, "Date:");
		if(NULL != dateptr)
		{
			// response has a Date field, like Date: Fri, 18 Apr 2014 02:57:20 GMT
			
			bzero((char*)datetime, 256);
			// skip 6 characters, namely "Date: "
			// and copy 29 characters, like "Fri, 18 Apr 2014 02:57:20 GMT"
			strncpy(datetime, &dateptr[6], 29);
			
			// send CONDITIONAL GET
			// If-Modified-Since the date that we cached it
			sprintf(buffer,"GET %s HTTP/1.0\r\nHost: %s\r\nIf-Modified-Since: %s\r\nConnection: close\r\n\r\n", url_path, url_host, datetime);
#ifdef DEBUG		
			printf("\t-> conditional GET...\n");
			printf("\t-> If-Modified-Since: %s\n", datetime);
#endif
		} else {
			// generally all http responses have Date filed, but just in case!
			sprintf(buffer,"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", url_path, url_host);
#ifdef DEBUG		
			printf("\t-> the response had no date field\n");
#endif
		}

request:
		// send the request to remote host
		n = send(rsockfd, buffer, strlen(buffer), 0);
		
		if(n < 0)
		{
			perror("failed to write to remote socket");
			goto end;
		}

do_cache:
		// now we have sent the request, we need to get the response and
		// cache it for later uses
		
		/* 
		 * CASE1: we had no previous cache
		 * CASE2: we had previous cache, but it did not have a Date field
		 * CASE3: we had a previous cache, and we have sent a conditional GET
		 * 		sub-CASE31: our cached copy is stale
		 * 		sub-CASE32: our cached copy is up-to-date
		 */
		cfd = -1;
		
		// since the response can be huge, we might need to iterate to
		// read all of it
		do
		{
			bzero((char*)buffer, 4096);
			
			// recieve from remote host
			n = recv(rsockfd, buffer, 4096, 0);
			// if we have read anything - otherwise END-OF-FILE
			if(n > 0)
			{
				// if this is the first time we are here
				// meaning: we are reading the http response header
				if(cfd == -1)
				{
					float ver;
					// read the first line to discover the response code
					// ex. HTTP/1.0 200 OK
					// ex. HTTP/1.0 304 Not Modified
					// we only care about these two!
					sscanf(buffer, "HTTP/%f %d", &ver, &response_code);
					
#ifdef DEBUG		
					printf("\t-> response_code: %d\n", response_code);
#endif
					// if it is not 304 -- anything other than sub-CASE32
					if(response_code != 304)
					{
						// create the cache-file to save the content
						sprintf(filepath, "./cache/%s", url_encoded);
						if((cfd = open(filepath, O_RDWR|O_TRUNC|O_CREAT, S_IRWXU)) < 0)
						{
							perror("failed to create cache file");
							goto end;
						}
#ifdef DEBUG		
						printf("\t-> from remote host...\n");
#endif
					} else {
						// if it is 304 -- sub-CASE32
						// we don't need to read the content
						// our content is already up-to-date
#ifdef DEBUG
						printf("\t-> not modified\n");
						printf("\t-> from local cache...\n");
#endif
						// send the response to the browser from local cache
						goto from_cache;
					}
				}
				
				// if we are here, it means we need to cache the response
				// and save it in a files
				
				// FILTER CONTENTS
				// as we recieve the response, check if it has any black-listed
				// word or not
				// for each swear word
				for(i = 0; i < word_blacklist_len; i++)
				{
					// if the swear word occurs in the content
					if(NULL != strstr(buffer, word_blacklist[i]))
					{
#ifdef DEBUG
						printf("\t-> content in blacklist: %s\n", word_blacklist[i]);
#endif
						
						close(cfd);
						
						// remove the cache file
						sprintf(filepath, "./cache/%s", url_encoded);
						remove(filepath);
						
						// tell the browser why we are not providing the content
						sprintf(buffer,"400 : BAD REQUEST\nCONTENT FOUND IN BLACKLIST\n%s", word_blacklist[i]);
						send(newsockfd, buffer, strlen(buffer), 0);		// send to browser
						goto end;
					}
				}
				
				// we are golden! no profanity!
				
				// write to file
				write(cfd, buffer, n);
			}
		} while(n > 0);
		close(cfd);
		
		// up to here we only cached the response, now we need to send it
		// back to the requesting browser

from_cache:
		
		// read from cache file
		sprintf(filepath, "./cache/%s", url_encoded);
		if((cfd = open (filepath, O_RDONLY)) < 0)
		{
			perror("failed to open cache file");
			goto end;
		}
		do
		{
			bzero((char*)buffer, 4096);
			n = read(cfd, buffer, 4096);
			if(n > 0)
			{
				// send it to the browser
				send(newsockfd, buffer, n, 0);
			}
		} while(n > 0);
		close(cfd);

end:
#ifdef DEBUG
		printf("\t-> exiting...\n");
#endif
		// closing sockets!
		close(rsockfd);
		close(newsockfd);
		close(sockfd);
		return 0;
	} else {
		// closing socket
		close(newsockfd);
		
		// loop...
		goto accepting;
	}
	
	close(sockfd);
	return 0;
	// :D -- You'll never get here! :(
}
