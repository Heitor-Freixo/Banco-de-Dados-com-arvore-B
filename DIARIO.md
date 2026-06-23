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