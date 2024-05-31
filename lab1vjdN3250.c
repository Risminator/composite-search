#define _XOPEN_SOURCE 500   // for nftw()

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ftw.h>
#include <getopt.h>
#include <dlfcn.h>
#include <errno.h>
#include <alloca.h>
#include <sys/types.h>
#include <dirent.h>

#include "plugin_api.h"

#define PATH_MAX 4096
#define UNUSED(x) (void)(x)

static char *g_program_name = "lab1vjdN3250";
static char *g_version = "1.1";
static char *g_author = "Дронов Вадим Юрьевич";
static char *g_group = "N3250";
static char *g_lab_variant = "1";

// Массив handle’ов всех найденных библиотек-плагинов
static void **handles;
static int number_of_plugins = 0;

// Массив handle’ов найденных библиотек-плагинов (только запрошенные пользователем)
static void **in_handles;
static int in_handles_len = 0;

// Массив всех найденных длинных опций - они будут переданы в getopt
static struct option *long_options;
static int number_of_options = 0;

// Массив найденных длинных опций (только запрошенные пользователем)
static struct option *in_opts;
static int in_opts_len = 0;

// Массив массивов in_opts для передачи разным плагинам
static struct option** plugin_in_opts;
static int *plugin_in_opts_len;

// Флаги опций для логических операций (по умолч. -A, без -N)
static char flagAO = 'A';
static int flagN = 0;

// Вывод справочной информации
static void print_help();
static void print_info();

// Функция при рекурсивном поиске подходящих файлов для nftw
static int walk_func(const char*, const struct stat*, int, struct FTW*);

// Функция поиска плагинов для nftw. Заполняются handles и long_options
static int find_plugin(const char*, const struct stat*, int, struct FTW*);

// Из всех найденных handle’ов создаёт массивы только из запрошенных пользователем
// В итоге заполняются plugin_in_opts и plugin_in_opts_len
static int get_in_handles();

int main(int argc, char *argv[]) {
	char *DEBUG = getenv("LAB1DEBUG");
	char *path_to_dir = NULL;	
	int path_len;
	
	// Обработка ситуации, когда -P - последний аргумент
	if (strcmp(argv[argc-1], "-P") == 0)
	{
		fprintf(stderr, "%s: Option '%s' requires an argument\n", argv[0], argv[argc-1]);
		goto END;
	}
	
	for (int i = 0; i < argc; ++i)
	{
		if (strcmp(argv[i], "-P") == 0)
		{
			path_len = strlen(argv[i + 1]);
			path_to_dir = malloc(path_len + 1);
			if (!path_to_dir) {
				fprintf(stderr, "ERROR: malloc() failed: %s\n", strerror(errno));
				goto END;
			}
			strcpy(path_to_dir, argv[i + 1]);
			// Обработка ошибки, если дана некорректная директория
			void* dir = opendir(path_to_dir);
			if (!dir) {
				fprintf(stderr, "ERROR: Directory not found: %s\n", path_to_dir);
				goto END;
			}
			else closedir(dir);
			
			if (DEBUG) fprintf(stderr, "DEBUG: Plugin search directory was changed to %s\n", path_to_dir);
			break;
		}
		else if (i == argc - 1) {
			// Если -P не была найдена, используем каталог с приложением
			char buf[PATH_MAX];
			int rl_ret;
			if ((rl_ret = readlink("/proc/self/exe", buf, 4096)) < 0)
			{
				fprintf(stderr, "ERROR: readlink() failed: %s\n", strerror(errno));
				goto END;
			}
			// Убираем название программы из пути - получаем путь к каталаогу
			int path_len = rl_ret - strlen(argv[0]) + 1;
			path_to_dir = malloc(path_len + 1);
			path_to_dir[path_len] = '\0';
			strncpy(path_to_dir, buf, path_len);
			break;
		}
	}
	
	if (DEBUG) fprintf(stderr, "DEBUG: path_to_dir = %s\n", path_to_dir);
	
	// Поиск плагинов. Заполняется handles и long_options
	// Возвращается -1 только при критической ошибке в realloc. Тогда завершаем работу программы
	if (nftw (path_to_dir, find_plugin, 10, FTW_PHYS) < 0) {
		perror("nftw");
		goto END;
	}
	if (!long_options)
		fprintf(stderr, "WARNING: No valid plugins found in %s\n", path_to_dir);
	
	// Переменные для getopt	
	int c, option_index;
	int flagA = 0, flagO = 0;
	
	optind = 0;
	while(1) {
    	c = getopt_long (argc, argv, "P:AONvh", long_options, &option_index); //
    	if (c==-1) 
     		break;
    	switch(c) {
			case 'P':
				break; // already processed
			case 'A':
				flagA = 1;
				break;
			case 'O':
				flagO = 1;
				break;
			case 'N':
				flagN = 1;
				break;
			case 'v':
				print_info();
				goto END;
			case ':':
				fprintf(stderr, "Option needs a value\n");
				break;
			case '?':
			case 'h':
				printf("Usage: %s [options] <start_dir>\n\n", argv[0]);
				print_help();
				goto END;
				break; 
			case 0:
				// Если нашёлся плагин – добавляем в запрошенные пользователем
				in_opts = (struct option*) realloc(in_opts, (in_opts_len+1)*sizeof(struct option));
				in_opts[in_opts_len] = long_options[option_index];
				in_opts[in_opts_len].flag = (int*) optarg;
				in_opts_len++;
				break;
			default:
				fprintf(stderr, "WARNING: Getopt couldn't resolve the option\n");
				break;
		}
	}
	
	// Обработка случая, когда каталог не был задан или аргументов больше/меньше, чем нужно
	if (argc - optind != 1)
	{
		fprintf(stderr, "Usage: %s [options] <start_dir>\n\n", argv[0]);
		print_help();
		goto END;
	}
	
	if (flagA && flagO)
	{
		fprintf(stderr, "ERROR: Can't use -A and -O options together\n");
		goto END;
	}
	else if (flagO) flagAO = 'O';
	
	if (DEBUG) fprintf(stderr, "DEBUG: AO flag = %c; N flag = %d\n", flagAO, flagN);
	
	if (DEBUG)
	{
		fprintf(stderr, "DEBUG: Нужные опции:\n");
		for (int i = 0; i < in_opts_len; ++i)
		{
			fprintf(stderr, "\t%s\n", in_opts[i].name);
			fprintf(stderr, "\t%d\n", in_opts[i].has_arg);
			fprintf(stderr, "\t%s\n", (char*)in_opts[i].flag);
			fprintf(stderr, "\t%d\n", in_opts[i].val);
		}
	}
	
	// Получаем заполненные массивы in_handles и plugin_in_opts
	if (get_in_handles() < 0)
		goto END;
	
	if (DEBUG) {
		fprintf(stderr, "DEBUG: Plugin in opts:\n");
		for (int i = 0; i < in_handles_len; ++i) {
				fprintf(stderr, "\tHandle number %d, len = %d\n", i, plugin_in_opts_len[i]);
			for (int j = 0; j < plugin_in_opts_len[i]; ++j) {
				fprintf(stderr, "\t\t%s %s\n", plugin_in_opts[i][j].name, (char*)plugin_in_opts[i][j].flag);
			}
		}
	}
	
	if (DEBUG) {
		fprintf(stderr, "DEBUG: in_handles array:\n");
		typedef int (*pgi_func_t)(struct plugin_info*);
		for (int i = 0; i < in_handles_len; ++i)
		{
			fprintf(stderr, "\tHandle number %d:\n", i);
			
			void *func = dlsym(in_handles[i], "plugin_get_info");
			struct plugin_info pi = {0};
			pgi_func_t pgi_func = (pgi_func_t)func;            
			int ret = pgi_func(&pi);
			if (ret < 0)
				fprintf(stderr, "DEBUG: WARNING: plugin_get_info ended with an error!\n");
			
			for (size_t j = 0; j < pi.sup_opts_len; ++j)
				fprintf(stderr, "\t\t%s\n", pi.sup_opts[j].opt.name);
		}
		fprintf(stderr, "DEBUG: in_handles_len = %d\n", in_handles_len);
	}
	
	if (DEBUG) fprintf(stderr, "DEBUG: Target directory = %s\n", argv[optind]);
	// Рекурсивный поиск файлов по критериям	
	int res = nftw(argv[optind], walk_func, 10, FTW_PHYS);
	if (res < 0) {
		perror("nftw");
		goto END;
	}

	// Очистка памяти	
	END:
	for (int i = 0; i < number_of_plugins; ++i)
		if (handles[i]) dlclose(handles[i]);
	if (handles) free(handles);
	if (in_handles) free(in_handles);
	if (long_options) free(long_options);
	if (in_opts) free(in_opts);
	if (path_to_dir) free(path_to_dir);
	for (int i = 0; i < in_handles_len; i++) 
		if (plugin_in_opts[i]) free(plugin_in_opts[i]);
	if (plugin_in_opts) free(plugin_in_opts);
	if (plugin_in_opts_len) free(plugin_in_opts_len);
		
	return 0;
}

int walk_func(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
	// Unused but necessary for nftw
	UNUSED(sb); UNUSED(ftwbuf);
	
	char *DEBUG = getenv("LAB1DEBUG");
	errno = 0;
	
	// Директории не обрабатываем
	if (typeflag == FTW_D || typeflag == FTW_DP || typeflag == FTW_DNR) return 0;
	
	// Массив возвращаемых для файла значений из функции plugin_process_file()	
	int *plugin_results = alloca(in_handles_len*sizeof(int));
	// Для каждого нужного плагина вызываем ppf с нужными массивами опций
	for (int i = 0; i < in_handles_len; ++i) {
		void *func = dlsym(in_handles[i], "plugin_process_file");
		if (!func) {
			fprintf(stderr, "ERROR: walk_func: No plugin_process_file() function found\n");
			return 0;
		}
		
		typedef int (*ppf_func_t)(const char*, struct option*, size_t);
		ppf_func_t ppf_func = (ppf_func_t)func;

		plugin_results[i] = ppf_func(fpath, plugin_in_opts[i], plugin_in_opts_len[i]);
		if (DEBUG)
			fprintf(stderr, "DEBUG: walk_func: plugin_process_file() returned %d\n", plugin_results[i]);
		if (plugin_results[i] < 0) {
			// Пользователю важна только ошибка аргумента
			// Остальное (файл размером 0, нет доступа к файлу и т.п.) его не интересует
			if (errno == EINVAL || DEBUG)
				fprintf(stderr, "ERROR: walk_func: File not processed: %s\n", strerror(errno));
			return 0;
		}
	}

	// Определяем, подходит ли файл запрошенным криитериям (лог. операции)	
	int ret;
	if (flagAO == 'A') {
		ret = 1;
		for (int i = 0; i < in_handles_len; ++i)
			ret = ret && !(plugin_results[i]);
	}
	else {
		ret = 0;
		for (int i = 0; i < in_handles_len; ++i)
			ret = ret || !(plugin_results[i]);
	}
	
	if (flagN) ret = !ret;
	
	if (ret) fprintf(stdout, "%s\n", fpath);
    
    return 0;
}

int find_plugin(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
	// Unused but necessary for nftw
	UNUSED(sb); UNUSED(ftwbuf);
	
	char *DEBUG = getenv("LAB1DEBUG");

	// Директории точно не являются динамическими библиотеками
	if (typeflag == FTW_D || typeflag == FTW_DP || typeflag == FTW_DNR) return 0;
	
	void *handle;
	handle = dlopen(fpath, RTLD_LAZY);
	if (!handle) return 0;
	
	// Если нет plugin_get_info, не можем работать с этой библиотекой
	void *func = dlsym(handle, "plugin_get_info"); 
	if (!func) {
		fprintf(stderr, "WARNING: find_plugin: No plugin_get_info() function found in %s\n", fpath);     
        if (handle) dlclose(handle);
        return 0;
    }
    
	// Если plugin_get_info вернул < 0, с таким плагином работать не можем 
    struct plugin_info pi = {0};
    typedef int (*pgi_func_t)(struct plugin_info*);
    pgi_func_t pgi_func = (pgi_func_t)func;            
    int ret = pgi_func(&pi);
    if (ret < 0) {   
    	fprintf(stderr, "WARNING: find_plugin: plugin_get_info() function failed for %s\n", fpath);     
        if (handle) dlclose(handle);
        return 0;
    }
    
	// Если нет plugin_process_file, не можем работать с этой библиотекой
    func = dlsym(handle, "plugin_process_file");
    if (!func) {
    	fprintf(stderr, "WARNING: find_plugin: No plugin_process_file() function found in %s\n", fpath);     
        if (handle) dlclose(handle);
        return 0;
    }
    
    // Если плагин подходящий, то выделяем на его handle место в массиве
    handles = (void**) realloc(handles, (number_of_plugins+1)*sizeof(void*));
    if (!handles) {
    	// Если ошибка в realloc, нужно завершить работу программы
    	fprintf(stderr, "ERROR: find_plugin: realloc() failed: %s\n", strerror(errno));
    	if (handle) dlclose(handle);
    	return -1;
    }
    handles[number_of_plugins] = handle;
    number_of_plugins++;
    
    long_options = (struct option*) realloc(long_options, sizeof(struct option)*(pi.sup_opts_len+number_of_options));
    
    if (!long_options) {
    	// Если ошибка в realloc, нужно завершить работу программы
    	fprintf(stderr, "ERROR: find_plugin: realloc() failed: %s\n", strerror(errno));
    	return -1;
    }
    
	// Заполняем long_options для getopt
    for (size_t i = 0; i < pi.sup_opts_len; i++) {
		long_options[number_of_options] = pi.sup_opts[i].opt;
		number_of_options++;
		if (DEBUG)
			fprintf(stderr, "DEBUG: find_plugin: found option %s\n", pi.sup_opts[i].opt.name);
	}

	return 0;
}

int get_in_handles() {
	typedef int (*pgi_func_t)(struct plugin_info*);
	
	// Проходим по каждому найденному плагину
	for (int i = 0; i < number_of_plugins; ++i) {
		void *func = dlsym(handles[i], "plugin_get_info"); 
		if (!func) {
			fprintf(stderr, "ERROR: get_in_handles: dlsym\n");     
		    return -1;
		}
		struct plugin_info pi = {0};
		pgi_func_t pgi_func = (pgi_func_t)func;            
		int ret = pgi_func(&pi);
		if (ret < 0) {   
			fprintf(stderr, "ERROR: get_in_handles: plugin_get_info() function failed\n");    
		    return -1;
		}
		
		// Находим совпадения между найденными опциями и запрошенными пользователем
		// Если совпадение есть, добавляем handle в in_handles, а каждую совпавшую 
		// опцию – в соответствующий плагину массив массива plugin_in_opts
		int match = 0;
		plugin_in_opts_len = (int*) realloc(plugin_in_opts_len, (in_handles_len+1)*sizeof(int));
		plugin_in_opts_len[in_handles_len] = 0;
		for (int j = 0; j < in_opts_len; j++) {
			for (size_t k = 0; k < pi.sup_opts_len; ++k) {			
				if (strcmp(pi.sup_opts[k].opt.name, in_opts[j].name) == 0) {
					plugin_in_opts_len[in_handles_len - match]++;
					if (!match) {
						//  При первом совпадении опции плагина выделяем место на handle
						in_handles = (void**) realloc(in_handles, (in_handles_len+1)*sizeof(void*));
						if (!in_handles) {
							fprintf(stderr, "ERROR: get_in_handles: realloc() failed: %s\n", strerror(errno));
							return -1;
						}
						in_handles[in_handles_len] = handles[i];
						// Выделяем место на массив нужных опций для данного плагина
						plugin_in_opts = (struct option**) realloc(plugin_in_opts, (in_handles_len+1)*sizeof(struct option*));
						if (!plugin_in_opts) {
							fprintf(stderr, "ERROR: get_in_handles: realloc() failed: %s\n", strerror(errno));
							return -1;
						}
						in_handles_len++;
						match = 1;
					}
				}
			}
		}
		
		// Если совпали нужные опции с опциями плагина, выделяем на них место в массиве опций
		int ind = 0;
		if (match == 1) {
			plugin_in_opts[in_handles_len-1] = (struct option*) calloc(plugin_in_opts_len[in_handles_len-1], sizeof(struct option));
			if (!plugin_in_opts[in_handles_len-1]) {
				fprintf(stderr, "ERROR: get_in_handles: realloc() failed: %s\n", strerror(errno));
				return -1;
			}

			// Заполняем массив опций для данного плагина
			for (int j = 0; j < in_opts_len; j++) {
				for (size_t k = 0; k < pi.sup_opts_len; ++k) {			
					if (strcmp(pi.sup_opts[k].opt.name, in_opts[j].name) == 0) {
						plugin_in_opts[in_handles_len-1][ind++] = in_opts[j];
					}
				}
			}
		}
	}

	return 0;
}

void print_help() {
	printf("Доступные опции:\n");
	printf("-P dir\tКаталог с плагинами.\n");
	printf("-A\tОбъединение опций плагинов с помощью операции «И» (действует по умолчанию)..\n");
	printf("-O\tОбъединение опций плагинов с помощью операции «ИЛИ».\n");
	printf("-N\tИнвертирование условия поиска (после объединения опций плагинов с помощью -A или -O).\n");
	printf("-v\tВывод версии программы и информации о программе (ФИО исполнителя, номер группы, номер варианта лабораторной).\n");
	printf("-h\tВывод справки по опциям.\n");
	
	if (number_of_plugins > 0) {
		printf("\nДоступные длинные опции:\n");
		typedef int (*pgi_func_t)(struct plugin_info*);
		for (int i = 0; i < number_of_plugins; ++i) {
			void *func = dlsym(handles[i], "plugin_get_info");
			struct plugin_info pi = {0};
			pgi_func_t pgi_func = (pgi_func_t)func;            
			int ret = pgi_func(&pi);
			if (ret < 0) fprintf(stderr, "ERROR: print_help: plugin_get_info failed\n");
			
			printf("Опции из плагина №%d:\n", i+1);
			for (size_t j = 0; j < pi.sup_opts_len; ++j) {
				printf("%s\r", pi.sup_opts[j].opt.name);
				printf("\t\t\t%s\n", pi.sup_opts[j].opt_descr);
			}
		}
	}
}

void print_info() {
	printf("Программа %s. Версия: %s\n", g_program_name, g_version);
	printf("Автор программы: %s, группа %s\n", g_author, g_group);
	printf("Вариант лабораторной работы №1: %s\n", g_lab_variant);
}
       
