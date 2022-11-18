/*INTEGRANTES
Gabriel Heifel
Maxwell Moreira
Rafael Martins
Willians Júnior
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mpi.h>



#define WIDTH 640
#define HEIGHT 360



unsigned char frameReferencia[WIDTH][HEIGHT];
unsigned char frameAtual[WIDTH][HEIGHT];
unsigned char U[WIDTH / 2][HEIGHT / 2];
unsigned char V[WIDTH / 2][HEIGHT / 2];
unsigned char blocoProcurado[8][8];
unsigned char blocoNoFrameRef[8][8];
FILE* out;

//unsigned char matrizVizinha[24][24];
int blocosIguais;
int totalBlocos;



void procuraBloco(int beginLine, int beginCol) {

    int i, j;
    int linJanViz, colJanViz;// indica onde esta sendo verificado na janela de vizinhan�a
    

    double somaErros;
    double erroMedioGeral;
    double tolerancia;

    tolerancia = 1.3;


    //Monta o bloco a ser procurado usando as linhas e colunas recebidas inicialmente
    for (i = 0;i < 8;i++) {
        for (j = 0;j < 8;j++) {
            if (i<0 || j<0 || i>HEIGHT || j >WIDTH) {//ISSO EVITA QUE ELE VERIFIQUE VALORES DE FORA DA MATRIZ (LIXO)
                blocoProcurado[i][j] = 0;
            }else{
                blocoProcurado[i][j] = frameAtual[i + beginLine][j + beginCol];
            }
           
        }
    }



    for (linJanViz = beginLine - 8;linJanViz < beginLine + 16;linJanViz++)//loop que percorre a janela de vizinhan�a, definida por 1 bloco inteiro ao redor do procurado
    {
        for (colJanViz = beginCol - 8;colJanViz < beginCol + 16;colJanViz++)
        {
            if (linJanViz<0 || colJanViz<0 || linJanViz>HEIGHT || colJanViz >WIDTH) {//ISSO EVITA QUE ELE VERIFIQUE VALORES DE FORA DA MATRIZ (LIXO)
                
            }
            else
            {

                //Aqui eu monto o bloco no frame referencia para fazer a compara��o com o bloco que procuro
                //Deve ser levado em conta a posi��o na janela de vizinhan�a que esta sendo verificado
                for (i = 0;i < 8;i++) {
                    for (j = 0;j < 8;j++) {
                        if (i<0 || j<0 || i>HEIGHT || j >WIDTH) {//ISSO EVITA QUE ELE VERIFIQUE VALORES DE FORA DA MATRIZ (LIXO)
                            blocoNoFrameRef[i][j] = 0;
                        }
                        else {
                            blocoNoFrameRef[i][j] = frameReferencia[i + linJanViz][j + colJanViz];
                        }         
                    }
                }

                

                somaErros = 0; //VAR que acumula a diferen�a entre os blocos
                //Aqui eu realizo a compra��o entre os blocos
                for (i = 0;i < 8;i++) {
                    for (j = 0;j < 8;j++) {
                        somaErros += abs(blocoProcurado[i][j]-blocoNoFrameRef[i][j]);
                    }
                }

                erroMedioGeral = somaErros / 64; //Calcula a media do erro percentual

                if (erroMedioGeral < tolerancia) { //se a media for menor que a tolerancia estabelecida considero os blocos iguais
                    //printf("Blocos Iguais! \n");
                    blocosIguais++;
                    fprintf(out,"Ra[%d][%d] > Rv[%d][%d] \n", beginLine, beginCol, linJanViz, colJanViz);
                    return; //se o bloco ja foi encontrado, entao a thread pode avan�ar para o proximo bloco
                }     
            }
        }
    }
}


int main(int argc, char* argv[]) {


    //Rank do processo
    int rank;
    //Tamanho do time
    int size;

    clock_t t;
    double time_taken;

    //Iinicia o MPI
    MPI_Init(&argc, &argv);
    //Identifica o rank da thread
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    //Identifica o tamanho do time
    MPI_Comm_size(MPI_COMM_WORLD, &size);



    if (size < 2) {
        printf("O programa requer ao menos 2 processos para ser executado! \n");
        MPI_Finalize();
        return 1;

    }

    if (argc < 2)
    {
        printf("ERRO!!! Enter: ./trabalho <filename>");
        MPI_Finalize();
        return 1;
    }

    const char* filename = argv[1];


    FILE* fp= NULL;
    int frameSize = WIDTH * HEIGHT;
    int line, col, i, beginLine, beginCol;

    //out � definido globalmente para acesso de fun��o externa
    out = fopen("out.txt", "w");
    if (!out)
    {
        perror("Error opening YUV video for read");
        return 1;
    }



    if (rank == 0) {


        //DEFINI��O DO ARQUIVO DE LEITURA E TAMANHO DOS FRAMES
        //apenas a thread 0 faz isso, por motivos de: Menos acesso de leitura ao disco
        //ela fica responsavel por repassar o conteudo �s outras threads
        

        fp = fopen(filename, "rb");
        if (!fp)
        {
            perror("Error opening YUV video for read");
            return 1;
            //Aqui n�o � invocado o MPI_Finalize(), mas diretamente o return, abortando todo o processo
            //as threads de servi�o n�o entram nesse trecho de c�digo, entao nao sabem que a thread principal
            //nao conseguiu ler o arquivo. Se for solicitado um MPI_Finalize() deveria ser enviado antes uma msg �s outras
            //threads indicando a falha na leitura do arquivo, pois elas seguem como se tudo tivesse acontecido corretamente
        }


        fread(frameReferencia, sizeof(unsigned char), frameSize, fp);// LE PRIMEIRO FRAME
        fread(U, sizeof(unsigned char), frameSize / 4, fp);// LE PRIMEIRO le o primeiro U
        fread(V, sizeof(unsigned char), frameSize / 4, fp);// LE PRIMEIRO FRAME

        fread(frameAtual, sizeof(unsigned char), frameSize, fp);// LE PRIMEIRO FRAME
        fread(U, sizeof(unsigned char), frameSize / 4, fp);// LE PRIMEIRO le o primeiro U
        fread(V, sizeof(unsigned char), frameSize / 4, fp);// LE PRIMEIRO FRAME
    }


    //A thread 0 (root) envia em broadcast uma copia para todos os threads dp time
    //Todas as threads possuem uma copia do frame de referencia para verifica��o
    //Foi utilizado o broadcast para evitar v�rias leituras simultaneas de arquivo, diminuindo acesso ao disco,e utilizando recurso do MPI
    MPI_Bcast(frameReferencia,WIDTH*HEIGHT,MPI_UNSIGNED_CHAR,0,MPI_COMM_WORLD);


    //A thread 0 (root) envia em broadcast uma copia para todos os threads dp time
    MPI_Bcast(frameAtual, WIDTH * HEIGHT, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);


    
    int threadDestino = 1; //utilizado no loop de distribui��o de trabalho
    int keepWorking = 1; //utilizada para indicar que a thread deve encerrar ou continuar executando
   

    if (rank == 0) { //THREAD 0 IS DEALING CARDS (diz os indices que cada thread deve procurar os quadros)
        
        t = clock();//inicia o timer;

        for (line = 0;line < HEIGHT;line = line + 8) //PULA DE 8 EM 8 POIS 8X8 � O TAMANHO DO BLOCO A SER PROCURADO
        {
            for (col = 0;col < WIDTH;col = col + 8)
            {
                //A thread 0 envia o L e o C de onde as threads devem ler o conteudo para procurar
                MPI_Send(&line, 1,MPI_INT, threadDestino, 0,MPI_COMM_WORLD);
                MPI_Send(&col, 1, MPI_INT, threadDestino, 0, MPI_COMM_WORLD);
                threadDestino++;//troca a proxima thread que vai receber os indices para procurar
                if (threadDestino >= size)
                    threadDestino = 1;
            }
        }


        //ao sair dos la�os superior, a thread 0 identifica que acabou o conteudo que deve ser verificado e deve informar 
        //as outras threads para encerrar, isso � feito enviando o valor -1
        for (i = 1;i < size;i++) {
            keepWorking = -1;
            MPI_Send(&keepWorking, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
        }

        //Solicita o que cada thread encontrou de blocos iguais e faz o reduce acumulativo
        MPI_Reduce(&blocosIguais, &totalBlocos, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
        t = clock() - t;
        time_taken = ((double)t) / CLOCKS_PER_SEC;

        printf("Total de blocos iguais encontrados: %d\n Tempo decorrido: %f segundos \n", totalBlocos,time_taken);

        fclose(fp);
        fclose(out);
        MPI_Finalize();

    }else{

        blocosIguais = 0;
        
        while (keepWorking) {
            
            //A thread recebe a linha da thread 0
            MPI_Recv(&beginLine, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);

            if (beginLine != -1) {//Isso verifica se o valor passado pela thred 0 n�o � o valor para parar de trabalhar

                //A thread recebe a coluna da thread 0
                MPI_Recv(&beginCol, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);

                procuraBloco(beginLine, beginCol);
            }
            else {

                //envia para a thread 0 a quantidade de blocos que achou
                MPI_Reduce(&blocosIguais, &totalBlocos, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
                keepWorking = -1;
                fclose(out);
                MPI_Finalize();
            }
        }
        
    }

    
    
    return 0;
}