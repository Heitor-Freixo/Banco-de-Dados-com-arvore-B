#include "btree.h"
#include <string.h>
#include <stdlib.h>

// Protótipos das funções estáticas internas corrigidas e limpas
static bool busca_binaria_na_pagina(const PaginaBTree* pagina, int32_t alvo, int32_t* indice);
static void dividir_no_filho(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int32_t indice_filho_no_pai, PaginaBTree* pai);
static void inserir_em_no_com_espaco(FILE* arquivo, BufferPool* pool, SuperBloco* sb, PaginaBTree* no, int32_t matricula, deslocamento_disco_t offset);
static int32_t obter_antecessor(FILE* arquivo, BufferPool* pool, int64_t id_no, deslocamento_disco_t* offset_out);
static int32_t obter_sucessor(FILE* arquivo, BufferPool* pool, int64_t id_no, deslocamento_disco_t* offset_out);
static void pegar_emprestado_esquerda(int32_t idx_filho, PaginaBTree* pai, PaginaBTree* filho, PaginaBTree* irmao_esq);
static void pegar_emprestado_direita(int32_t idx_filho, PaginaBTree* pai, PaginaBTree* filho, PaginaBTree* irmao_dir);
static void fundir_nos_irmaos(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int32_t idx_filho, PaginaBTree* pai);
static bool remover_no_preventivo(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int64_t id_no, int32_t matricula);

// Inicializa a árvore e o pool de buffers acoplado
void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo) {
    btree->arquivo_db = arquivo;
    InitializeCriticalSection(&btree->mutex_banco);
    
    EnterCriticalSection(&btree->mutex_banco);
    inicializar_buffer_pool(&btree->pool);
    ler_superbloco(arquivo, &btree->superbloco);
    LeaveCriticalSection(&btree->mutex_banco);
}

// Descarrega as páginas modificadas na RAM para o disco e limpa travas
void destruir_btree(GerenciadorBTree* btree) {
    EnterCriticalSection(&btree->mutex_banco);
    destruir_buffer_pool(&btree->pool, btree->arquivo_db);
    LeaveCriticalSection(&btree->mutex_banco);
    DeleteCriticalSection(&btree->mutex_banco);
}

// Busca binária interna em uma página carregada na RAM
static bool busca_binaria_na_pagina(const PaginaBTree* pagina, int32_t alvo, int32_t* indice) {
    int32_t inicio = 0;
    int32_t fim = pagina->cabecalho.qtd_chaves - 1;
    
    while (inicio <= fim) {
        int32_t meio = inicio + (fim - inicio) / 2;
        if (pagina->chaves[meio] == alvo) {
            *indice = meio;
            return true;
        }
        if (pagina->chaves[meio] < alvo) {
            inicio = meio + 1;
        } else {
            fim = meio - 1;
        }
    }
    *indice = inicio;
    return false;
}

// Busca pública que passa obrigatoriamente pelo cache LRU antes do I/O físico
ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula) {
    ResultadoBusca resultado;
    memset(&resultado, 0, sizeof(ResultadoBusca));
    resultado.deslocamento_registro = DESLOCAMENTO_NULO;
    resultado.id_pagina_onde_esta = DESLOCAMENTO_NULO;
    resultado.encontrada = false;

    EnterCriticalSection(&btree->mutex_banco);

    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) {
        LeaveCriticalSection(&btree->mutex_banco);
        return resultado;
    }

    int64_t id_pagina_atual = btree->superbloco.id_pagina_raiz;

    while (id_pagina_atual != DESLOCAMENTO_NULO) {
        FrameBuffer* f = pin_pagina(&btree->pool, btree->arquivo_db, id_pagina_atual);
        resultado.paginas_lidas++;

        int32_t indice = 0;
        bool achou = busca_binaria_na_pagina(&f->pagina, chave_matricula, &indice);

        if (achou) {
            resultado.encontrada = true;
            resultado.id_pagina_onde_esta = id_pagina_atual;
            resultado.indice_na_pagina = indice;
            resultado.deslocamento_registro = f->pagina.deslocamentos_registros[indice];
            unpin_pagina(&btree->pool, id_pagina_atual, false);
            break; 
        }

        bool eh_folha = f->pagina.cabecalho.eh_folha;
        int64_t proximo_id = eh_folha ? DESLOCAMENTO_NULO : f->pagina.deslocamentos_filhos[indice];

        unpin_pagina(&btree->pool, id_pagina_atual, false);
        id_pagina_atual = proximo_id;
    }

    LeaveCriticalSection(&btree->mutex_banco);
    return resultado;
}

// Divide um nó filho cheio gerenciando pin/unpin das páginas afetadas no cache
static void dividir_no_filho(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int32_t indice_filho_no_pai, PaginaBTree* pai) {
    int64_t id_filho_cheio = pai->deslocamentos_filhos[indice_filho_no_pai];
    
    FrameBuffer* f_filho = pin_pagina(pool, arquivo, id_filho_cheio);

    int64_t id_novo_irmao = alocar_pagina(arquivo, sb);
    FrameBuffer* f_novo = pin_pagina(pool, arquivo, id_novo_irmao);
    memset(&f_novo->pagina, 0, sizeof(PaginaBTree));
    
    f_novo->pagina.cabecalho.eh_folha = f_filho->pagina.cabecalho.eh_folha;

    int32_t indice_mediana = ((M - 1) / 2); 
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

    unpin_pagina(pool, id_filho_cheio, true);
    unpin_pagina(pool, id_novo_irmao, true);
}

// Insere a chave recursivamente descendo pelas páginas mapeadas no Buffer Pool
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
        while (i >= 0 && no->chaves[i] > matricula) {
            i--;
        }
        i++;

        int64_t id_filho = no->deslocamentos_filhos[i];
        FrameBuffer* f_filho = pin_pagina(pool, arquivo, id_filho);

        if (f_filho->pagina.cabecalho.qtd_chaves == (M - 1)) {
            dividir_no_filho(arquivo, pool, sb, i, no);
            unpin_pagina(pool, id_filho, true); 
            
            if (no->chaves[i] < matricula) {
                i++;
                id_filho = no->deslocamentos_filhos[i];
            }
            f_filho = pin_pagina(pool, arquivo, id_filho);
        }
        
        inserir_em_no_com_espaco(arquivo, pool, sb, &f_filho->pagina, matricula, offset);
        unpin_pagina(pool, id_filho, true);
    }
}

// Ponto de entrada público para inserção estruturada de chaves
bool inserir_chave(GerenciadorBTree* btree, int32_t chave_matricula, deslocamento_disco_t deslocamento_registro) {
    EnterCriticalSection(&btree->mutex_banco);

    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) {
        int64_t id_raiz = alocar_pagina(btree->arquivo_db, &btree->superbloco);
        
        FrameBuffer* f_raiz = pin_pagina(&btree->pool, btree->arquivo_db, id_raiz);
        memset(&f_raiz->pagina, 0, sizeof(PaginaBTree));
        f_raiz->pagina.cabecalho.eh_folha = 1;
        f_raiz->pagina.cabecalho.qtd_chaves = 1;
        f_raiz->pagina.chaves[0] = chave_matricula;
        f_raiz->pagina.deslocamentos_registros[0] = deslocamento_registro;

        unpin_pagina(&btree->pool, id_raiz, true);

        btree->superbloco.id_pagina_raiz = id_raiz;
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        LeaveCriticalSection(&btree->mutex_banco);
        return true;
    }

    int64_t id_raiz_atual = btree->superbloco.id_pagina_raiz;
    FrameBuffer* f_raiz = pin_pagina(&btree->pool, btree->arquivo_db, id_raiz_atual);

    if (f_raiz->pagina.cabecalho.qtd_chaves == (M - 1)) {
        int64_t id_nova_raiz = alocar_pagina(btree->arquivo_db, &btree->superbloco);
        FrameBuffer* f_nova = pin_pagina(&btree->pool, btree->arquivo_db, id_nova_raiz);
        memset(&f_nova->pagina, 0, sizeof(PaginaBTree));
        f_nova->pagina.cabecalho.eh_folha = 0;
        f_nova->pagina.cabecalho.qtd_chaves = 0;
        f_nova->pagina.deslocamentos_filhos[0] = id_raiz_atual;

        btree->superbloco.id_pagina_raiz = id_nova_raiz;
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        unpin_pagina(&btree->pool, id_raiz_atual, false); 

        dividir_no_filho(btree->arquivo_db, &btree->pool, &btree->superbloco, 0, &f_nova->pagina);

        int32_t i = 0;
        if (f_nova->pagina.chaves[0] < chave_matricula) {
            i++;
        }
        
        int64_t id_filho_destino = f_nova->pagina.deslocamentos_filhos[i];
        FrameBuffer* f_destino = pin_pagina(&btree->pool, btree->arquivo_db, id_filho_destino);

        inserir_em_no_com_espaco(btree->arquivo_db, &btree->pool, &btree->superbloco, &f_destino->pagina, chave_matricula, deslocamento_registro);
        
        unpin_pagina(&btree->pool, id_filho_destino, true);
        unpin_pagina(&btree->pool, id_nova_raiz, true);
    } else {
        inserir_em_no_com_espaco(btree->arquivo_db, &btree->pool, &btree->superbloco, &f_raiz->pagina, chave_matricula, deslocamento_registro);
        unpin_pagina(&btree->pool, id_raiz_atual, true);
    }

    LeaveCriticalSection(&btree->mutex_banco);
    return true;
}

// Obtém o elemento mais à direita da subárvore esquerda
static int32_t obter_antecessor(FILE* arquivo, BufferPool* pool, int64_t id_no, deslocamento_disco_t* offset_out) {
    FrameBuffer* f = pin_pagina(pool, arquivo, id_no);
    int64_t id_atual = id_no;
    
    while (!f->pagina.cabecalho.eh_folha) {
        int64_t proximo = f->pagina.deslocamentos_filhos[f->pagina.cabecalho.qtd_chaves];
        unpin_pagina(pool, id_atual, false);
        id_atual = proximo;
        f = pin_pagina(pool, arquivo, id_atual);
    }
    
    *offset_out = f->pagina.deslocamentos_registros[f->pagina.cabecalho.qtd_chaves - 1];
    int32_t chave = f->pagina.chaves[f->pagina.cabecalho.qtd_chaves - 1];
    unpin_pagina(pool, id_atual, false);
    return chave;
}

// Obtém o elemento mais à esquerda da subárvore direita
static int32_t obter_sucessor(FILE* arquivo, BufferPool* pool, int64_t id_no, deslocamento_disco_t* offset_out) {
    FrameBuffer* f = pin_pagina(pool, arquivo, id_no);
    int64_t id_atual = id_no;
    
    while (!f->pagina.cabecalho.eh_folha) {
        int64_t proximo = f->pagina.deslocamentos_filhos[0];
        unpin_pagina(pool, id_atual, false);
        id_atual = proximo;
        f = pin_pagina(pool, arquivo, id_atual);
    }
    
    *offset_out = f->pagina.deslocamentos_registros[0];
    int32_t chave = f->pagina.chaves[0];
    unpin_pagina(pool, id_atual, false);
    return chave;
}

// Move elemento do irmão esquerdo para o filho sob subfluxo
static void pegar_emprestado_esquerda(int32_t idx_filho, PaginaBTree* pai, PaginaBTree* filho, PaginaBTree* irmao_esq) {
    for (int32_t i = filho->cabecalho.qtd_chaves - 1; i >= 0; i--) {
        filho->chaves[i + 1] = filho->chaves[i];
        filho->deslocamentos_registros[i + 1] = filho->deslocamentos_registros[i];
    }
    if (!filho->cabecalho.eh_folha) {
        for (int32_t i = filho->cabecalho.qtd_chaves; i >= 0; i--) {
            filho->deslocamentos_filhos[i + 1] = filho->deslocamentos_filhos[i];
        }
        filho->deslocamentos_filhos[0] = irmao_esq->deslocamentos_filhos[irmao_esq->cabecalho.qtd_chaves];
    }

    filho->chaves[0] = pai->chaves[idx_filho - 1];
    filho->deslocamentos_registros[0] = pai->deslocamentos_registros[idx_filho - 1];
    filho->cabecalho.qtd_chaves++;

    pai->chaves[idx_filho - 1] = irmao_esq->chaves[irmao_esq->cabecalho.qtd_chaves - 1];
    pai->deslocamentos_registros[idx_filho - 1] = irmao_esq->deslocamentos_registros[irmao_esq->cabecalho.qtd_chaves - 1];
    
    irmao_esq->cabecalho.qtd_chaves--;
}

// Move elemento do irmão direito para o filho sob subfluxo
static void pegar_emprestado_direita(int32_t idx_filho, PaginaBTree* pai, PaginaBTree* filho, PaginaBTree* irmao_dir) {
    filho->chaves[filho->cabecalho.qtd_chaves] = pai->chaves[idx_filho];
    filho->deslocamentos_registros[filho->cabecalho.qtd_chaves] = pai->deslocamentos_registros[idx_filho];
    
    if (!filho->cabecalho.eh_folha) {
        filho->deslocamentos_filhos[filho->cabecalho.qtd_chaves + 1] = irmao_dir->deslocamentos_filhos[0];
    }
    filho->cabecalho.qtd_chaves++;

    pai->chaves[idx_filho] = irmao_dir->chaves[0];
    pai->deslocamentos_registros[idx_filho] = irmao_dir->deslocamentos_registros[0];

    irmao_dir->cabecalho.qtd_chaves--;
    for (int32_t i = 0; i < irmao_dir->cabecalho.qtd_chaves; i++) {
        irmao_dir->chaves[i] = irmao_dir->chaves[i + 1];
        irmao_dir->deslocamentos_registros[i] = irmao_dir->deslocamentos_registros[i + 1];
    }
    if (!irmao_dir->cabecalho.eh_folha) {
        for (int32_t i = 0; i <= irmao_dir->cabecalho.qtd_chaves; i++) {
            irmao_dir->deslocamentos_filhos[i] = irmao_dir->deslocamentos_filhos[i + 1];
        }
    }
}

// Executa a fusão física e lógica de duas páginas filhas irmãs
static void fundir_nos_irmaos(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int32_t idx_filho, PaginaBTree* pai) {
    int64_t id_filho = pai->deslocamentos_filhos[idx_filho];
    int64_t id_irmao = pai->deslocamentos_filhos[idx_filho + 1];

    FrameBuffer* f_filho = pin_pagina(pool, arquivo, id_filho);
    FrameBuffer* f_irmao = pin_pagina(pool, arquivo, id_irmao);

    f_filho->pagina.chaves[f_filho->pagina.cabecalho.qtd_chaves] = pai->chaves[idx_filho];
    f_filho->pagina.deslocamentos_registros[f_filho->pagina.cabecalho.qtd_chaves] = pai->deslocamentos_registros[idx_filho];
    f_filho->pagina.cabecalho.qtd_chaves++;

    for (int32_t i = 0; i < f_irmao->pagina.cabecalho.qtd_chaves; i++) {
        f_filho->pagina.chaves[f_filho->pagina.cabecalho.qtd_chaves] = f_irmao->pagina.chaves[i];
        f_filho->pagina.deslocamentos_registros[f_filho->pagina.cabecalho.qtd_chaves] = f_irmao->pagina.deslocamentos_registros[i];
        f_filho->pagina.cabecalho.qtd_chaves++;
    }
    if (!f_filho->pagina.cabecalho.eh_folha) {
        int32_t idx_ponteiro_inicial = f_filho->pagina.cabecalho.qtd_chaves - f_irmao->pagina.cabecalho.qtd_chaves;
        for (int32_t i = 0; i <= f_irmao->pagina.cabecalho.qtd_chaves; i++) {
            f_filho->pagina.deslocamentos_filhos[idx_ponteiro_inicial + i] = f_irmao->pagina.deslocamentos_filhos[i];
        }
    }

    for (int32_t i = idx_filho; i < pai->cabecalho.qtd_chaves - 1; i++) {
        pai->chaves[i] = pai->chaves[i + 1];
        pai->deslocamentos_registros[i] = pai->deslocamentos_registros[i + 1];
    }
    for (int32_t i = idx_filho + 1; i < pai->cabecalho.qtd_chaves; i++) {
        pai->deslocamentos_filhos[i] = pai->deslocamentos_filhos[i + 1];
    }
    pai->cabecalho.qtd_chaves--;

    unpin_pagina(pool, id_filho, true);
    unpin_pagina(pool, id_irmao, true);

    liberar_id_pagina(sb, id_irmao);
}

// Remoção preventiva top-down operando sobre as trancas do cache LRU
static bool remover_no_preventivo(FILE* arquivo, BufferPool* pool, SuperBloco* sb, int64_t id_no, int32_t matricula) {
    FrameBuffer* f_atual = pin_pagina(pool, arquivo, id_no);
    PaginaBTree* no = &f_atual->pagina;

    int32_t idx = 0;
    while (idx < no->cabecalho.qtd_chaves && no->chaves[idx] < matricula) {
        idx++;
    }

    if (idx < no->cabecalho.qtd_chaves && no->chaves[idx] == matricula) {
        if (no->cabecalho.eh_folha) {
            for (int32_t i = idx; i < no->cabecalho.qtd_chaves - 1; i++) {
                no->chaves[i] = no->chaves[i + 1];
                no->deslocamentos_registros[i] = no->deslocamentos_registros[i + 1];
            }
            no->cabecalho.qtd_chaves--;
            unpin_pagina(pool, id_no, true);
            return true;
        } else {
            int64_t id_filho_esq = no->deslocamentos_filhos[idx];
            FrameBuffer* f_esq = pin_pagina(pool, arquivo, id_filho_esq);

            if (f_esq->pagina.cabecalho.qtd_chaves > MIN_CHAVES) {
                deslocamento_disco_t offset_ant;
                unpin_pagina(pool, id_filho_esq, false);
                int32_t antecessor = obter_antecessor(arquivo, pool, id_filho_esq, &offset_ant);
                no->chaves[idx] = antecessor;
                no->deslocamentos_registros[idx] = offset_ant;
                unpin_pagina(pool, id_no, true);
                return remover_no_preventivo(arquivo, pool, sb, id_filho_esq, antecessor);
            }
            unpin_pagina(pool, id_filho_esq, false);

            int64_t id_filho_dir = no->deslocamentos_filhos[idx + 1];
            FrameBuffer* f_dir = pin_pagina(pool, arquivo, id_filho_dir);

            if (f_dir->pagina.cabecalho.qtd_chaves > MIN_CHAVES) {
                deslocamento_disco_t offset_suc;
                unpin_pagina(pool, id_filho_dir, false);
                int32_t sucessor = obter_sucessor(arquivo, pool, id_filho_dir, &offset_suc);
                no->chaves[idx] = sucessor;
                no->deslocamentos_registros[idx] = offset_suc;
                unpin_pagina(pool, id_no, true);
                return remover_no_preventivo(arquivo, pool, sb, id_filho_dir, sucessor);
            }
            unpin_pagina(pool, id_filho_dir, false);

            fundir_nos_irmaos(arquivo, pool, sb, idx, no);
            unpin_pagina(pool, id_no, true);
            return remover_no_preventivo(arquivo, pool, sb, id_filho_esq, matricula);
        }
    }

    if (no->cabecalho.eh_folha) {
        unpin_pagina(pool, id_no, false);
        return false;
    }

    int64_t id_filho_alvo = no->deslocamentos_filhos[idx];
    FrameBuffer* f_alvo = pin_pagina(pool, arquivo, id_filho_alvo);

    if (f_alvo->pagina.cabecalho.qtd_chaves == MIN_CHAVES) {
        if (idx > 0) {
            FrameBuffer* f_esq = pin_pagina(pool, arquivo, no->deslocamentos_filhos[idx - 1]);
            if (f_esq->pagina.cabecalho.qtd_chaves > MIN_CHAVES) {
                pegar_emprestado_esquerda(idx, no, &f_alvo->pagina, &f_esq->pagina);
                unpin_pagina(pool, no->deslocamentos_filhos[idx - 1], true);
                unpin_pagina(pool, id_filho_alvo, true);
                unpin_pagina(pool, id_no, true);
                return remover_no_preventivo(arquivo, pool, sb, id_filho_alvo, matricula);
            }
            unpin_pagina(pool, no->deslocamentos_filhos[idx - 1], false);
        }
        if (idx < no->cabecalho.qtd_chaves) {
            FrameBuffer* f_dir = pin_pagina(pool, arquivo, no->deslocamentos_filhos[idx + 1]);
            if (f_dir->pagina.cabecalho.qtd_chaves > MIN_CHAVES) {
                pegar_emprestado_direita(idx, no, &f_alvo->pagina, &f_dir->pagina);
                unpin_pagina(pool, no->deslocamentos_filhos[idx + 1], true);
                unpin_pagina(pool, id_filho_alvo, true);
                unpin_pagina(pool, id_no, true);
                return remover_no_preventivo(arquivo, pool, sb, id_filho_alvo, matricula);
            }
            unpin_pagina(pool, no->deslocamentos_filhos[idx + 1], false);
        }
        
        unpin_pagina(pool, id_filho_alvo, false); 
        if (idx < no->cabecalho.qtd_chaves) {
            fundir_nos_irmaos(arquivo, pool, sb, idx, no);
        } else {
            fundir_nos_irmaos(arquivo, pool, sb, idx - 1, no);
            id_filho_alvo = no->deslocamentos_filhos[idx - 1];
        }
        unpin_pagina(pool, id_no, true);
        return remover_no_preventivo(arquivo, pool, sb, id_filho_alvo, matricula);
    }

    unpin_pagina(pool, id_filho_alvo, false);
    unpin_pagina(pool, id_no, false);
    return remover_no_preventivo(arquivo, pool, sb, id_filho_alvo, matricula);
}

// Entrada pública da rotina de deleção
bool remover_chave(GerenciadorBTree* btree, int32_t chave_matricula) {
    EnterCriticalSection(&btree->mutex_banco);

    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) {
        LeaveCriticalSection(&btree->mutex_banco);
        return false;
    }

    int64_t id_raiz = btree->superbloco.id_pagina_raiz;
    bool status = remover_no_preventivo(btree->arquivo_db, &btree->pool, &btree->superbloco, id_raiz, chave_matricula);

    FrameBuffer* f_raiz = pin_pagina(&btree->pool, btree->arquivo_db, id_raiz);
    if (f_raiz->pagina.cabecalho.qtd_chaves == 0 && !f_raiz->pagina.cabecalho.eh_folha) {
        btree->superbloco.id_pagina_raiz = f_raiz->pagina.deslocamentos_filhos[0];
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        unpin_pagina(&btree->pool, id_raiz, false);
        liberar_id_pagina(&btree->superbloco, id_raiz);
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);
    } else {
        unpin_pagina(&btree->pool, id_raiz, false);
    }

    LeaveCriticalSection(&btree->mutex_banco);
    return status;
}