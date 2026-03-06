# Sistema Táctico Descentralizado (C2) con OpenDDS y SQLite

Este proyecto implementa una red descentralizada de Comando y Control (C2) para la fusión y replicación de datos tácticos en tiempo real. Utiliza **OpenDDS** para la comunicación P2P entre nodos, **SQLite** para la persistencia de datos local y **sockets UDP** para la ingesta sensorial simulada.

## 🏗️ Arquitectura y Funcionamiento

El sistema opera bajo un modelo **Multi-Master** descentralizado, donde cualquier nodo puede recibir información del exterior y replicarla al resto de la red. Se compone de tres capas principales:

1. **Capa de Ingesta (UDP - Puerto 7402):** Un hilo independiente en cada nodo escucha continuamente el puerto UDP 7402. Los sensores externos (radares, sonares) envían "Trazas" (Tracks) o "Situaciones" (Sitreps) en formato JSON.
2. **Capa de Persistencia Local (SQLite):** Al recibir un JSON válido, el nodo parsea los datos y utiliza un comando `INSERT OR REPLACE` en su propia base de datos local (`<Node_ID>.db`). Esto asegura que si un ID ya existe (ej. "TRK-001"), se actualicen sus coordenadas sin crear duplicados.
3. **Capa de Replicación (OpenDDS):** Inmediatamente después de guardar el dato localmente, el nodo empaqueta la información en un struct IDL (`DBChange`) y lo publica en el tópico "DBChanges" mediante OpenDDS utilizando el estándar **RTPS puro**. Los demás nodos de la red, al estar suscritos, reciben este cambio y lo aplican silenciosamente en sus respectivas bases de datos locales.
4. **Elección Dinámica de Primary (Heartbeats):**
   Todos los nodos publican su "tiempo de vida" (uptime) cada 5 segundos en el tópico "NodeHeartbeat". Si un nodo detecta que tiene el mayor uptime de la red activa (purgando nodos caídos tras 15 segundos), asume automáticamente el rol de **Primary** (Maestro).

## 📋 Requisitos Previos

* **Sistema Operativo:** Linux (probado en Ubuntu/Debian).
* **Middleware:** OpenDDS (v3.34.0 o superior configurado con C++17) y TAO/ACE.
* **Compilación:** CMake (v3.10+) y GCC/Clang.
* **Librerías C++:** `sqlite3` (`libsqlite3-dev`), `nlohmann-json` (`nlohmann-json3-dev`).
* **Simuladores:** Python 3.

## 🚀 Instalación y Compilación

1. **Clonar el repositorio:**
   ```bash
   git clone [https://github.com/GalliMart1/replicacion_dds.git](https://github.com/GalliMart1/replicacion_dds.git)
   cd replicacion_dds
    ```

2. **Configurar el entorno OpenDDS:**

   Asegúrate de exportar las variables de entorno de OpenDDS antes de compilar o ejecutar:
   
    ```Bash
   source ~/OpenDDS/setenv.sh
    ```

4. **Compilar el proyecto:**

    ```Bash
   mkdir build && cd build
   cmake ..
   make
    ``` 

## 🖥️ Ejecución de los Nodos

Puedes levantar múltiples nodos en distintas terminales dentro de la misma máquina o en máquinas diferentes dentro de la misma red LAN. Los archivos de base de datos se crearán automáticamente en la carpeta build/.

**Terminal 1 (Nodo Principal):**

   ```Bash
   cd build
   ./DecentralizedC2 -DCPSConfigFile ../rtps.ini Consola_Principal
```

**Terminal 2 (Nodo Secundario):**

   ```Bash
   cd build
   ./DecentralizedC2 -DCPSConfigFile ../rtps.ini Radar_Proa
 ```

## 📡 Simuladores Disponibles

El proyecto incluye tres scripts de Python para inyectar datos en la red y probar la replicación bajo distintas cargas de trabajo. Se recomienda ejecutarlos desde la carpeta raíz del proyecto en una terminal independiente.
1. *Inyección Estática (simulador_radar.py):*
Envía un único contacto de radar (una Fragata) en una posición fija. Ideal para comprobar que el flujo UDP -> SQLite -> DDS funciona inicialmente.

```Bash
python3 simulador_radar.py
```

2. *Simulación de Movimiento Continuo (simulador_continuo.py):*
Simula el avance de un único buque, inyectando nuevas coordenadas cada 2 segundos con variaciones aleatorias para representar navegación real.

```Bash
python3 simulador_continuo.py
```

3. *Simulación de Flota Táctica (simulador_flota.py):*
La prueba de estrés principal. Despliega un arreglo de múltiples unidades (Caza F-16, Fragata Meko, Submarino TR-1700), cada una moviéndose a distintas velocidades y vectores de forma paralela y continua.

```Bash
python3 simulador_flota.py
```

## 🔍 Visualización de la Base de Datos

Para verificar que los datos se están replicando y actualizando correctamente, puedes usar DB Browser for SQLite.
Abre el visualizador apuntando al archivo de base de datos del nodo que desees observar (ej. Consola_Principal.db):

```Bash
sqlitebrowser build/Consola_Principal.db &
 ``` 
Ve a la pestaña "Browse Data" (Explorar Datos).
Selecciona la tabla TRACKS en el menú desplegable.

*Importante: DB Browser no se actualiza automáticamente. Mientras el simulador esté corriendo, presiona F5 (o el botón de Refrescar 🔃) para ver cómo las coordenadas cambian en tiempo real.*
