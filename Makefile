all: server client server_select

clean:
	rm -f client server server_select *.txt

server_select: server_select.c
	gcc server_select.c -o server_select

server: server.c
	gcc server.c -o server

client: client.c
	gcc client.c -o client