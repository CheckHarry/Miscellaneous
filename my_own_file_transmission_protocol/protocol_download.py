import socket

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(("127.0.0.1", 12345))

name = b"./protocol.py"
data = b"\2"
data += len(name).to_bytes(1, byteorder='little')
data += name
client.sendall(data)
response = client.recv(1024)
print(int.from_bytes(response[1:1 + 8], byteorder='little'))
print(response)

client.close()