#!/bin/bash
clear
gcc -o pisplay main.c pisplay.c fmopl.c logo.c -lSDL2 -lSDL2_ttf -lm && \
rm *.o &>/dev/null ; \
emcc -Os main.c pisplay.c fmopl.c logo.c -s WASM=1 -s USE_SDL=2 -s USE_SDL_TTF=2 -s MODULARIZE=1 -o pisplay.js \
     --embed-file tunes --embed-file assets

# gcc -o pisplay main.c pisplay.c fmopl_linux.o logo_linux.o -lSDL2 -lSDL2_ttf -lm && \
# rm pisplay.o &>/dev/null ; \
# emcc main.c pisplay.c fmopl_emscripten.o logo_emscripten.o -s WASM=1 -s USE_SDL=2 -s USE_SDL_TTF=2 -s MODULARIZE=1 -o pisplay.js \
#      --embed-file tunes --embed-file assets
