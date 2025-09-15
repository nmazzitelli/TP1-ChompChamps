# TP1 ChompChamps

## Flujo básico ejecución

1. `make docker`  (abre el entorno provisto por la cátedra)
2. `make deps`    (instala dependencias del sistema: ncurses, etc.)
3. `make clean && make build` (limpia objetos previos y compila todos los binarios en `bin/`)
4. `make play`    (abre el menu interactivo)

## Lanzador interactivo (`make play`)

El target `make play` compila y ejecuta `bin/play`, un menú que permite:
- Configurar parámetros de partida.
- Lanzar el `master` propio con jugadores y vista.

## Ejecución con el binario de la cátedra (`make run-catedra`)

- Realizar el flujo básico de ejecución y reemplazar el paso 4 por: `make run-catedra`

Parámetros disponibles (se pueden overridear en la misma línea de make):

```
make run-catedra W=30 H=20 N=4 D=50 T=120 S=123
```

Donde:
- `W`: ancho del tablero (default 20)
- `H`: alto del tablero (default 20)
- `N`: cantidad de jugadores instanciados (default 2) – se lanzan N procesos `player` propios
- `D`: delay en ms entre ticks (default 200)
- `T`: timeout total de la partida en segundos (default 10)
- `S`: seed (0 => usa el tiempo actual)
