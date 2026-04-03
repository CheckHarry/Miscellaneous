import socket

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(("127.0.0.1", 12345))
client.sendall(b"\1")
response = client.recv(1024)

num = 0
fcnt = int.from_bytes(response[num:num + 2], byteorder='little')
num += 2
for i in range(fcnt):
    print(int.from_bytes(response[num:num + 8], byteorder='little'))
    num += 8
    filenamesize = int.from_bytes(response[num:num + 1], byteorder='little')
    print(filenamesize)
    num += 1
    print(response[num:num + filenamesize])
    num += filenamesize
    

client.close()