# TP1 ChompChamps

## Flujo de comandos

1) **make docker**
2) **make deps**
3) **make clean && make**
4) **make run**

## Comandos con `make`

- **`make docker`**  
  Levanta un contenedor de Docker con la imagen oficial de la cátedra (`agodio/itba-so-multi-platform:3.0`), montando el proyecto en `/work`.

- **`make dstop`**  
  Detiene/elimina el contenedor si quedó corriendo.

- **`make deps`**  
  Instala dependencias necesarias dentro del contenedor (ej: `ncurses`).  
  Cada vez que abras un contenedor nuevo, corré `make deps` una vez antes de compilar.

- **`make`**  
  Compila todos los binarios (`bin/master`, `bin/view`, etc.).

- **`make clean`**  
  Limpia la carpeta `bin/`.

- **`make run-demo`**  
  Corre `master` (en background) y después `view` para ver un tablero de prueba.

- **`make run`**  
  Igual que `run-demo`, pero permite pasar parámetros (width, height, delay, timeout, seed), siempre y cuando cumplan con el minimo pedido en el codigo:

  **Ejemplo**
  ```
  make run W=20 H=12 D=200 T=10 S=7