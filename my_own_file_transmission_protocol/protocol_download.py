import socket

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(("127.0.0.1", 12345))

name = b"protocol.cpp"
data = b"\2"
data += len(name).to_bytes(2, byteorder='little')
data += name
client.sendall(data)
response = client.recv(1024)

print(response)

client.close()