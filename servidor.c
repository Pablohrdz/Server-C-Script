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
#include <signal.h>

#define EXIT_SUCCESS 0
#define EXIT_ERROR 1
#define TRUE 1
#define PORT "80"
#define SEMANTIC_URL_FILE "/home/ec2-user/Web_Server/url_pattern_match.txt"

//Prototypes
int readLineFromFileDescriptor(int socket_fd, char *buf, int len);
void handleRequest(int socket_fd);
void executeCGI(int socket_fd, const char *file_path, const char *method, const char *query);
void sendRegularFile(int socket_fd, const char *file_path);
void sendHeaders(int socketfd, const char *filename, int image);

void serverError(int socket_fd);
void fileNotFound(int socket_fd);
void badRequest(int socket_fd);
void unimplementedRequestMethod(int socket_fd);

void patternMatchURL(char *requestURL, char *resultURL);

void sigchld_handler(int s)
{
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

int main()
{	
	//getaddrinfo
	int resp_getaddr = -1;
	struct addrinfo hints, *server_addr_info;
	struct sigaction sig_act;


	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE; //Usar la ip de la máquina en la que se corra
	hints.ai_family = AF_UNSPEC; //Puede ser IP4 ó IP6
	hints.ai_socktype = SOCK_STREAM; //TCP/IP

	//Llenar el struct server_addr_info con los datos del servidor
	if( (resp_getaddr = getaddrinfo(NULL, PORT, &hints, &server_addr_info)) != 0 )
	{
		fprintf(stderr, "error getaddrinfo %s \n", gai_strerror(resp_getaddr));
	}

	//fprintf(stdout, "%s\n", );

	//Socket + setsockopt para reutilizar el puerto si está en uso + bind
	int socket_fd = -1;
	int flag = 1; //Para el setsockopt
	struct addrinfo *temp;

	//Iterar por la lista de resultados
	for(temp = server_addr_info; temp != NULL; temp = temp -> ai_next) 
	{
		//Asignación del socket
	    if( (socket_fd = socket(temp -> ai_family, temp -> ai_socktype, temp -> ai_protocol)) == -1)
		{	
			//errno
			perror("error socket");
			continue;
		}

		//Por si el puerto está en uso
		if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) != 0)
		{
			//errno
			perror("error opción socket");
			close(socket_fd);
			continue;
		}

		//Realizar el bind del puerto y dirección con el socket_fd
		if( bind(socket_fd, temp -> ai_addr, temp -> ai_addrlen) != 0 )
		{
			//errno
			perror("error bind");
			close(socket_fd);
			continue;
		}

		break;
	}

	//Aquí, el socket_fd ya está unido (bounded) con la dirección IP y puerto

	//Darle free a la lista ligada
	freeaddrinfo(server_addr_info); 

	//Listen
	if( listen(socket_fd, 10) != 0 )
	{
		//errno
		perror("error listen");
		exit(EXIT_ERROR);
	}

	//Para el accept
	int nuevo_socket_fd = -1;
	struct sockaddr_storage client_address;
	socklen_t client_address_size = sizeof(client_address);

	sig_act.sa_handler = sigchld_handler; // reap
    sigemptyset(&sig_act.sa_mask);
    sig_act.sa_flags = SA_RESTART;
    
    if (sigaction(SIGCHLD, &sig_act, NULL) == -1) 
    {
        perror("sigaction");
        exit(1);
    }


	printf("Esperando peticiones... \n");

	while(TRUE)
	{
		//Aceptar la conexión entrante
		if( (nuevo_socket_fd = accept(socket_fd, (struct sockaddr *) &client_address, &client_address_size)) == -1 )
		{	
			//errno
			perror("error accept");
			continue;
		}

		//Notificar en consola
		fprintf(stdout, "Se aceptó la conexión.");

		//Fork (hijo)
		if(!fork())
		{
			//Leer request hasta que se lea un '\n'
			handleRequest(nuevo_socket_fd);
		}

		//Cerrar socket del padre
		close(nuevo_socket_fd);
	}
	
	close(socket_fd);
		
}

/* Maneja los requests del cliente */
void handleRequest(int socket_fd)
{
	int is_cgi = 0;
	char request[1024], method[255], url[255], file_path[512];
	int request_len = sizeof(request);
	int n = readLineFromFileDescriptor(socket_fd, request, request_len);
	struct stat file_stat;
	int is_semantic_url = 0;

	int i = 0;

	//Leer el tipo de método (se lee hasta encontrar un espacio)
	while(i < request_len - 1 && !isspace((int) request[i]))
	{
		method[i] = request[i];
		i++;
	}

	//Para que funcionen los métodos de strings
	method[i] = '\0';

	int j = 0;

	//Brincarse todos los espacios hasta el URL
	while (isspace((int) request[i]) && (i < request_len))	i++;

	//Leer el url
	while(i < request_len && !isspace((int) request[i]) && j < sizeof(url) - 1)
	{
		url[j] = request[i];
		i++;
		j++;
	}

	url[j] = '\0';

	//Si el método no está implementado, comunicárselo al cliente
	if(strcasecmp(method, "GET") != 0 && strcasecmp(method, "POST"))
	{
		unimplementedRequestMethod(socket_fd);
		exit(EXIT_ERROR);
	}

	i = 0;

	//Obtener el string del archivo
	if(strcasecmp(method, "GET") == 0)
	{
		while(url[i] != '\0')
		{
			if(url[i] == '?')
			{
				is_cgi = 1;
				url[i] = '\0';
				i++;
				break;
			}

			i++;
		}

		//Semantic URL
		if(!is_cgi)
		{	
			is_semantic_url = 1;
			is_cgi = 1;

			//String temporal
			char temp_url[512];
			sprintf(temp_url, url);
			memset(url, 0, sizeof(url));

			patternMatchURL(temp_url, url);
		}
	}

	else if(strcasecmp(method, "POST") == 0)	
	{
		is_cgi = 1;
	}

	//sprintf(file_path, "/Users/administrador%s", url);

	//Si no es semantic, pegarle el extra
	if(!is_semantic_url)	
	{
		sprintf(file_path, "/home/ec2-user/Web_Server/cgi-bin%s", url);
	}
	//Si es semantic y ya fue transformado, guardar el índice del query para mandarlo al cgi
	else
	{
		int len = strlen(url);
		j = 0;

		while(j < len)
		{
			if(url[j] == '?')		
			{
				url[j++] = '\0';
				break;
			}
			j++;
		}
		printf("url -> %s\n", url);
		sprintf(file_path, url);
	}

	/* DEBUG
	if(send(socket_fd, file_path, strlen(file_path), 0) == -1)
	{
		perror("error send");
	}
	*/

	if (file_path[strlen(file_path) - 1] == '/')
	{
		strcat(file_path, "index.html");
	}
	 
	if(stat(file_path, &file_stat) == -1)
	{
		//Enviar mensaje de "file not found"
		fileNotFound(socket_fd);
	}
	else
	{
		//Si es un directorio, hacer que apunte al index.html del mismo
		if( (file_stat.st_mode & S_IFMT) == S_IFDIR)
		{
			strcat(file_path, "/index.html");
		}

		if(!is_cgi)
		{
			sendRegularFile(socket_fd, file_path);
		}
		else
		{
			char *query = url;

			if(!is_semantic_url)		query += i;
			else						query += j;

			printf("query -> %s", query);

			executeCGI(socket_fd, file_path, method, query);
		}
	}

	close(socket_fd);
}

/* Ejecuta un archivo PHP mediante GET o POST */
void executeCGI(int socket_fd, const char *file_path, const char *method, const char *query)
{
	char buf[1024];
	int content_length = -1;
	int n = 1;
	pid_t pid; //pid_t para el waitpid(2)
	int child_status;
	

	//Pipes
	int pipe_output[2];
	int pipe_input[2];

	buf[0] = 's'; 
	buf[1] = '\0'; 

	//GET
	if (strcasecmp(method, "GET") == 0)
	{
		//Brincarse los headers del GET
		while ((n > 0) && strcmp("\n", buf) != 0) 
		{
			n = readLineFromFileDescriptor(socket_fd, buf, sizeof(buf));
		}
	}

	//POST
	else    
	{
		 n = readLineFromFileDescriptor(socket_fd, buf, sizeof(buf));

		 //Brincarse los headers y guardar el Content-Length
		 while ((n > 0) && strcmp("\n", buf))
		 {
		 	 buf[strlen("Content-Length:")] = '\0';

		  	 if(strcasecmp(buf, "Content-Length:") == 0)
		  	 {
		  	 	content_length = atoi(&(buf[strlen("Content-Length:") + 1]));
		  	 }
		   
		  	 n = readLineFromFileDescriptor(socket_fd, buf, sizeof(buf));
		 }

		 //El CONTENT_LENGTH no se especificó; BAD REQUEST
		 if (content_length == -1) 
		 {
		 	 badRequest(socket_fd);
		  	 return;
		 }
	}

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	//Error de pipe
	if (pipe(pipe_output) < 0 || pipe(pipe_input) < 0)
	{
	 	serverError(socket_fd);
	 	return;
	}

	//Error de fork
	if ( (pid = fork()) < 0 ) 
	{
		 serverError(socket_fd);
	 	 return;
	}

	//Proceso hijo, encargado de ejecutar el script de PHP-CGI
	if (pid == 0)  
	{
		 //Variables de entorno para PHP
		 char env_method[255], env_query_string[255];
		 char env_content_length[255], env_content_type[255];
		 char env_script_filename[255], env_redirect_status[255];


		 //Unir los pipes del hijo con standard output e input
		 dup2(pipe_output[1], 1);
		 dup2(pipe_input[0], 0);

		 //Cerrar conexiones innecesarias
		 close(pipe_output[0]);
		 close(pipe_input[1]);

		 //Variables de entorno en común para ambos métodos
		 sprintf(env_method, "REQUEST_METHOD=%s", method);
		 putenv(env_method);

		 sprintf(env_redirect_status, "REDIRECT_STATUS=CGI");
		 putenv(env_redirect_status);

		 sprintf(env_script_filename, "SCRIPT_FILENAME=%s", file_path);
		 putenv(env_script_filename);

		 
		 //Variables de entorno específicas para GET
		 if (strcasecmp(method, "GET") == 0) 
		 {
		 	sprintf(env_query_string, "QUERY_STRING=%s", query);
		  	putenv(env_query_string);
		  	
		  	//DEBUG
		  	//send(socket_fd, query, strlen(query), 0); 
		 }

		 //Variables de entorno específicas para POST
		 else 
		 {   
		 	sprintf(env_content_length, "CONTENT_LENGTH=%d", content_length);
		  	putenv(env_content_length);

		  	//DEBUG
		  	//send(socket_fd, env_content_length, strlen(env_content_length), 0);

		  	sprintf(env_content_type, "CONTENT_TYPE=application/x-www-form-urlencoded");
		  	putenv(env_content_type);
		 }

		 //execl("/usr/bin/php", "/usr/bin/php", "-q", file_path, 0);
		 execl("/usr/bin/php-cgi-5.6", "/usr/bin/php-cgi-5.6", "-q", file_path, 0);
		 
		 exit(EXIT_ERROR);
	} 

	//Proceso padre
	else
	 {   
	 	 char c;

	 	 //Cerrar pipes innecesarios
		 close(pipe_output[1]);
		 close(pipe_input[0]);
		 
		 if (strcasecmp(method, "POST") == 0)
		 {
		 	int i;

		    for (i = 0; i < content_length; i++) 
		    {
			  	recv(socket_fd, &c, 1, 0);
			 	write(pipe_input[1], &c, 1);
			}
		 }

		 while (read(pipe_output[0], &c, 1) > 0)		
		 {
		 	send(socket_fd, &c, 1, 0);
		 }

		 close(pipe_output[0]);
		 close(pipe_input[1]);

		 //Esperar al proceso hijo
		 waitpid(pid, &child_status, 0);
	}
}


/* Envía un archivo html al cliente */
void sendRegularFile(int socket_fd, const char *file_path)
{
	FILE *file = NULL;
	char buf[1024];
	int n = 10;

	//Ignorar headers
	while ((n > 0)) 
	{
		n = readLineFromFileDescriptor(socket_fd, buf, sizeof(buf));

		if(strcmp(buf, "\n") == 0)	break;
	} 
  	
  	file = fopen(file_path, "r");

  	if(file == NULL)
  	{
  		//Error file not found
  		fileNotFound(socket_fd);
  		//perror("file not found");
  	}
  	else
  	{
  		//Enviar los headers
  		if( strcmp(file_path + strlen(file_path) - 3, "jpg") )		sendHeaders(socket_fd, file_path, 1);
  		else														sendHeaders(socket_fd, file_path, 0);

  		//Enviar el archivo
  		memset(buf, 0, sizeof(buf));
  		fgets(buf, sizeof(buf), file);

  		while(!feof(file))
  		{
  			send(socket_fd, buf, strlen(buf), 0);
  			fgets(buf, sizeof(buf), file);
  		}
  	}

  	fclose(file);
}

/* Manda los headers al cliente */
void sendHeaders(int socketfd, const char *filename, int image)
{
 	char buf[512];

 	strcpy(buf, "HTTP/1.0 200 OK\r\n");
 	send(socketfd, buf, strlen(buf), 0);

 	strcpy(buf, "Servidor de Pablo HC\r\n");
 	send(socketfd, buf, strlen(buf), 0);

 	if(image)
 	{
 		sprintf(buf, "Content-Type: image/jpg\r\n");
 		send(socketfd, buf, strlen(buf), 0);
 	}
 	else
 	{
 		sprintf(buf, "Content-Type: text/html\r\n");
 		send(socketfd, buf, strlen(buf), 0);
 	}

 	strcpy(buf, "\r\n");
 	send(socketfd, buf, strlen(buf), 0);
}



/* Lee una línea del socket, suponiendo que ésta termina en \n o en \r\n
 * La información se guarda en el buffer (buf) de tamaño len, leyendo del
 * socket socketfd. */
int readLineFromFileDescriptor(int socket_fd, char *buf, int len)
{
	int bytes_read = 0;
	char temp = '\0';
	int n = 0;

	//len - 1 para poder concatenarle el '\0', al final, en caso de que se acabe el buffer
	while(bytes_read < len - 1 && temp != '\n')
	{
		n = recv(socket_fd, &temp, 1, 0);

		//Si se recibieron datos
		if(n > 0)
		{
			//DEBUG
			//send(socket_fd, "lol", strlen("lol"), 0);

			//Carriage return. Este if toma en cuenta el caso de \r\n
			if(temp == '\r')
			{
				//Asomarse al socket
				n = recv(socket_fd, &temp, 1, MSG_PEEK);
				
				if(n > 0 && temp == '\n')
				{
					//Actualizar el socket 
					recv(socket_fd, &temp, 1, 0);
				}
				else
				{
					temp = '\n';
				}
			}
		}

		else
		{
			break;
		}

		buf[bytes_read] = temp;
		bytes_read++;
	}

	buf[bytes_read] = '\0';

	return bytes_read;
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



//Error 500
void serverError(int socket_fd)
{
	char buf[512];

	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "Content-type: text/html\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "<p>Error prohibited CGI execution.</p>\r\n");
	send(socket_fd, buf, strlen(buf), 0);
}

//Error 400
void badRequest(int socket_fd)
{
	char buf[512];

	sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
	send(socket_fd, buf, sizeof(buf), 0);

	sprintf(buf, "Content-type: text/html\r\n");
	send(socket_fd, buf, sizeof(buf), 0);

	sprintf(buf, "\r\n");
	send(socket_fd, buf, sizeof(buf), 0);

	sprintf(buf, "<p>Your browser sent a bad request.<p>");
	send(socket_fd, buf, sizeof(buf), 0);
}

//Error 404
void fileNotFound(int socket_fd)
{
	char buf[512];

	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "Servidor de Pablo HC\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "Content-Type: text/html\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "<html><title>Not Found</title>\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "<body><p>The server could not fulfill\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "your request because the resource specified\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "is unavailable or nonexistent.</p>\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "</body></html>\r\n");
	send(socket_fd, buf, strlen(buf), 0);
}

//Método no implementado
void unimplementedRequestMethod(int socket_fd)
{
	char buf[512];

	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "Servidor de Pablo HC\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "Content-Type: text/html\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "<html><head><title>Method Not Implemented\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "</title></head>\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "<body><p>HTTP request method not supported.</p>\r\n");
	send(socket_fd, buf, strlen(buf), 0);

	sprintf(buf, "</body></html>\r\n");
	send(socket_fd, buf, strlen(buf), 0);
}

