import socket
import json

data = {
    "type": "TRACK",
    "id": "TRK-001",
    "ambiente": "SUPERFICIE",
    "figura": "Fragata",
    "estado": "DESCONOCIDO",
    "detalle": "Detectado por radar de proa",
    "lat": -38.9,
    "lon": -62.1
}

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(json.dumps(data).encode('utf-8'), ("127.0.0.1", 7402))
print("Traza táctica enviada al sistema vía UDP.")
