#include "btree.h"
#include <string.h>

/**
 * Inicializa a estrutura da Árvore B e a Seção Crítica do Windows.
 */
void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo) {
    btree->arquivo_db = arquivo;
    
    // Inicializa o trinco de concorrência do Windows
    InitializeCriticalSection(&btree->mutex_banco);
    
    // Protege a leitura inicial do superbloco
    EnterCriticalSection(&btree->mutex_banco);
    ler_superbloco(arquivo, &btree->superbloco);
    LeaveCriticalSection(&btree->mutex_banco);
}

/**
 * Libera os recursos de sincronização alocados pelo Windows.
 */
void destruir_btree(GerenciadorBTree* btree) {
    DeleteCriticalSection(&btree->mutex_banco);
}

/**
 * Função auxiliar: Realiza busca binária nas chaves carregadas no buffer de RAM.
 */
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

/**
 * Busca uma matrícula navegando dinamicamente pelo disco de forma thread-safe.
 */
ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula) {
    ResultadoBusca resultado;
    memset(&resultado, 0, sizeof(ResultadoBusca));
    resultado.deslocamento_registro = DESLOCAMENTO_NULO;
    resultado.id_pagina_onde_esta = DESLOCAMENTO_NULO;
    resultado.encontrada = false;

    // Tranca o acesso ao arquivo binário para outras threads
    EnterCriticalSection(&btree->mutex_banco);

    // Se a árvore estiver vazia (sem nó raiz)
    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) {
        LeaveCriticalSection(&btree->mutex_banco);
        return resultado;
    }

    int64_t id_pagina_atual = btree->superbloco.id_pagina_raiz;
    PaginaBTree pagina_ram; // Nosso cache temporário em RAM para varredura de I/O

    while (id_pagina_atual != DESLOCAMENTO_NULO) {
        // Leitura física através da camada storage
        if (!ler_pagina(btree->arquivo_db, id_pagina_atual, &pagina_ram)) {
            break; 
        }
        resultado.paginas_lidas++; // Incrementa o contador exigido pela instrumentação do edital

        int32_t indice = 0;
        bool achou = busca_binaria_na_pagina(&pagina_ram, chave_matricula, &indice);

        if (achou) {
            resultado.encontrada = true;
            resultado.id_pagina_onde_esta = id_pagina_atual;
            resultado.indice_na_pagina = indice;
            resultado.deslocamento_registro = pagina_ram.deslocamentos_registros[indice];
            break; // Chave localizada
        }

        if (pagina_ram.cabecalho.eh_folha) {
            break; // Fim da linha, chave não existe
        }

        // Avança para o ID do nó filho determinado pela busca binária
        id_pagina_atual = pagina_ram.deslocamentos_filhos[indice];
    }

    // Libera o banco de dados para a próxima thread operar
    LeaveCriticalSection(&btree->mutex_banco);
    return resultado;
}