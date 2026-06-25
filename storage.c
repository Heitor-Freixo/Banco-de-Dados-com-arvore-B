#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>  
#include <io.h>     
#include <string.h>
#include "storage.h"

#ifdef __MINGW32__
int _fileno(FILE *stream);
#endif

uint64_t metrica_leituras_disco = 0;
uint64_t metrica_escritas_disco = 0;

static void fsync_interno(FILE* arquivo) {
    fflush(arquivo);
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(arquivo));
    if (handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle);
    }
}

void inicializar_buffer_pool(BufferPool* bp) {
    InitializeCriticalSection(&bp->mutex_pool);
    for (int32_t i = 0; i < NUM_FRAMES; i++) {
        bp->pool[i].id_pagina = DESLOCAMENTO_NULO;
        bp->pool[i].pin_count = 0;
        bp->pool[i].dirty = false;
        InitializeCriticalSection(&bp->pool[i].mutex_frame);
        bp->lru_lista[i] = i;
    }
}

void destruir_buffer_pool(BufferPool* bp, FILE* arquivo) {
    for (int32_t i = 0; i < NUM_FRAMES; i++) {
        EnterCriticalSection(&bp->pool[i].mutex_frame);
        if (bp->pool[i].id_pagina != DESLOCAMENTO_NULO && bp->pool[i].dirty) {
            fseek(arquivo, bp->pool[i].id_pagina * sizeof(PaginaBTree), SEEK_SET);
            fwrite(&bp->pool[i].pagina, sizeof(PaginaBTree), 1, arquivo);
            metrica_escritas_disco++;
            bp->pool[i].dirty = false;
        }
        LeaveCriticalSection(&bp->pool[i].mutex_frame);
        DeleteCriticalSection(&bp->pool[i].mutex_frame);
    }
    DeleteCriticalSection(&bp->mutex_pool);
}

FrameBuffer* pin_pagina(BufferPool* bp, FILE* arquivo, int64_t id_pagina) {
    EnterCriticalSection(&bp->mutex_pool);
    
    for (int32_t i = 0; i < NUM_FRAMES; i++) {
        if (bp->pool[i].id_pagina == id_pagina) {
            bp->pool[i].pin_count++;
            
            int32_t idx_lru = -1;
            for (int32_t j = 0; j < NUM_FRAMES; j++) {
                if (bp->lru_lista[j] == i) { idx_lru = j; break; }
            }
            if (idx_lru != -1) {
                for (int32_t j = idx_lru; j < NUM_FRAMES - 1; j++) {
                    bp->lru_lista[j] = bp->lru_lista[j + 1];
                }
                bp->lru_lista[NUM_FRAMES - 1] = i;
            }
            
            LeaveCriticalSection(&bp->mutex_pool);
            return &bp->pool[i];
        }
    }

    int32_t frame_escolhido = -1;
    for (int32_t i = 0; i < NUM_FRAMES; i++) {
        int32_t idx_candidato = bp->lru_lista[i];
        if (bp->pool[idx_candidato].pin_count == 0) {
            frame_escolhido = idx_candidato;
            for (int32_t j = i; j < NUM_FRAMES - 1; j++) {
                bp->lru_lista[j] = bp->lru_lista[j + 1];
            }
            bp->lru_lista[NUM_FRAMES - 1] = frame_escolhido;
            break;
        }
    }

    if (frame_escolhido == -1) {
        LeaveCriticalSection(&bp->mutex_pool);
        return NULL;
    }

    FrameBuffer* f = &bp->pool[frame_escolhido];
    EnterCriticalSection(&f->mutex_frame);

    if (f->id_pagina != DESLOCAMENTO_NULO && f->dirty) {
        fseek(arquivo, f->id_pagina * sizeof(PaginaBTree), SEEK_SET);
        fwrite(&f->pagina, sizeof(PaginaBTree), 1, arquivo);
        metrica_escritas_disco++;
    }

    f->id_pagina = id_pagina;
    f->pin_count = 1;
    f->dirty = false;

    fseek(arquivo, id_pagina * sizeof(PaginaBTree), SEEK_SET);
    if (fread(&f->pagina, sizeof(PaginaBTree), 1, arquivo) != 1) {
        memset(&f->pagina, 0, sizeof(PaginaBTree));
    }
    metrica_leituras_disco++;

    LeaveCriticalSection(&f->mutex_frame);
    LeaveCriticalSection(&bp->mutex_pool);
    return f;
}

void unpin_pagina(BufferPool* bp, int64_t id_pagina, bool dirty) {
    EnterCriticalSection(&bp->mutex_pool);
    for (int32_t i = 0; i < NUM_FRAMES; i++) {
        if (bp->pool[i].id_pagina == id_pagina) {
            if (dirty) bp->pool[i].dirty = true;
            bp->pool[i].pin_count--;
            break;
        }
    }
    LeaveCriticalSection(&bp->mutex_pool);
}

int64_t alocar_pagina(FILE* arquivo, SuperBloco* sb) {
    int64_t id;
    if (sb->proxima_pagina_livre != DESLOCAMENTO_NULO) {
        id = sb->proxima_pagina_livre;
        PaginaBTree p;
        fseek(arquivo, id * sizeof(PaginaBTree), SEEK_SET);
        if (fread(&p, sizeof(PaginaBTree), 1, arquivo) == 1) {
            sb->proxima_pagina_livre = p.deslocamentos_filhos[0];
        } else {
            sb->proxima_pagina_livre = DESLOCAMENTO_NULO;
        }
        metrica_leituras_disco++;
    } else {
        fseek(arquivo, 0, SEEK_END);
        id = ftell(arquivo) / sizeof(PaginaBTree);
        if (id == 0) id = 1; 
        
        // Expande o arquivo fisicamente no disco para evitar o loop infinito no ftell
        int64_t offset_final_nova_pagina = (id + 1) * sizeof(PaginaBTree) - 1;
        fseek(arquivo, offset_final_nova_pagina, SEEK_SET);
        fputc('\0', arquivo);
    }
    return id;
}

void liberar_id_pagina(SuperBloco* sb, int64_t id_pagina) {
    PaginaBTree p;
    memset(&p, 0, sizeof(PaginaBTree));
    p.deslocamentos_filhos[0] = sb->proxima_pagina_livre;
    sb->proxima_pagina_livre = id_pagina;
}

void ler_superbloco(FILE* arquivo, SuperBloco* sb) {
    fseek(arquivo, 0, SEEK_SET);
    if (fread(sb, sizeof(SuperBloco), 1, arquivo) != 1) {
        sb->id_pagina_raiz = DESLOCAMENTO_NULO;
        sb->proxima_pagina_livre = DESLOCAMENTO_NULO;
    }
}

void escrever_superbloco(FILE* arquivo, SuperBloco* sb) {
    fseek(arquivo, 0, SEEK_SET);
    fwrite(sb, sizeof(SuperBloco), 1, arquivo);
    fsync_interno(arquivo);
}