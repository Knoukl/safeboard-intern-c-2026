#include "mangen.h"

#include <stdio.h>
#include <string.h>

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

static int parse_args(int argc, const char* argv[], Options* options);
static void print_help(void);
static void print_version(void);

/* Разбирает аргументы и запускает запрошенное действие программы.
 * Возвращает код ошибки разбора аргументов или генерации манифеста.
 */
int main(int argc, const char* argv[]) {
    Options options = {0};
    int result = 0;
    result = parse_args(argc, argv, &options);

    if (!result) {
        if (options.show_help) {
            print_help();
        }
        if (options.show_version) {
            print_version();
        }
        if (!options.show_help && !options.show_version) {
            result = generate_manifest(&options);
        }
    }
    return result;
}

/* Проверяет аргументы командной строки и заполняет структуру Options.
 * Возвращает 0 при корректных аргументах и ненулевое значение при ошибке.
 */
static int parse_args(int argc, const char* argv[], Options* options) {
    int result = 0;
    for (int i = 1; i < argc && !result; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            options->show_help = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            options->show_version = 1;
        } else if (strcmp(argv[i], "-e") == 0) {
            if (options->exclude_name != NULL) {
                fprintf(stderr, "mangen: option '-e' may be used only once\n");
                result = 1;
            } else if (i == argc - 1) {
                fprintf(stderr, "mangen: option '-e' requires a pattern\n");
                result = 1;
            } else {
                i++;
                if (argv[i][0] == '\0') {
                    fprintf(
                        stderr,
                        "mangen: option '-e' requires a non-empty pattern\n");
                    result = 1;
                } else {
                    options->exclude_name = argv[i];
                }
            }
        } else if (options->directory_path == NULL) {
            options->directory_path = argv[i];
        } else {
            fprintf(stderr,
                    "mangen: only one directory path may be specified\n");
            result = 1;
        }
    }
    return result;
}

/* Печатает способ запуска программы и описание поддерживаемых опций.
 * Текст справки выводится в стандартный поток вывода.
 */
static void print_help(void) {
    printf(
        "Использование: ./mangen [DIR_PATH] [OPTIONS]\n"
        "\n"
        "Опции:\n"
        "  -h           показать справку и завершить работу\n"
        "  -v           показать версию и commit hash и завершить работу\n"
        "  -e PATTERN   исключить файлы и каталоги, имя которых целиком\n"
        "               совпадает с шаблоном (без учёта пути).\n"
        "               '.' означает любой один символ, '*' — любую\n"
        "               последовательность символов, в том числе "
        "пустую.\n"
        "               Подходящие каталоги и их содержимое не "
        "обходятся.\n");
}

/* Печатает hash коммита, встроенный в программу во время сборки. */
static void print_version(void) { printf("%s\n", GIT_COMMIT_HASH); }
