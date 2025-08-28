# Proyecto-Final-SO

Simulador de un sistema automatizado de preparación de hamburguesas en C (Linux), usando procesos, memoria compartida y semáforos.

Características clave:
- N bandas de preparación (procesos workers) con inventario propio y cola FIFO.
- Cola global de órdenes (FIFO). Si ninguna banda puede atender, la orden espera.
- Restocker repone inventario aleatoriamente.
- Dashboard muestra en una pantalla el estado en tiempo real (inventarios, procesadas, alertas).
- Controller en otra pantalla permite pausar/reanudar cada banda.

Requisitos de compilación:
- gcc, make, Linux con soporte POSIX (semaphores, shm).

Compilar:
```
make
```

Ejecutar (ejemplo con 3 bandas, generando órdenes aleatorias, en tres terminales):
```
./burger_manager -n 3 -g
./dashboard
./controller
```

Comandos en controller (entra por stdin del proceso controller):
- `p i` pausa banda i
- `r i` reanuda banda i
- `q` salir

Notas:
- Ingredientes: pan, tomate, cebolla, lechuga, queso, carne.
- Las órdenes aleatorias siempre requieren pan y carne. El resto es opcional.
