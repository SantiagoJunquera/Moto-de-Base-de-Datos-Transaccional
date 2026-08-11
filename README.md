# Documentación Técnica y Manual de Uso: Motor de Base de Datos Transaccional Distribuido (TxDB-OS)

---

## 1. Visión General del Proyecto

**TxDB-OS** es un motor de base de datos transaccional distribuido programado íntegramente en C++. El sistema imita la arquitectura interna de un motor de base de datos real, abstrayéndose del sistema de archivos y gestor de memoria nativos del host para sus operaciones críticas.

El ecosistema está compuesto por 4 procesos independientes que se comunican entre sí exclusivamente a través de sockets TCP/IP de bajo nivel en un esquema multinivel:

1. **TxClient**: Consola interactiva REPL con formato de tabla ASCII y conexiones Keep-Alive.
2. **TxKernel**: Orquestador central, gestor de concurrencia y detector de Deadlocks mediante grafos dirigidos.
3. **TxMemory**: Gestor de Memoria RAM, paginación de 4KB y algoritmo de reemplazo LRU.
4. **TxFS**: File System binario personalizado en disco (`txdb_storage.dat`) administrado mediante una File Allocation Table.

### Formato de Registros Binarios en Disco
Cada página de 4KB en `TxMemory` y bloque en `TxFS` almacena estructuras de datos binarias empaquetadas con `#pragma pack(1)` para soportar múltiples tablas en el disco:

```cpp
// Tabla "usuarios" (Bloque 1)
struct UserRecord {
    uint32_t id;
    char nombre[32];
    int32_t saldo;
    uint8_t is_active; // 1 = activo, 0 = inactivo
};

// Tabla "productos" (Bloque 2)
struct ProductRecord {
    uint32_t id;
    char nombre[32];
    int32_t precio;
    uint8_t is_active; // 1 = activo, 0 = inactivo
};
```

### Flujo de Consulta y Renderizado de Filas
1. **Petición**: `TxClient` envía la query `SELECT * FROM usuarios;` o `SELECT * FROM productos;`.
2. **Concurrencia**: `TxKernel` analiza la tabla requerida y solicita el Lock correspondiente (`S` para lectura o `X` para escritura) al `LockManager`.
3. **Paginación**: `TxKernel` solicita la página correspondiente a `TxMemory`. Si ocurre un *Page Fault*, `TxMemory` la recupera desde `TxFS` (`txdb_storage.dat`).
4. **Modificación Binaria**: En caso de `UPDATE`, `TxKernel` localiza la fila por `id` en la página RAM y actualiza el campo correspondiente.
5. **Renderizado en Consola**: `TxKernel` retorna el payload binario de 4KB al `TxClient`, el cual castea los registros y dibuja una tabla formateada en pantalla:

```text
+----+--------------------------------+--------+
| ID | Nombre                         | Saldo  |
+----+--------------------------------+--------+
| 1  | Juan Perez                     |   1000 |
| 2  | Maria Gomez                    |   2500 |
| 5  | Carlos Rodriguez               |    500 |
+----+--------------------------------+--------+
 (3 fila(s) leída(s) de la tabla 'usuarios' en txdb_storage.dat)
```

---

## 2. Análisis Exhaustivo de Seguridad, Tolerancia a Fallos y Manejo de Recursos

### A. ¿Qué pasa si un cliente se cae o desconecta abruptamente?
- **Detección Automática por Socket**: Cuando la aplicación cliente finaliza de forma imprevista o cierra su proceso, el socket en `TxKernel` recibe un evento de desconexión (`recv` retorna 0 o error).
- **Liberación Inmediata de Locks (`release_locks`)**: El hilo atiende el evento en `KernelServer`, sale del bucle de cliente y ejecuta `lock_manager_.release_locks(pcb)`.
- **Desbloqueo de Clientes en Espera**: Todos los locks adquiridos por el cliente caído son liberados al instante y se notifica a las transacciones que estaban esperando en cola (`grant_pending_locks`), evitando que queden bloqueadas.
- **Limpieza de Recursos (PCB & Threads)**: La PCB de la transacción caída es eliminada de `active_pcbs_`. El hilo del cliente fue instanciado con `.detach()`, por lo que el Sistema Operativo destruye automáticamente el contexto del hilo y libera su pila de RAM.

### B. ¿Qué pasa si cae el servidor de la Base de Datos (`TxKernel`, `TxMemory` o `TxFS`)?
- **Desconexión Limpia de Clientes**: Al detenerse un servidor, su `ServerSocket` se cierra. Los sockets conectados reciben EOF y los clientes detectan la desconexión mostrando un mensaje de error limpio sin congelarse ni romper la terminal.
- **Persistencia e Integridad en Disco (`TxFS`)**: `DiskManager` sincroniza todos los bloques modificados a través de `file_stream_.flush()`. La File Allocation Table (FAT) se persiste en el **Bloque 0** del disco binario `txdb_storage.dat`.

### C. Aislamiento y Seguridad de Red (`127.0.0.1 Loopback Strict`)
- **Enlace Local Restringido**: Todos los módulos servidores se enlazan **estrictamente a `127.0.0.1`**. Ningún puerto queda expuesto a redes externas, eliminando 100% de brechas de seguridad de red y previniendo alertas del Firewall de Windows.

### D. Liberación de Hilos y Manejo de Memoria
- **Cero Acumulación de Hilos Fantasma**: Todos los sockets entrantes se atienden en hilos desvinculados (`std::thread(...).detach()`), lo que garantiza que al desconectarse un cliente los recursos del kernel se liberen de inmediato.
- **Destructores RAII**: Uso exclusivo de RAII (`std::unique_ptr`, `std::lock_guard`, `std::unique_lock`) que garantizan el cierre automático de descriptores de archivos y sockets.

---

## 3. Principios SOLID Aplicados

1. **Single Responsibility Principle**: Cada módulo y clase tiene una responsabilidad única y delimitada (`DiskManager` maneja I/O binario, `FatTable` maneja índices de bloques, `LRUReplacer` rastrea páginas candidatas a desalojo, `LockManager` administra la matriz 2PL).
2. **Open/Closed Principle**: El protocolo de mensajes binarios permite agregar nuevos opcodes sin modificar la infraestructura de sockets.
3. **Liskov Substitution Principle**: Abstracciones limpias sobre los sockets de red.
4. **Interface Segregation Principle**: Encabezados independientes en subdirectorios `include/` de cada módulo (`TxClient/include`, `TxKernel/include`, `TxMemory/include`, `TxFS/include`).
5. **Dependency Inversion Principle**: Los componentes de nivel superior dependen de abstracciones de servicios en lugar de implementaciones acopladas.

---

## 4. Resumen de Walkthroughs y Pruebas Automatizadas

El proyecto fue validado con **17 pruebas unitarias e integrales** en **GoogleTest** divididas en 6 ejecutables:

| Ejecutable | Área de Prueba | Cantidad | Estado |
| :--- | :--- | :---: | :---: |
| `network_tests.exe` | Red TCP & Protocolo Binario | 4 tests | **PASSED (100%)** |
| `txfs_tests.exe` | File System Binario & FAT | 3 tests | **PASSED (100%)** |
| `txmemory_tests.exe` | Buffer Pool & Algoritmo LRU | 3 tests | **PASSED (100%)** |
| `txkernel_tests.exe` | Lock Manager 2PL & Deadlocks | 3 tests | **PASSED (100%)** |
| `txclient_tests.exe` | Consola REPL e Integración | 2 tests | **PASSED (100%)** |
| `resilience_tests.exe` | Caída de Clientes & 10 Clientes Concurrentes | 2 tests | **PASSED (100%)** |
| **Total** | **Sistema Completo TxDB-OS** | **17 Tests** | **PASSED (100%)** |

---

## 5. Guía Paso a Paso: Cómo Compilar y Ejecutar el Proyecto

### A. Requisitos del Sistema
- **Sistema Operativo**: Windows 10/11 (o Linux POSIX).
- **Compilador**: C++20 (MSVC Visual Studio 2022 o GCC/Clang).
- **Herramienta de Construcción**: CMake version 3.14 o superior.

### B. Compilación desde la Consola (PowerShell / CMD)

Abrir la terminal en la raíz del repositorio:

```powershell
# 1. Configurar la carpeta de build con CMake
cmake -B build -S . -G "Visual Studio 17 2022"

# 2. Compilar todo el proyecto en modo Debug (o Release)
cmake --build build --config Debug
```

Una vez compilado, los ejecutables estarán generados en:
- `build\TxFS\Debug\TxFS.exe`
- `build\TxMemory\Debug\TxMemory.exe`
- `build\TxKernel\Debug\TxKernel.exe`
- `build\TxClient\Debug\TxClient.exe`
- `build\tests\Debug\*.exe` (Pruebas unitarias)

---

### C. Ejecución de la Suite de Pruebas Automatizadas

Para validar la resiliencia y el comportamiento del motor en tu máquina:

```powershell
.\build\tests\Debug\network_tests.exe
.\build\tests\Debug\txfs_tests.exe
.\build\tests\Debug\txmemory_tests.exe
.\build\tests\Debug\txkernel_tests.exe
.\build\tests\Debug\txclient_tests.exe
.\build\tests\Debug\resilience_tests.exe
```

---

### D. Cómo Levantar el Clúster Distribuido Completo

Para levantar el sistema de base de datos completo de forma distribuida, abrí 4 ventanas independientes de PowerShell/CMD y ejecutá los módulos en el siguiente orden:

#### Terminal 1: Módulo TxFS (Almacenamiento Persistente)
```powershell
.\build\TxFS\Debug\TxFS.exe
```

#### Terminal 2: Módulo TxMemory (Buffer Pool & RAM)
```powershell
.\build\TxMemory\Debug\TxMemory.exe
```

#### Terminal 3: Módulo TxKernel (Orquestador Transaccional)
```powershell
.\build\TxKernel\Debug\TxKernel.exe
```

#### Terminal 4: Módulo TxClient (Consola de Usuario REPL)
```powershell
.\build\TxClient\Debug\TxClient.exe
```

---

## 6. Manual del Usuario: Uso de la Consola `TxClient`

```sql
-- Iniciar transacción
txdb> BEGIN

-- Consultar tabla "usuarios" o "productos"
txdb [Tx #1]> SELECT * FROM usuarios;
txdb [Tx #1]> SELECT * FROM productos;

-- Modificar datos (adquiere Exclusive Lock en Kernel)
txdb [Tx #1]> UPDATE usuarios SET saldo = 100 WHERE id = 5;
txdb [Tx #1]> UPDATE productos SET precio = 90 WHERE id = 2;

-- Confirmar guardado (Commit)
txdb [Tx #1]> COMMIT

-- Abortar cambios (Rollback)
txdb [Tx #1]> ROLLBACK

-- Salir
txdb> EXIT
```
