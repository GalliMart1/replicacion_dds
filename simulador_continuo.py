import socket
import json
import time
import random

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
target_ip = "127.0.0.1"
target_port = 7402

# Coordenadas iniciales (ej. navegando por la zona de Puerto Belgrano)
lat_actual = -38.88
lon_actual = -62.10

print("Iniciando radar naval continuo... (Presiona Ctrl+C para detener)")

try:
    secuencia = 1
    while True:
        # Simular el avance del buque (pequeñas variaciones aleatorias)
        lat_actual += random.uniform(-0.005, 0.005)
        lon_actual += random.uniform(-0.005, 0.005)

        data = {
            "type": "TRACK",
            "id": "TRK-001",
            "ambiente": "SUPERFICIE",
            "figura": "Fragata Meko",
            "estado": "SOSPECHOSO",
            "detalle": f"Actualización de radar #{secuencia}",
            "lat": round(lat_actual, 4),
            "lon": round(lon_actual, 4)
        }

        # Enviar el paquete UDP
        sock.sendto(json.dumps(data).encode('utf-8'), (target_ip, target_port))
        
        print(f"[{secuencia}] Traza enviada -> Lat: {data['lat']}, Lon: {data['lon']}")
        
        secuencia += 1
        time.sleep(2)  # Pausa de 2 segundos entre cada barrido del radar

except KeyboardInterrupt:
    print("\nSimulador de radar detenido por el usuario.")
finally:
    sock.close()
