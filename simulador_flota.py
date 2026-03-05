import socket
import json
import time
import random

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
target_ip = "127.0.0.1"
target_port = 7402

# Nuestra flota: id, ambiente, figura, estado, lat, lon, y cuánto se mueven por ciclo (dlat, dlon)
flota = [
    {"id": "TRK-001", "ambiente": "SUPERFICIE", "figura": "Fragata Meko", "estado": "HOSTIL", "lat": -38.88, "lon": -62.10, "dlat": 0.001, "dlon": 0.002},
    {"id": "TRK-002", "ambiente": "AIRE", "figura": "Caza F-16", "estado": "AMIGO", "lat": -38.50, "lon": -62.00, "dlat": -0.015, "dlon": 0.010},
    {"id": "TRK-003", "ambiente": "SUBMARINO", "figura": "Submarino TR-1700", "estado": "DESCONOCIDO", "lat": -39.10, "lon": -61.80, "dlat": 0.0005, "dlon": -0.001}
]

print("Iniciando radar de flota táctica... (Presiona Ctrl+C para detener)")

try:
    secuencia = 1
    while True:
        print(f"\n--- Barrido de Radar #{secuencia} ---")
        
        for unidad in flota:
            # Actualizamos la posición sumando su vector de movimiento más un poco de ruido aleatorio
            unidad["lat"] += unidad["dlat"] + random.uniform(-0.0002, 0.0002)
            unidad["lon"] += unidad["dlon"] + random.uniform(-0.0002, 0.0002)

            data = {
                "type": "TRACK",
                "id": unidad["id"],
                "ambiente": unidad["ambiente"],
                "figura": unidad["figura"],
                "estado": unidad["estado"],
                "detalle": f"Rumbo táctico actualizado",
                "lat": round(unidad["lat"], 4),
                "lon": round(unidad["lon"], 4)
            }

            # Enviar por UDP a nuestro nodo C++
            sock.sendto(json.dumps(data).encode('utf-8'), (target_ip, target_port))
            print(f"Detectado {unidad['id']} ({unidad['figura']}) -> Lat: {data['lat']}, Lon: {data['lon']}")
        
        secuencia += 1
        time.sleep(2)  # El radar da una vuelta cada 2 segundos

except KeyboardInterrupt:
    print("\nSimulador de flota detenido.")
finally:
    sock.close()
