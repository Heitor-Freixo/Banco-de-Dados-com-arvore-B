# Diario de Engenharia - (Arvore B)

## 21/06/26
foi criado um repositorio específico para este trabalho interdisciplinar de SO e ED, junto a ele os arquivos, diário(onde todas as informações sobre o projeto serão anotadas), .gitignore (fiktro para que não venha nenhum .exe ou arquiv inútil para o git), ReadMe ( descrição e instruçãod e uso ). 
Passamos por dificuldades em sincronizar o git com os arquivos do computador, houve uma tentativa de se usar o codespace, mas foi optado o uso do git em seu modelo padrão.

## 23/06/26
o arquivo de execução rigida foi criado (makefile), qualquer compilação deve passar por seus critérios (-Wall -Wextra -Werror)
definida a arquitetura do projeto. Optou-se pelo tamanho de página de 4096 bytes (bloco de memória virtual) para espelhar os blocos do sistema operacional

Parte 2:criamos a base do I/O. Agora o sistema consegue criar um arquivo binário organizado em blocos de tamanho fixo. No começo desse arquivo, criamos uma área de controle (Superbloco) que sabe onde tudo começa. Também criamos um sistema inteligente: se algum dado for apagado, o espaço vazio é guardado para ser reutilizado depois, impedindo que o arquivo cresça sem necessidade.
Por fim, fizemos testes rigorosos para garantir que o programa leia e grave sempre no lugar exato do arquivo, sem misturar ou corromper os dados."
OBS: o comando make não vem no windows(tem que instalar)

pra que fui traduzir isso pra português, nenhuma chamada da certo.

Parte 3:implementamos a função de busca na Árvore B em disco, que lê as páginas do arquivo binário e faz busca binária na RAM. Para tratar a concorrência exigida, a IA sugeriu inicialmente a biblioteca <pthread.h> (do Linux), o que travou a compilação no Windows. Corrigimos isso substituindo o código para a biblioteca nativa do Windows (CRITICAL_SECTION via <windows.h>). Também silenciamos avisos de variáveis não usadas na main com (void)argv;

Parte 4:implementamos o algoritmo de inserção clássico da Árvore B com técnica estrutural Top-Down (Inserção Preventiva). Criamos as funções inserir_chave, inserir_em_no_com_espaco e dividir_no_filho em btree.c para lidar com a divisão (split) de nós que atingem o limite físico de chaves da página. O algoritmo aloca novas páginas binárias sob demanda e promove a chave mediana de forma estável. As validações comprovaram a integridade e a ordenação dos registros gravados diretamente em disco.

Parte 5:a lógica do núcleo do SGBD foi concluída com a implementação da rotina de remoção estruturada "Top-Down" preventiva na Árvore B em Disco. Foram desenvolvidas as funções de redistribuição de chaves (`pegar_emprestado_esquerda` e `pegar_emprestado_direita`) e a fusão de páginas físicas (`fundir_nos_irmaos`).

Erros Superados:
    Incompatibilidade de Gerenciamento de Espaço: Inicialmente, a lógica de fusão foi projetada tentando manipular uma lista ligada tradicional gravada diretamente nos nós descartados (`id_lista_livres`). Isso gerou erros de compilação (`struct unnamed has no field...`), pois a arquitetura real do nosso `storage.h` utiliza um vetor estático de cache interno no Superbloco (`paginas_livres` e `qtd_paginas_livres`). A solução foi refatorar o código para integrar-se perfeitamente com a função nativa `liberar_id_pagina()`.

    Correções de Sintaxe e Tipografia: Foram corrigidos erros estritos de compilação, como a passagem incorreta do Superbloco por valor na escrita final (esquecimento do operador de endereço `&`) e um erro de digitação no vetor de ponteiros de filhos (`deslocamentos_filisons` corrigido para `deslocamentos_filhos`).

Parte 6: Para otimizar o acesso ao disco, criamos um Buffer Cache na RAM com capacidade para 16 páginas. Utilizando a política de substituição LRU (Least Recently Used) por meio de uma lista duplamente ligada, garantimos que as páginas menos utilizadas sejam descartadas primeiro para abrir espaço quando o cache lotar.

Para suportar concorrência sem corrupção de dados, adotamos o mecanismo de Pins e Unpins: enquanto uma thread utiliza um nó, ele fica fixado e protegido contra descarte. Além disso, implementamos a escrita postergada (Dirty Bit), onde as modificações só são salvas fisicamente no arquivo quando a página precisa ser liberada do cache ou no encerramento do sistema. Toda a Árvore B foi refatorada e limpa para operar exclusivamente através dessa memória virtual.