#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SEMANTIC_URL_FILE "/Users/administrador/url_pattern_match.txt"

void patternMatchURL(char *requestURL, char *resultURL);



int main()
{
  char buf[512];
  patternMatchURL("/welcome/get/pablo/zeus@hotmail.com", buf);

  printf("%s\n", buf);
}


void patternMatchURL(char *requestURL, char *resultURL)
{
  FILE *file = fopen(SEMANTIC_URL_FILE, "r");
  char friendlyURL[255];
  char mappedURL[255];
  char bufFriendly[255];
  char bufRequest[255];
  char buf[255];

  if(file == NULL)
    {
      //Error file not found
      printf("lol");
      //fileNotFound(socket_fd);
      return ;
      //perror("file not found");
    }
    else
    { 
      int i;

      while(!feof(file))
      { 
        i = 0;

        fgets(buf, sizeof(friendlyURL), file); //Descartar la primera línea (descripción y espacios)

        fgets(friendlyURL, sizeof(friendlyURL), file);
        fgets(mappedURL, sizeof(mappedURL), file);

        int lenFriendly = strlen(friendlyURL);
        int lenRequest = strlen(requestURL);

        //printf("buf %s\n", buf);
        //printf("friendly %s\n", friendlyURL);
        //printf("mapped %s\n", mappedURL);

        while(i < lenFriendly && friendlyURL[i] != '$' && i < lenRequest)
        {
          bufFriendly[i] = friendlyURL[i];
          bufRequest[i] = requestURL[i];
          i++;
        }

        bufFriendly[i] = '\0';
        bufRequest[i] = '\0';

        i--;
        //i apunta al primer slash

        if(strcasecmp(bufFriendly, bufRequest) != 0)  
        {
          //printf("NOT");
          continue;
        }

          //printf("bufRequest %s\n", bufRequest);
          //printf("bufFriendly %s\n", bufFriendly);

        //Contar # de slashes y que haya algo adentro
        int slash_num_friendly = 0;
        int slash_num_request = 0;
        int j = i;
        int no_empty_arg = 1;

        while(i < lenFriendly)
        {
          if(friendlyURL[i] == '/')   slash_num_friendly++;
          i++;
        }

        i = j;

        while(i < lenRequest - 1)
        {
          if(requestURL[i] == '/')    slash_num_request++;

          //Hay algo de la  forma url/arg1//arg3, o sea, un arg vacío
          if(requestURL[i] == '/' && requestURL[i + 1] == '/')  no_empty_arg = 0;
          i++;
        }

        i = j;

        printf("slash_num_request %d\n", slash_num_request);
        printf("slash_num_friendly %d\n", slash_num_friendly);
        printf("no_empty_arg %d\n", no_empty_arg);

        //Si son iguales
        if(no_empty_arg && slash_num_request == slash_num_friendly)
        {
          printf("TROLOLOL\n");
          //Apunta al caracter después del primer slash, antes del primer argumento
          char *arg_string = requestURL + i + 1;

          printf("arg_string -> %s\n", arg_string);
          printf("requestURL -> %s\n", requestURL);

          int len = strlen(arg_string);

          //El número de slashes contados es el número de argumentos del query
          char args[slash_num_friendly][strlen(arg_string)];
          int current_char = 0;

          //Añadir los argumentos a un arreglo de strings
          for(i = 0, j = 0; i < len; i++, current_char++)
          {
            if(arg_string[i] == '/')    
            {
                args[j][current_char] = '\0';
                j++;
                i++;
                current_char = 0;
            }

            args[j][current_char] = arg_string[i];
          }

          int len_mapped = strlen(mappedURL);
          int k = 0;

          i = 0;
          j = 0;
          
          printf("mappedURL -> %s\n", mappedURL);
          printf("args0 -> %s\n", args[0]);
          printf("args1 -> %s\n", args[1]);

          while(i < len_mapped)
          {
            resultURL[j] = mappedURL[i];

            if(mappedURL[i] == '=')
            {
              strcat(resultURL, args[k]);
              j += strlen(args[k++]);
              i += 2; //Para el formato de $1
            }

            i++;
            j++;
          }

          fclose(file);
          return ;
        }

      }
    }

    fclose(file);
    return ;
}