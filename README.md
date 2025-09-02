# Proyecto Final - Sistema Operativos
## Simulador de Sistema Automatizado de Preparación de Hamburguesas

### 📋 Descripción
Sistema multiproceso en C que simula un restaurante de hamburguesas automatizado con bandas de preparación independientes, gestión de inventario y control en tiempo real.

### 🏗️ Arquitectura del Sistema

#### Componentes principales:
- **Manager**: Proceso principal que gestiona la memoria compartida y distribuye órdenes
- **Bandas (Workers)**: Procesos independientes que preparan hamburguesas
- **Dashboard**: Interfaz de monitoreo en tiempo real
- **Controller**: Interfaz de control para gestión manual

#### Características técnicas:
- ✅ **Multiproceso**: Cada banda es un proceso independiente (fork)
- ✅ **Memoria compartida**: Estado global usando POSIX shared memory
- ✅ **Sincronización**: Semáforos POSIX para evitar condiciones de carrera
- ✅ **Colas FIFO**: Cola global + colas individuales por banda
- ✅ **Distribución inteligente**: Asignación equitativa basada en carga
- ✅ **Alertas automáticas**: Notificaciones cuando faltan ingredientes
- ✅ **Control dinámico**: Pausar/reanudar bandas en tiempo real

### 🚀 Instalación y Compilación

#### Requisitos:
- GCC compiler
- GNU Make
- Linux con soporte POSIX (semáforos, memoria compartida)

#### Compilar:
```bash
make
```

#### Limpiar archivos compilados:
```bash
make clean
```

### 🎮 Ejecución del Sistema

#### Paso 1: Iniciar el Manager (Terminal 1)
```bash
./burger_manager [opciones]
```

**Opciones disponibles:**
- `-n N`: Número de bandas (1-16) **[REQUERIDO]**
- `-g`: Generar órdenes aleatorias continuamente
- `-s seed`: Semilla para generador aleatorio (entero ≥ 0)
- `-i a,b,c,d,e,f`: Inventario inicial por ingrediente

**Ejemplos:**
```bash
# 2 bandas con inventario estándar
./burger_manager -n 2

# 3 bandas con generación automática y inventario personalizado
./burger_manager -n 3 -g -i 10,8,5,6,7,9

# Reproducible con semilla fija
./burger_manager -n 2 -g -s 123 -i 5,5,5,5,5,5
```

#### Paso 2: Iniciar el Dashboard (Terminal 2)
```bash
./dashboard
```

**Información mostrada:**
```
=== BURGER MANAGER DASHBOARD ===
Bandas: 2 | Cola Global: 3 órdenes
🚨 [ALERTA] Orden 5 bloqueada: falta carne en todas las bandas

ESTADO DE BANDAS:
ID  Estado  Proc  Cola  Inventario (p/t/c/l/q/m)
--  ------  ----  ----  ------------------------
B0  ACTIVA*    5     2  3/2/1/4/2/0
B1  LISTA      3     0  5/5/5/5/5/5

Leyenda: * = procesando orden, p=pan, t=tomate, c=cebolla, l=lechuga, q=queso, m=carne
```

#### Paso 3: Usar el Controller (Terminal 3)
```bash
./controller
```

### 🎛️ Comandos del Controller

#### Comandos de información:
- `help` o `h`: Mostrar ayuda completa
- Presionar Enter: Continuar sin acción

#### Comandos de generación de órdenes:

**Generar órdenes aleatorias:**
```bash
gen N
```
- `gen 5`: Genera 5 órdenes aleatorias
- `gen 1`: Genera 1 orden aleatoria

**Crear orden manual:**
```bash
ord a b c d e f
```
Donde cada letra es 0 (no incluir) o 1 (incluir):
- a = pan, b = tomate, c = cebolla, d = lechuga, e = queso, f = carne

**Ejemplos de órdenes:**
```bash
ord 1 0 0 0 0 1    # Hamburguesa simple (solo pan y carne)
ord 1 1 1 1 1 1    # Hamburguesa completa
ord 1 1 0 1 1 1    # Sin cebolla
ord 1 0 1 0 1 1    # Solo pan, cebolla, queso y carne
```

#### Comandos de control de bandas:

**Pausar banda:**
```bash
p N
```
- `p 0`: Pausa la banda 0
- `p 1`: Pausa la banda 1

**Reanudar banda:**
```bash
r N  
```
- `r 0`: Reanuda la banda 0
- `r 1`: Reanuda la banda 1

#### Comandos de gestión de inventario:

**Agregar/modificar inventario:**
```bash
inv banda ingrediente cantidad
```

**Índices de ingredientes:**
- 0 = pan
- 1 = tomate  
- 2 = cebolla
- 3 = lechuga
- 4 = queso
- 5 = carne

**Ejemplos de gestión de inventario:**
```bash
inv 0 5 10    # Banda 0: agregar 10 carnes
inv 1 0 20    # Banda 1: set 20 panes
inv 0 1 0     # Banda 0: agotar tomates (para testing)
inv 1 2 5     # Banda 1: set 5 cebollas
inv 0 4 15    # Banda 0: agregar 15 quesos
```

**Escenarios comunes:**
```bash
# Reabastecer todo en banda 0
inv 0 0 20
inv 0 1 15  
inv 0 2 10
inv 0 3 12
inv 0 4 18
inv 0 5 8

# Simular falta de ingrediente crítico
inv 0 5 0     # Sin carne en banda 0
inv 1 5 0     # Sin carne en banda 1 -> Verás alerta
```

#### Salir del controller:
```bash
q
```
o `quit` o `exit` o Ctrl+D

### 🧪 Escenarios de Prueba

#### Prueba básica de funcionamiento:
```bash
# Terminal 1
./burger_manager -n 2 -i 5,5,5,5,5,5

# Terminal 2 
./dashboard

# Terminal 3 - En controller:
gen 3
# Observar en dashboard cómo se distribuyen
```

#### Prueba de distribución equitativa:
```bash
# En controller:
gen 10
# Verificar que las órdenes se distribuyen entre bandas según su carga
```

#### Prueba de alertas y manejo de inventario:
```bash
# En controller:
gen 5
inv 0 5 0    # Agotar carne banda 0
inv 1 5 0    # Agotar carne banda 1  
gen 3        # Deberías ver alerta en dashboard
inv 0 5 10   # Reabastecer carne banda 0
# Observar cómo se procesan las órdenes pendientes
```

#### Prueba de control de bandas:
```bash
# En controller:
gen 8
p 1          # Pausar banda 1
gen 5        # Solo banda 0 trabaja
r 1          # Reanudar banda 1
# Observar redistribución
```

### 🔧 Personalización

#### Configuración de inventario inicial:
```bash
# Inventario balanceado
./burger_manager -n 3 -i 20,15,10,12,18,8

# Inventario limitado (para testing)
./burger_manager -n 2 -i 3,3,3,3,3,3

# Solo ingredientes básicos
./burger_manager -n 2 -i 10,0,0,0,0,10
```

#### Generación automática vs. manual:
```bash
# Solo órdenes manuales
./burger_manager -n 2

# Generación automática continua
./burger_manager -n 2 -g

# Generación automática con semilla (reproducible)
./burger_manager -n 2 -g -s 12345
```

### 🛑 Detener el Sistema

1. **Controller**: `q` o Ctrl+D
2. **Dashboard**: Ctrl+C  
3. **Manager**: Ctrl+C

El sistema limpia automáticamente:
- Memoria compartida
- Semáforos
- Procesos worker

### 📊 Interpretación del Dashboard

**Estados de banda:**
- `LISTA`: Banda activa, esperando órdenes
- `ACTIVA*`: Banda procesando una orden
- `PAUSA`: Banda pausada manualmente

**Campos importantes:**
- `Proc`: Hamburguesas completadas
- `Cola`: Órdenes pendientes en la cola de esta banda
- `Inventario`: Cantidad disponible de cada ingrediente

**Alertas automáticas:**
- Se muestran cuando las órdenes no pueden procesarse
- Se limpian automáticamente cuando se resuelve el problema
- Indican qué ingrediente específico falta

### 🎯 Algoritmo de Distribución

El sistema usa distribución inteligente:
1. **Toma todas las órdenes** de la cola global
2. **Evalúa cada banda** (running, inventario, carga actual)
3. **Asigna a la banda con menor cola** que pueda cumplir la orden
4. **Órdenes no asignables** vuelven a cola global con alerta
5. **Redistribución automática** cuando se reabastece inventario

### 📋 Cumplimiento de Requisitos

#### ✅ Ingreso de parámetros CLI:
- Validación robusta con `strtol/strtoul`
- Mensajes de error claros
- Opción `-i` para inventario personalizado

#### ✅ Sincronización de eventos:
- Semáforos POSIX para colas y bandas
- Mutex por banda para inventario/estado
- Sin deadlocks (orden consistente de locks)

#### ✅ Manejo de procesos:
- Creación con `fork()` 
- Terminación controlada con `SIGINT`
- Suspensión/reanudación por flag `running`

#### ✅ Datos compartidos:
- Estructura `SharedState` en memoria compartida
- Sincronización apropiada con semáforos
- Consistencia de datos garantizada

#### ✅ Estructuras eficientes:
- Ring buffers con operaciones O(1)
- Semáforos para señalización
- Distribución por carga mínima

#### ✅ Solución multiproceso:
- Manager + N workers + dashboard + controller
- Distribución eficiente de tareas
- Comunicación vía memoria compartida

#### ✅ Cola FIFO y alertas:
- Cola global FIFO implementada
- Alertas automáticas por ingredientes faltantes
- Despacho automático al reabastecer

#### ✅ Dashboard tiempo real:
- Estado completo de todas las bandas
- Inventario actual por banda
- Órdenes procesadas y pendientes
- Actualización en tiempo real

#### ✅ Control de bandas:
- Pausar/reanudar bandas individuales
- Control completo de inventario
- Monitoreo del sistema
- Respuesta inmediata
