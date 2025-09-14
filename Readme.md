# ChompChamps - Juego Multijugador con IPC

ChompChamps es un juego multijugador tipo Snake implementado en C que utiliza memoria compartida, semáforos y pipes para la comunicación entre procesos.

## Descripción del Juego

ChompChamps es un juego donde múltiples jugadores compiten en un tablero rectangular para obtener la mayor puntuación. Los jugadores se mueven por el tablero capturando celdas que contienen recompensas (valores del 1 al 9). El juego no es por turnos, lo que lo hace más dinámico y competitivo.

### Reglas del Juego

- Los jugadores pueden moverse en 8 direcciones (↑↗→↘↓↙←↖)
- No pueden salir del tablero ni pasar por celdas ocupadas
- Las celdas capturadas otorgan puntos y quedan marcadas como del jugador
- El juego termina cuando ningún jugador puede moverse o se agota el tiempo
- Gana el jugador con mayor puntaje (desempates por eficiencia de movimientos)

## Arquitectura del Sistema

El proyecto consta de 3 binarios principales:

### 1. Máster (`bin/master`)
- Controla el estado del juego y valida movimientos
- Maneja la memoria compartida y sincronización
- Implementa política round-robin para atender jugadores
- Crea y supervisa procesos de jugadores y vista

### 2. Vista (`bin/view`)
- Muestra el estado del tablero en tiempo real
- Se conecta a la memoria compartida para leer el estado
- Sincroniza con el máster para mostrar actualizaciones

### 3. Jugador (`bin/player`)
- Evalúa movimientos considerando recompensas y movilidad futura
- Se comunica con el máster via pipes

## Mecanismos de IPC Utilizados

### Memoria Compartida
- **`/game_state`**: Estado completo del juego (tablero, jugadores, puntuaciones)
- **`/game_sync`**: Semáforos para sincronización entre procesos

### Semáforos
- Implementa el problema lectores-escritores para acceso al estado
- Previene inanición del proceso máster
- Coordina turnos de jugadores y notificaciones a la vista

### Pipes Anónimos
- Comunicación unidireccional de jugadores hacia el máster
- Envío de movimientos (valores 0-7 representando direcciones)
- Detección de jugadores bloqueados via EOF

## Compilación y Ejecución

### Requisitos
- GCC con soporte C99
- Sistema POSIX con soporte para memoria compartida
- Docker: `agodio/itba-so-multi-platform:3.0`

### Makefile
Para correr el contenedor de docker ejecutar el siguiente comando:
make container

Dentro del mismo, cambiar de directorio a workspace:
cd workspace

### Ncurses
make ncurses

### Compilación
make

### Ejecución Básica

# Usando Makefile
make run_def (default, sin vista y dos jugadores)
make run_view (default, con vista y dos jugadores)
make run h=<NUM> w=<NUM> d=<NUM> t=<NUM> s=<NUM> v="./bin/view" p="./bin/player ./bin/player ..." (para modificar parametros)

# Sin Makefile
Ejemplo de juego con vista y dos jugadores (w=h=15)
./bin/master -w 15 -h 15 -v ./bin/view -p ./bin/player ./bin/player

Ejemplo de juego sin vista y dos jugadores (w=h=10)
./bin/master -w 10 -h 10 -p ./bin/player

### Parámetros del Máster
- `-w width`: Ancho del tablero (mínimo 10, default 10)
- `-h height`: Alto del tablero (mínimo 10, default 10)
- `-d delay`: Delay en ms entre actualizaciones (default 200)
- `-t timeout`: Timeout en segundos sin movimientos válidos (default 10)
- `-s seed`: Semilla para generación del tablero (default: time(NULL))
- `-v view_path`: Ruta del binario de vista (opcional)
- `-p player1 player2 ...`: Rutas de binarios de jugadores (1-9 jugadores)

## Estructura del Proyecto
CHOMPCHAMPS-GRUPO-27
├── include/
│   ├── game_functions.h
│   ├── ipc.h
│   └── structs.h
├── src/
│   ├── game_functions.c    # Funciones utilitarias propias del juego
│   ├── ipc.c               # Funciones utilitarias para manejo de memoria compartida y semaforos
│   ├── master.c            # Proceso máster
│   ├── view.c              # Proceso vista
│   └── player.c            # Proceso jugador (IA)
├── obj/                    # Archivos objeto (generado)
├── bin/                    # Binarios compilados (generado)
├── Makefile               # Sistema de compilación
└── README.md              # Este archivo

## Algoritmo del jugador

1. **Valor de recompensa**: Prioriza celdas con mayor puntuación
2. **Proximidad al centro**: Favorece posiciones centrales para mayor movilidad
3. **Movilidad futura**: Cuenta celdas libres adyacentes para evitar quedar atrapado
4. **Validez del movimiento**: Verifica límites y colisiones

## Características Técnicas

### Sincronización
- **Libre de deadlocks**: Orden consistente de adquisición de semáforos
- **Sin inanición**: Implementación correcta del problema lectores-escritores
- **Sin condiciones de carrera**: Acceso sincronizado a memoria compartida
- **Sin espera activa**: Uso de semáforos para bloqueo eficiente

### Gestión de Recursos
- Limpieza automática de memoria compartida y semáforos
- Manejo de señales para terminación limpia
- Detección y manejo de procesos terminados inesperadamente

## Limitaciones Conocidas

- Máximo 9 jugadores simultáneos
- Tablero mínimo 10x10
- Requiere sistema POSIX con soporte completo para memoria compartida

## Problemas Resueltos Durante el Desarrollo

1. **Sincronización compleja**: Implementación cuidadosa del patrón lectores-escritores
2. **Gestión de pipes**: Manejo correcto de EOF y select() para múltiples jugadores
3. **Limpieza de recursos**: Asegurar liberación de memoria compartida en todos los casos
4. **Round-robin justo**: Evitar sesgos sistemáticos en la atención de jugadores
