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
 * Libera os recursos de sincronização alocados pelo Windows ao encerrar o SGBD.
 */
void destruir_btree(GerenciadorBTree* btree) {
    DeleteCriticalSection(&btree->mutex_banco);
}

/**
 * Função auxiliar: Realiza busca binária nas chaves carregadas no buffer de RAM.
 * Retorna true se encontrou a chave, e define em *indice o local exato (ou onde ela deveria estar).
 */
static bool busca_binaria_na_pagina(const PaginaBTree* pagina, int32_t alvo, int32_t* indice) {
    int32_t inicio = 0;
    int32_t fim = pagina->cabecalho.qtd_chaves - 1;
    
    while (inicio <= fim) {
        int32_t meio = inicio + (fim - inicio) / 2;
        
        if (pagina->chaves[meio] == alvo) {
            *indice = meio;
            return true; // Chave localizada
        }
        
        if (pagina->chaves[meio] < alvo) {
            inicio = meio + 1;
        } else {
            fim = meio - 1;
        }
    }
    
    *indice = inicio; // Ponto de inserção ou descida
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
    PaginaBTree pagina_ram; // Cache temporário em RAM para varredura de I/O

    while (id_pagina_atual != DESLOCAMENTO_NULO) {
        // Leitura física através da camada storage
        if (!ler_pagina(btree->arquivo_db, id_pagina_atual, &pagina_ram)) {
            break; 
        }
        resultado.paginas_lidas++; // Incrementa contador para relatório do edital

        int32_t indice = 0;
        bool achou = busca_binaria_na_pagina(&pagina_ram, chave_matricula, &indice);

        if (achou) {
            resultado.encontrada = true;
            resultado.id_pagina_onde_esta = id_pagina_atual;
            resultado.indice_na_pagina = indice;
            resultado.deslocamento_registro = pagina_ram.deslocamentos_registros[indice];
            break; 
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

/**
 * Função Auxiliar: Divide um nó filho que está completamente cheio.
 * O nó pai (id_pai) DEVE ter espaço em RAM e em disco para receber a chave mediana.
 */
static void dividir_no_filho(FILE* arquivo, SuperBloco* sb, int64_t id_pai, int32_t indice_filho_no_pai, PaginaBTree* pai) {
    int64_t id_filho_cheio = pai->deslocamentos_filhos[indice_filho_no_pai];
    
    PaginaBTree filho_cheio;
    ler_pagina(arquivo, id_filho_cheio, &filho_cheio);

    // Aloca uma nova página em disco usando o gerenciador de espaço do seu Superbloco
    int64_t id_novo_irmao = alocar_pagina(arquivo, sb);
    PaginaBTree novo_irmao;
    memset(&novo_irmao, 0, sizeof(PaginaBTree));
    
    // O novo irmão herda a propriedade de folha do filho original
    novo_irmao.cabecalho.eh_folha = filho_cheio.cabecalho.eh_folha;

    // Calculamos o meio exato com base nas definições do seu storage.h
    int32_t indice_mediana = ((M - 1) / 2); 
    novo_irmao.cabecalho.qtd_chaves = (M - 1) - 1 - indice_mediana; 

    // 1. Move as chaves e registros da metade superior para o novo irmão
    for (int32_t j = 0; j < novo_irmao.cabecalho.qtd_chaves; j++) {
        novo_irmao.chaves[j] = filho_cheio.chaves[j + indice_mediana + 1];
        novo_irmao.deslocamentos_registros[j] = filho_cheio.deslocamentos_registros[j + indice_mediana + 1];
    }

    // 2. Se não for folha, move também os ponteiros dos filhos correspondentes
    if (!filho_cheio.cabecalho.eh_folha) {
        for (int32_t j = 0; j <= novo_irmao.cabecalho.qtd_chaves; j++) {
            novo_irmao.deslocamentos_filhos[j] = filho_cheio.deslocamentos_filhos[j + indice_mediana + 1];
        }
    }

    // 3. Reduz o tamanho do filho original que ficou na esquerda
    filho_cheio.cabecalho.qtd_chaves = indice_mediana;

    // 4. Abre espaço no nó pai para empurrar o novo ponteiro do filho da direita
    for (int32_t j = pai->cabecalho.qtd_chaves; j >= indice_filho_no_pai + 1; j--) {
        pai->deslocamentos_filhos[j + 1] = pai->deslocamentos_filhos[j];
    }
    pai->deslocamentos_filhos[indice_filho_no_pai + 1] = id_novo_irmao;

    // 5. Abre espaço no nó pai para a chave mediana que está subindo
    for (int32_t j = pai->cabecalho.qtd_chaves - 1; j >= indice_filho_no_pai; j--) {
        pai->chaves[j + 1] = pai->chaves[j];
        pai->deslocamentos_registros[j + 1] = pai->deslocamentos_registros[j];
    }

    // 6. Copia a chave mediana e o deslocamento do registro para o pai
    pai->chaves[indice_filho_no_pai] = filho_cheio.chaves[indice_mediana];
    pai->deslocamentos_registros[indice_filho_no_pai] = filho_cheio.deslocamentos_registros[indice_mediana];
    pai->cabecalho.qtd_chaves++;

    // Salva todas as 3 páginas modificadas de volta no disco
    escrever_pagina(arquivo, id_filho_cheio, &filho_cheio);
    escrever_pagina(arquivo, id_novo_irmao, &novo_irmao);
    escrever_pagina(arquivo, id_pai, pai);
}

/**
 * Função Auxiliar: Insere uma chave em um nó que garantidamente possui espaço livre.
 */
static void inserir_em_no_com_espaco(FILE* arquivo, SuperBloco* sb, int64_t id_no, PaginaBTree* no, int32_t matricula, deslocamento_disco_t offset) {
    int32_t i = no->cabecalho.qtd_chaves - 1;

    if (no->cabecalho.eh_folha) {
        // Encontra a posição correta deslocando as chaves maiores para a direita
        while (i >= 0 && no->chaves[i] > matricula) {
            no->chaves[i + 1] = no->chaves[i];
            no->deslocamentos_registros[i + 1] = no->deslocamentos_registros[i];
            i--;
        }
        
        // Insere a nova chave na posição vaga
        no->chaves[i + 1] = matricula;
        no->deslocamentos_registros[i + 1] = offset;
        no->cabecalho.qtd_chaves++;
        
        // Salva a alteração da folha no arquivo binário
        escrever_pagina(arquivo, id_no, no);
    } else {
        // Descobre qual filho deve receber a chave
        while (i >= 0 && no->chaves[i] > matricula) {
            i--;
        }
        i++; // Índice do filho correspondente

        int64_t id_filho = no->deslocamentos_filhos[i];
        PaginaBTree filho;
        ler_pagina(arquivo, id_filho, &filho);

        // Se o nó filho estiver completamente cheio, divide-o preventivamente antes de descer
        if (filho.cabecalho.qtd_chaves == (M - 1)) {
            dividir_no_filho(arquivo, sb, id_no, i, no);
            
            // Após o split, a chave mediana subiu e o nó filho foi dividido em dois.
            if (no->chaves[i] < matricula) {
                i++;
                id_filho = no->deslocamentos_filhos[i];
                ler_pagina(arquivo, id_filho, &filho);
            }
        }
        
        inserir_em_no_com_espaco(arquivo, sb, id_filho, &filho, matricula, offset);
    }
}

/**
 * Função Principal: Insere uma matrícula e o deslocamento do seu registro na Árvore B.
 */
bool inserir_chave(GerenciadorBTree* btree, int32_t chave_matricula, deslocamento_disco_t deslocamento_registro) {
    EnterCriticalSection(&btree->mutex_banco);

    // CASO 1: Árvore totalmente vazia. Inicializa a primeira página raiz.
    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) {
        int64_t id_raiz = alocar_pagina(btree->arquivo_db, &btree->superbloco);
        
        PaginaBTree raiz;
        memset(&raiz, 0, sizeof(PaginaBTree));
        raiz.cabecalho.eh_folha = 1;
        raiz.cabecalho.qtd_chaves = 1;
        raiz.chaves[0] = chave_matricula;
        raiz.deslocamentos_registros[0] = deslocamento_registro;

        escrever_pagina(btree->arquivo_db, id_raiz, &raiz);

        // Atualiza e persiste o Superbloco com a localização da nova raiz
        btree->superbloco.id_pagina_raiz = id_raiz;
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        LeaveCriticalSection(&btree->mutex_banco);
        return true;
    }

    // Carrega a raiz atual da árvore
    int64_t id_raiz_atual = btree->superbloco.id_pagina_raiz;
    PaginaBTree raiz;
    ler_pagina(btree->arquivo_db, id_raiz_atual, &raiz);

    // CASO 2: A raiz atual está completamente cheia. A árvore vai crescer um nível para cima.
    if (raiz.cabecalho.qtd_chaves == (M - 1)) {
        int64_t id_nova_raiz = alocar_pagina(btree->arquivo_db, &btree->superbloco);
        
        PaginaBTree nova_raiz;
        memset(&nova_raiz, 0, sizeof(PaginaBTree));
        nova_raiz.cabecalho.eh_folha = 0; // Nova raiz nasce como nó interno
        nova_raiz.cabecalho.qtd_chaves = 0;
        nova_raiz.deslocamentos_filhos[0] = id_raiz_atual; // Aponta para a antiga raiz

        // Atualiza a estrutura de controle antes de realizar a divisão
        btree->superbloco.id_pagina_raiz = id_nova_raiz;
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        // Executa a divisão da antiga raiz a partir da nova raiz vazia
        dividir_no_filho(btree->arquivo_db, &btree->superbloco, id_nova_raiz, 0, &nova_raiz);

        // Decide em qual dos dois pedaços resultantes do split a chave deve entrar
        int32_t i = 0;
        if (nova_raiz.chaves[0] < chave_matricula) {
            i++;
        }
        
        int64_t id_filho_destino = nova_raiz.deslocamentos_filhos[i];
        PaginaBTree filho_destino;
        ler_pagina(btree->arquivo_db, id_filho_destino, &filho_destino);

        inserir_em_no_com_espaco(btree->arquivo_db, &btree->superbloco, id_filho_destino, &filho_destino, chave_matricula, deslocamento_registro);
    } else {
        // CASO 3: A raiz possui espaço livre. Desce normalmente na estrutura.
        inserir_em_no_com_espaco(btree->arquivo_db, &btree->superbloco, id_raiz_atual, &raiz, chave_matricula, deslocamento_registro);
    }

    LeaveCriticalSection(&btree->mutex_banco);
    return true;
}

/**
 * Função Auxiliar: Busca a maior chave (antecessor) na subárvore à esquerda.
 */
static int32_t obter_antecessor(FILE* arquivo, int64_t id_no, deslocamento_disco_t* offset_out) {
    PaginaBTree no;
    ler_pagina(arquivo, id_no, &no);
    
    while (!no.cabecalho.eh_folha) {
        id_no = no.deslocamentos_filhos[no.cabecalho.qtd_chaves];
        ler_pagina(arquivo, id_no, &no);
    }
    
    *offset_out = no.deslocamentos_registros[no.cabecalho.qtd_chaves - 1];
    return no.chaves[no.cabecalho.qtd_chaves - 1];
}

/**
 * Função Auxiliar: Busca a menor chave (sucessor) na subárvore à direita.
 */
static int32_t obter_sucessor(FILE* arquivo, int64_t id_no, deslocamento_disco_t* offset_out) {
    PaginaBTree no;
    ler_pagina(arquivo, id_no, &no);
    
    while (!no.cabecalho.eh_folha) {
        id_no = no.deslocamentos_filhos[0];
        ler_pagina(arquivo, id_no, &no);
    }
    
    *offset_out = no.deslocamentos_registros[0];
    return no.chaves[0];
}

/**
 * Função Auxiliar: Pega uma chave emprestada do irmão da ESQUERDA e rotaciona pelo pai.
 */
static void pegar_emprestado_esquerda(FILE* arquivo, int64_t id_pai, int32_t idx_filho, PaginaBTree* pai, PaginaBTree* filho, PaginaBTree* irmao_esq) {
    // Desloca todas as chaves e ponteiros do filho uma posição para a direita
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

    // O filho recebe a chave divisora que estava no pai
    filho->chaves[0] = pai->chaves[idx_filho - 1];
    filho->deslocamentos_registros[0] = pai->deslocamentos_registros[idx_filho - 1];
    filho->cabecalho.qtd_chaves++;

    // O pai recebe a última chave do irmão da esquerda
    pai->chaves[idx_filho - 1] = irmao_esq->chaves[irmao_esq->cabecalho.qtd_chaves - 1];
    pai->deslocamentos_registros[idx_filho - 1] = irmao_esq->deslocamentos_registros[irmao_esq->cabecalho.qtd_chaves - 1];
    
    irmao_esq->cabecalho.qtd_chaves--;

    // Persiste as três páginas afetadas
    escrever_pagina(arquivo, pai->deslocamentos_filhos[idx_filho], filho); 
    escrever_pagina(arquivo, pai->deslocamentos_filhos[idx_filho - 1], irmao_esq);
    escrever_pagina(arquivo, id_pai, pai);
}

/**
 * Função Auxiliar: Pega uma chave emprestada do irmão da DIREITA e rotaciona pelo pai.
 */
static void pegar_emprestado_direita(FILE* arquivo, int64_t id_pai, int32_t idx_filho, PaginaBTree* pai, PaginaBTree* filho, PaginaBTree* irmao_dir) {
    // O filho recebe a chave divisora do pai no final dos seus vetores
    filho->chaves[filho->cabecalho.qtd_chaves] = pai->chaves[idx_filho];
    filho->deslocamentos_registros[filho->cabecalho.qtd_chaves] = pai->deslocamentos_registros[idx_filho];
    
    if (!filho->cabecalho.eh_folha) {
        filho->deslocamentos_filhos[filho->cabecalho.qtd_chaves + 1] = irmao_dir->deslocamentos_filhos[0];
    }
    filho->cabecalho.qtd_chaves++;

    // O pai herda a primeira chave do irmão da direita
    pai->chaves[idx_filho] = irmao_dir->chaves[0];
    pai->deslocamentos_registros[idx_filho] = irmao_dir->deslocamentos_registros[0];

    // Desloca os elementos restantes do irmão da direita uma posição para a esquerda
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

    // Persiste as três páginas alteradas
    escrever_pagina(arquivo, pai->deslocamentos_filhos[idx_filho], filho);
    escrever_pagina(arquivo, pai->deslocamentos_filhos[idx_filho + 1], irmao_dir);
    escrever_pagina(arquivo, id_pai, pai);
}

/**
 * Função Auxiliar: Funde (Merge) o nó filho com seu irmão da direita e puxa uma chave do pai.
 * Utiliza o liberar_id_pagina nativo do seu storage.
 */
static void fundir_nos_irmaos(FILE* arquivo, SuperBloco* sb, int64_t id_pai, int32_t idx_filho, PaginaBTree* pai) {
    int64_t id_filho = pai->deslocamentos_filhos[idx_filho];
    int64_t id_irmao = pai->deslocamentos_filhos[idx_filho + 1];

    PaginaBTree filho, irmao;
    ler_pagina(arquivo, id_filho, &filho);
    ler_pagina(arquivo, id_irmao, &irmao);

    // 1. Puxa a chave divisora do pai para dentro do filho da esquerda
    filho.chaves[filho.cabecalho.qtd_chaves] = pai->chaves[idx_filho];
    filho.deslocamentos_registros[filho.cabecalho.qtd_chaves] = pai->deslocamentos_registros[idx_filho];
    filho.cabecalho.qtd_chaves++;

    // 2. Copia todas as chaves, registros e ponteiros do irmão da direita para o filho
    for (int32_t i = 0; i < irmao.cabecalho.qtd_chaves; i++) {
        filho.chaves[filho.cabecalho.qtd_chaves] = irmao.chaves[i];
        filho.deslocamentos_registros[filho.cabecalho.qtd_chaves] = irmao.deslocamentos_registros[i];
        filho.cabecalho.qtd_chaves++;
    }
    if (!filho.cabecalho.eh_folha) {
        int32_t idx_ponteiro_inicial = filho.cabecalho.qtd_chaves - irmao.cabecalho.qtd_chaves;
        for (int32_t i = 0; i <= irmao.cabecalho.qtd_chaves; i++) {
            filho.deslocamentos_filhos[idx_ponteiro_inicial + i] = irmao.deslocamentos_filhos[i];
        }
    }

    // 3. Remove a chave e o ponteiro do pai, reajustando os seus vetores internos
    for (int32_t i = idx_filho; i < pai->cabecalho.qtd_chaves - 1; i++) {
        pai->chaves[i] = pai->chaves[i + 1];
        pai->deslocamentos_registros[i] = pai->deslocamentos_registros[i + 1];
    }
    for (int32_t i = idx_filho + 1; i < pai->cabecalho.qtd_chaves; i++) {
        pai->deslocamentos_filhos[i] = pai->deslocamentos_filhos[i + 1];
    }
    pai->cabecalho.qtd_chaves--;

    // 4. Salva o nó esquerdo inflado e o pai reduzido
    escrever_pagina(arquivo, id_filho, &filho);
    escrever_pagina(arquivo, id_pai, pai);

    // 5. CORREÇÃO DA PILHA DE LIVRES: Usa a função oficial do seu storage.h
    liberar_id_pagina(sb, id_irmao);
    escrever_superbloco(arquivo, sb);
}

/**
 * Função Recursiva Interna: Navega de forma preventiva removendo a chave em disco.
 */
static bool remover_no_preventivo(FILE* arquivo, SuperBloco* sb, int64_t id_no, int32_t matricula) {
    PaginaBTree no;
    ler_pagina(arquivo, id_no, &no);

    int32_t idx = 0;
    while (idx < no.cabecalho.qtd_chaves && no.chaves[idx] < matricula) {
        idx++;
    }

    // CASO A: A chave está presente NESTE nó
    if (idx < no.cabecalho.qtd_chaves && no.chaves[idx] == matricula) {
        if (no.cabecalho.eh_folha) {
            // Caso A1: Está em nó folha. Basta deslocar para a esquerda excluindo-a.
            for (int32_t i = idx; i < no.cabecalho.qtd_chaves - 1; i++) {
                no.chaves[i] = no.chaves[i + 1];
                no.deslocamentos_registros[i] = no.deslocamentos_registros[i + 1];
            }
            no.cabecalho.qtd_chaves--;
            escrever_pagina(arquivo, id_no, &no);
            return true;
        } else {
            // Caso A2: Está em nó interno. Substitui pelo antecessor ou sucessor.
            int64_t id_filho_esq = no.deslocamentos_filhos[idx];
            PaginaBTree filho_esq;
            ler_pagina(arquivo, id_filho_esq, &filho_esq);

            if (filho_esq.cabecalho.qtd_chaves > MIN_CHAVES) {
                deslocamento_disco_t offset_ant;
                int32_t antecessor = obter_antecessor(arquivo, id_filho_esq, &offset_ant);
                no.chaves[idx] = antecessor;
                no.deslocamentos_registros[idx] = offset_ant;
                escrever_pagina(arquivo, id_no, &no);
                return remover_no_preventivo(arquivo, sb, id_filho_esq, antecessor);
            }

            int64_t id_filho_dir = no.deslocamentos_filhos[idx + 1];
            PaginaBTree filho_dir;
            ler_pagina(arquivo, id_filho_dir, &filho_dir);

            if (filho_dir.cabecalho.qtd_chaves > MIN_CHAVES) {
                deslocamento_disco_t offset_suc;
                int32_t sucessor = obter_sucessor(arquivo, id_filho_dir, &offset_suc);
                no.chaves[idx] = sucessor;
                no.deslocamentos_registros[idx] = offset_suc;
                escrever_pagina(arquivo, id_no, &no);
                return remover_no_preventivo(arquivo, sb, id_filho_dir, sucessor);
            }

            // Ambos têm exatamente MIN_CHAVES. Faz fusão e deleta do nó fundido.
            fundir_nos_irmaos(arquivo, sb, id_no, idx, &no);
            return remover_no_preventivo(arquivo, sb, id_filho_esq, matricula);
        }
    }

    // CASO B: A chave não está neste nó.
    if (no.cabecalho.eh_folha) {
        return false; // Matrícula não localizada
    }

    int64_t id_filho_alvo = no.deslocamentos_filhos[idx];
    PaginaBTree filho_alvo;
    ler_pagina(arquivo, id_filho_alvo, &filho_alvo);

    if (filho_alvo.cabecalho.qtd_chaves == MIN_CHAVES) {
        // Tenta inflar pegando do vizinho da esquerda
        if (idx > 0) {
            PaginaBTree irmao_esq;
            ler_pagina(arquivo, no.deslocamentos_filhos[idx - 1], &irmao_esq);
            if (irmao_esq.cabecalho.qtd_chaves > MIN_CHAVES) {
                pegar_emprestado_esquerda(arquivo, id_no, idx, &no, &filho_alvo, &irmao_esq);
                return remover_no_preventivo(arquivo, sb, id_filho_alvo, matricula);
            }
        }
        // Tenta inflar pegando do vizinho da direita
        if (idx < no.cabecalho.qtd_chaves) {
            PaginaBTree irmao_dir;
            ler_pagina(arquivo, no.deslocamentos_filhos[idx + 1], &irmao_dir);
            if (irmao_dir.cabecalho.qtd_chaves > MIN_CHAVES) {
                pegar_emprestado_direita(arquivo, id_no, idx, &no, &filho_alvo, &irmao_dir);
                return remover_no_preventivo(arquivo, sb, id_filho_alvo, matricula);
            }
        }
        // Sem chaves extras: executa a fusão (Merge) preventivo
        if (idx < no.cabecalho.qtd_chaves) {
            fundir_nos_irmaos(arquivo, sb, id_no, idx, &no);
        } else {
            fundir_nos_irmaos(arquivo, sb, id_no, idx - 1, &no);
            id_filho_alvo = no.deslocamentos_filhos[idx - 1]; 
        }
    }

    return remover_no_preventivo(arquivo, sb, id_filho_alvo, matricula);
}

/**
 * Função Principal Pública: Remove uma chave de forma atômica e thread-safe.
 */
bool remover_chave(GerenciadorBTree* btree, int32_t chave_matricula) {
    EnterCriticalSection(&btree->mutex_banco);

    if (btree->superbloco.id_pagina_raiz == DESLOCAMENTO_NULO) {
        LeaveCriticalSection(&btree->mutex_banco);
        return false; 
    }

    int64_t id_raiz = btree->superbloco.id_pagina_raiz;
    bool status = remover_no_preventivo(btree->arquivo_db, &btree->superbloco, id_raiz, chave_matricula);

    // Se a operação esvaziou a raiz por completo, a árvore encolhe um nível
    PaginaBTree raiz;
    ler_pagina(btree->arquivo_db, id_raiz, &raiz);
    if (raiz.cabecalho.qtd_chaves == 0 && !raiz.cabecalho.eh_folha) {
        btree->superbloco.id_pagina_raiz = raiz.deslocamentos_filhos[0];
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);

        // CORREÇÃO DA PILHA DE LIVRES DA RAIZ ANTIGA: Usa a função oficial do seu storage.h
        liberar_id_pagina(&btree->superbloco, id_raiz);
        escrever_superbloco(btree->arquivo_db, &btree->superbloco);
    }

    LeaveCriticalSection(&btree->mutex_banco);
    return status;
}