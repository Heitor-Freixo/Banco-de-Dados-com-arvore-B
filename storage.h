#ifndef STORAGE_H
#define STORAGE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

#define M 200
#define DESLOCAMENTO_NULO -1
#define NUM_FRAMES 16

typedef int64_t deslocamento_disco_t;

// Contadores globais para medição do EXPLAIN e Análise Experimental
extern uint64_t metrica_leituras_disco;
extern uint64_t metrica_escritas_disco;

typedef struct {
    struct {
        int32_t qtd_chaves;
        int32_t eh_folha;
    } cabecalho;
    int32_t chaves[M - 1];
    deslocamento_disco_t deslocamentos_registros[M - 1];
    int64_t deslocamentos_filhos[M];
} PaginaBTree;

typedef struct {
    int64_t id_pagina_raiz;
    int64_t proxima_pagina_livre;
} SuperBloco;

typedef struct {
    PaginaBTree pagina;
    int64_t id_pagina;
    int32_t pin_count;
    bool dirty;
    CRITICAL_SECTION mutex_frame; // Trava refinada por página
} FrameBuffer;

typedef struct {
    FrameBuffer pool[NUM_FRAMES];
    int32_t lru_lista[NUM_FRAMES];
    CRITICAL_SECTION mutex_pool;  // Controla apenas a busca/substituição de frames
} BufferPool;

void inicializar_buffer_pool(BufferPool* bp);
void destruir_buffer_pool(BufferPool* bp, FILE* arquivo);
FrameBuffer* pin_pagina(BufferPool* bp, FILE* arquivo, int64_t id_pagina);
void unpin_pagina(BufferPool* bp, int64_t id_pagina, bool dirty);
int64_t alocar_pagina(FILE* arquivo, SuperBloco* sb);
void liberar_id_pagina(SuperBloco* sb, int64_t id_pagina);
void ler_superbloco(FILE* arquivo, SuperBloco* sb);
void escrever_superbloco(FILE* arquivo, SuperBloco* sb);

#endif