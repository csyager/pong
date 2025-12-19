import socket
import struct
import argparse 

parser = argparse.ArgumentParser(
                    prog='UDPHelper',
                    description='Utility script for packaging and sending position as UDP buffer.')

parser.add_argument('-i', '--id', type=int, required=True)
parser.add_argument('-x', '--x-position', type=int, required=True)
parser.add_argument('-y', '--y-position', type=int, required=True)

args = parser.parse_args()

# The format '<Ic' means:
# '<' : Little-endian
# 'I' : Unsigned 4-byte integer (uint32_t)
# 'c' : 1-byte character
# Note: Python's struct.pack will handle the 8-byte alignment automatically.
data = struct.pack('<III', int(args.id), int(args.x_position), int(args.y_position))
print(f"sending data: {data}")
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(data, ("localhost", 9034)) # Replace with your server's port
