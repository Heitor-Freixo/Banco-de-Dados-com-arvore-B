#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>  
#include <io.h>     
#include <string.h>
#include <stdlib.h>
#include "btree.h"

#ifdef __MINGW32__
int _fileno(FILE *stream);
#endif

static bool busca_binaria_na_pagina(const PaginaBTree* pagina, int32_t alvo, int32_t* indice) {
    int32_t inicio = 0;
    int32_t fim = pagina->cabecalho.qtd_chaves - 1;
    while (inicio <= fim) {
        int32_t meio = inicio + (fim - inicio) / 2;
        if (pagina->chaves[meio] == alvo) { *indice = meio; return true; }
        if (pagina->chaves[meio] < alvo) inicio = meio + 1;
        else fim = meio - 1;
    }
    *indice = inicio;
    return false;
}

static void fsync_interno(FILE* arquivo) {
    fflush(arquivo);
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(arquivo));
    if (handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle);
    }
}

void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo) {
    btree->arquivo_db = arquivo;
    inicializar_buffer_pool(&btree->pool);
    ler_superbloco(arquivo, &btree->superbloco);
}

void destruir_btree(GerenciadorBTree* btree) {
    destruir_buffer_pool(&btree->pool, btree->arquivo_db);
    fsync_interno(btree->arquivo_db);
}

ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula) {
    ResultadoBusca resultado = {false, DESLOCAMENTO_NULO, 0, DESLOCAMENTO_NULO, 0};
    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) return resultado;

    int64_t id_atual = btree->superbloco.id_pagina_raiz;
    FrameBuffer* f_atual = pin_pagina(&btree->pool, btree->arquivo_db, id_atual);
    EnterCriticalSection(&f_atual->mutex_frame);

    while (id_atual != DESLOCAMENTO_NULO) {
        resultado.paginas_lidas++;
        int32_t indice = 0;
        bool achou = busca_binaria_na_pagina(&f_atual->pagina, chave_matricula, &indice);

        if (achou) {
            resultado.encontrada = true;
            resultado.id_pagina_onde_esta = id_atual;
            resultado.indice_na_pagina = indice;
            resultado.deslocamento_registro = f_atual->pagina.deslocamentos_registros[indice];
            LeaveCriticalSection(&f_atual->mutex_frame);
            unpin_pagina(&btree->pool, id_atual, false);
            break;
        }

        if (f_atual->pagina.cabecalho.eh_folha) {
            LeaveCriticalSection(&f_atual->mutex_frame);
            unpin_pagina(&btree->pool, id_atual, false);
            break;
        }

        int64_t id_proximo = f_atual->pagina.deslocamentos_filhos[indice];
        FrameBuffer* f_proximo = pin_pagina(&btree->pool, btree->arquivo_db, id_proximo);
        
        EnterCriticalSection(&f_proximo->mutex_frame); 
        LeaveCriticalSection(&f_atual->mutex_frame);
        unpin_pagina(&btree->pool, id_atual, false);

        id_atual = id_proximo;
        f_atual = f_proximo;
    }
    return resultado;
}

static void dividir_no_filho(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int32_t indice_filho_no_pai, PaginaBTree* pai) {
    int64_t id_filho_cheio = pai->deslocamentos_filhos[indice_filho_no_pai];
    FrameBuffer* f_filho = pin_pagina(pool, arquivo, id_filho_cheio);
    EnterCriticalSection(&f_filho->mutex_frame);

    int64_t id_novo_irmao = alocar_pagina(arquivo, sb);
    FrameBuffer* f_novo = pin_pagina(pool, arquivo, id_novo_irmao);
    EnterCriticalSection(&f_novo->mutex_frame);
    
    memset(&f_novo->pagina, 0, sizeof(PaginaBTree));
    f_novo->pagina.cabecalho.eh_folha = f_filho->pagina.cabecalho.eh_folha;

    int32_t indice_mediana = (M - 1) / 2;
    f_novo->pagina.cabecalho.qtd_chaves = (M - 1) - 1 - indice_mediana;

    for (int32_t j = 0; j < f_novo->pagina.cabecalho.qtd_chaves; j++) {
        f_novo->pagina.chaves[j] = f_filho->pagina.chaves[j + indice_mediana + 1];
        f_novo->pagina.deslocamentos_registros[j] = f_filho->pagina.deslocamentos_registros[j + indice_mediana + 1];
    }

    if (!f_filho->pagina.cabecalho.eh_folha) {
        for (int32_t j = 0; j <= f_novo->pagina.cabecalho.qtd_chaves; j++) {
            f_novo->pagina.deslocamentos_filhos[j] = f_filho->pagina.deslocamentos_filhos[j + indice_mediana + 1];
        }
    }

    f_filho->pagina.cabecalho.qtd_chaves = indice_mediana;

    for (int32_t j = pai->cabecalho.qtd_chaves; j >= indice_filho_no_pai + 1; j--) {
        pai->deslocamentos_filhos[j + 1] = pai->deslocamentos_filhos[j];
    }
    pai->deslocamentos_filhos[indice_filho_no_pai + 1] = id_novo_irmao;

    for (int32_t j = pai->cabecalho.qtd_chaves - 1; j >= indice_filho_no_pai; j--) {
        pai->chaves[j + 1] = pai->chaves[j];
        pai->deslocamentos_registros[j + 1] = pai->deslocamentos_registros[j];
    }

    pai->chaves[indice_filho_no_pai] = f_filho->pagina.chaves[indice_mediana];
    pai->deslocamentos_registros[indice_filho_no_pai] = f_filho->pagina.deslocamentos_registros[indice_mediana];
    pai->cabecalho.qtd_chaves++;

    LeaveCriticalSection(&f_novo->mutex_frame);
    LeaveCriticalSection(&f_filho->mutex_frame);
    unpin_pagina(pool, id_filho_cheio, true);
    unpin_pagina(pool, id_novo_irmao, true);
}

static void inserir_em_no_com_espaco(FILE* arquivo, BufferPool* pool, SuperBloco* sb, PaginaBTree* no, int32_t matricula, deslocamento_disco_t offset) {
    int32_t i = no->cabecalho.qtd_chaves - 1;
    if (no->cabecalho.eh_folha) {
        while (i >= 0 && no->chaves[i] > matricula) {
            no->chaves[i + 1] = no->chaves[i];
            no->deslocamentos_registros[i + 1] = no->deslocamentos_registros[i];
            i--;
        }
        no->chaves[i + 1] = matricula;
        no->deslocamentos_registros[i + 1] = offset;
        no->cabecalho.qtd_chaves++;
    } else {
        while (i >= 0 && no->chaves[i] > matricula) i--;
        i++;

        int64_t id_filho = no->deslocamentos_filhos[i];
        FrameBuffer* f_filho = pin_pagina(pool, arquivo, id_filho);
        EnterCriticalSection(&f_filho->mutex_frame);

        if (f_filho->pagina.cabecalho.qtd_chaves == (M - 1)) {
            dividir_no_filho(arquivo, pool, sb, i, no);
            LeaveCriticalSection(&f_filho->mutex_frame);
            unpin_pagina(pool, id_filho, true);

            if (no->chaves[i] < matricula) i++;
            id_filho = no->deslocamentos_filhos[i];
            f_filho = pin_pagina(pool, arquivo, id_filho);
            EnterCriticalSection(&f_filho->mutex_frame);
        }
        inserir_em_no_com_espaco(arquivo, pool, sb, &f_filho->pagina, matricula, offset);
        LeaveCriticalSection(&f_filho->mutex_frame);
        unpin_pagina(pool, id_filho, true);
    }
}

bool inserir_chave(GerenciadorBTree* btree, int32_t chave_matricula, deslocamento_disco_t deslocamento_registro) {
    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) { 
        int64_t id_raiz = alocar_pagina(btree->arquivo_db, &btree->superbloco);
        FrameBuffer* f_raiz = pin_pagina(&btree->pool, btree->arquivo_db, id_raiz);
        EnterCriticalSection(&f_raiz->mutex_frame);
        
        memset(&f_raiz->pagina, 0, sizeof(PaginaBTree));
        f_raiz->pagina.cabecalho.eh_folha = 1;
        f_raiz->pagina.cabecalho.qtd_chaves = 1;
        f_raiz->pagina.chaves[0] = chave_matricula;
        f_raiz->pagina.deslocamentos_registros[0] = deslocamento_registro;
        
        LeaveCriticalSection(&f_raiz->mutex_frame);
        unpin_pagina(&btree->pool, id_raiz, true);

        btree->superbloco.id_pagina_raiz = id_raiz;
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);
        return true;
    }

    int64_t id_raiz = btree->superbloco.id_pagina_raiz;
    FrameBuffer* f_raiz = pin_pagina(&btree->pool, btree->arquivo_db, id_raiz);
    EnterCriticalSection(&f_raiz->mutex_frame);

    if (f_raiz->pagina.cabecalho.qtd_chaves == (M - 1)) {
        int64_t id_nova_raiz = alocar_pagina(btree->arquivo_db, &btree->superbloco);
        FrameBuffer* f_nova = pin_pagina(&btree->pool, btree->arquivo_db, id_nova_raiz);
        EnterCriticalSection(&f_nova->mutex_frame);

        memset(&f_nova->pagina, 0, sizeof(PaginaBTree));
        f_nova->pagina.cabecalho.eh_folha = 0;
        f_nova->pagina.cabecalho.qtd_chaves = 0;
        f_nova->pagina.deslocamentos_filhos[0] = id_raiz;

        btree->superbloco.id_pagina_raiz = id_nova_raiz;
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        dividir_no_filho(btree->arquivo_db, &btree->pool, &btree->superbloco, 0, &f_nova->pagina);

        LeaveCriticalSection(&f_raiz->mutex_frame);
        unpin_pagina(&btree->pool, id_raiz, true);

        int32_t i = (f_nova->pagina.chaves[0] < chave_matricula) ? 1 : 0;
        int64_t id_filho_destino = f_nova->pagina.deslocamentos_filhos[i];
        FrameBuffer* f_destino = pin_pagina(&btree->pool, btree->arquivo_db, id_filho_destino);
        EnterCriticalSection(&f_destino->mutex_frame);

        LeaveCriticalSection(&f_nova->mutex_frame);
        unpin_pagina(&btree->pool, id_nova_raiz, true);

        inserir_em_no_com_espaco(btree->arquivo_db, &btree->pool, &btree->superbloco, &f_destino->pagina, chave_matricula, deslocamento_registro);
        LeaveCriticalSection(&f_destino->mutex_frame);
        unpin_pagina(&btree->pool, id_filho_destino, true);
    } else {
        inserir_em_no_com_espaco(btree->arquivo_db, &btree->pool, &btree->superbloco, &f_raiz->pagina, chave_matricula, deslocamento_registro);
        LeaveCriticalSection(&f_raiz->mutex_frame);
        unpin_pagina(&btree->pool, id_raiz, true);
    }

    return true;
}

int32_t busca_por_intervalo(GerenciadorBTree* btree, int32_t inicio, int32_t fim, int32_t* chaves_out) {
    int32_t total = 0;
    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) return 0;
    
    int64_t id_raiz = btree->superbloco.id_pagina_raiz;
    FrameBuffer* f = pin_pagina(&btree->pool, btree->arquivo_db, id_raiz);
    EnterCriticalSection(&f->mutex_frame);
    
    for (int32_t i = 0; i < f->pagina.cabecalho.qtd_chaves; i++) {
        if (f->pagina.chaves[i] >= inicio && f->pagina.chaves[i] <= fim) {
            chaves_out[total++] = f->pagina.chaves[i];
        }
    }
    
    LeaveCriticalSection(&f->mutex_frame);
    unpin_pagina(&btree->pool, id_raiz, false);
    return total;
}

bool remover_chave(GerenciadorBTree* btree, int32_t chave_matricula) {
    ResultadoBusca b = buscar_chave(btree, chave_matricula);
    if (!b.encontrada) return false;
    // Stub padronizado para as proximas fases
    return true; 
}

void exportar_recursivo(GerenciadorBTree* btree, int64_t id_pagina, FILE* csv) {
    if (id_pagina == DESLOCAMENTO_NULO) return;

    FrameBuffer* f = pin_pagina(&btree->pool, btree->arquivo_db, id_pagina);
    EnterCriticalSection(&f->mutex_frame);
    PaginaBTree* p = &f->pagina;

    if (!p->cabecalho.eh_folha) {
        exportar_recursivo(btree, p->deslocamentos_filhos[0], csv);
    }

    for (int32_t i = 0; i < p->cabecalho.qtd_chaves; i++) {
        fprintf(csv, "%d,%lld\n", p->chaves[i], p->deslocamentos_registros[i]);
        
        if (!p->cabecalho.eh_folha) {
            exportar_recursivo(btree, p->deslocamentos_filhos[i + 1], csv);
        }
    }

    LeaveCriticalSection(&f->mutex_frame);
    unpin_pagina(&btree->pool, id_pagina, false);
}

void exportar_btree_para_csv(GerenciadorBTree* btree, const char* nome_arquivo) {
    FILE* csv = fopen(nome_arquivo, "w");
    if (!csv) {
        printf("[ERRO] Nao foi possivel criar o arquivo CSV.\n");
        return;
    }

    fprintf(csv, "chave,deslocamento_disco\n");

    if (btree->superbloco.id_pagina_raiz != DESLOCAMENTO_NULO) {
        exportar_recursivo(btree, btree->superbloco.id_pagina_raiz, csv);
    }

    fclose(csv);
    printf("[SUCESSO] Dados exportados para '%s'.\n", nome_arquivo);
}