#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "mangen.h"

static int walk_directory(const char* full_dir_path,
                          const char* relative_dir_path,
                          const Options* options);
static int scan_directory(DIR* directory, const char* full_dir_path,
                          const char* relative_dir_path,
                          const Options* options);
static int process_entry(const char* parent_full_path,
                         const char* parent_relative_path,
                         const char* entry_name, const Options* options);
static int process_path(const char* full_path, const char* relative_path,
                        const Options* options);
static int process_symbolic_link(const char* full_path,
                                 const char* relative_path);
static int hash_and_print_file(const char* full_path,
                               const char* relative_path);
static char* join_path(const char* parent_path, const char* child_name);
static int matches_pattern(const char* pattern, const char* text);
static int is_dot_entry(const char* entry_name);

/* Проверяет параметры и запускает обход выбранного каталога.
 * Если путь не задан, использует текущий каталог; возвращает статус обхода.
 */
int generate_manifest(const Options* options) {
    int red_flag = 0;

    if (options == NULL) {
        fprintf(stderr, "mangen: options are missing\n");
        red_flag = 1;
    } else {
        const char* full_dir_path = options->directory_path;
        const char* relative_dir_path = "";

        if (full_dir_path == NULL) {
            full_dir_path = ".";
        }

        red_flag = walk_directory(full_dir_path, relative_dir_path, options);
    }

    return red_flag;
}

/* Открывает каталог, передаёт его записи сканеру и закрывает поток.
 * Возвращает ненулевой статус при ошибке открытия, обхода или закрытия.
 */
static int walk_directory(const char* full_dir_path,
                          const char* relative_dir_path,
                          const Options* options) {
    int red_flag = 0;
    DIR* directory = opendir(full_dir_path);

    if (directory == NULL) {
        fprintf(stderr, "mangen: cannot open directory '%s': %s\n",
                full_dir_path, strerror(errno));
        red_flag = 1;
    } else {
        red_flag = scan_directory(directory, full_dir_path, relative_dir_path,
                                  options);

        if (closedir(directory) != 0) {
            fprintf(stderr, "mangen: cannot close directory '%s': %s\n",
                    full_dir_path, strerror(errno));
            red_flag = 1;
        }
    }

    return red_flag;
}

/* Читает записи открытого каталога и передаёт каждую в process_entry().
 * Обходит каталог до конца списка и запоминает ошибки обработки записей.
 */
static int scan_directory(DIR* directory, const char* full_dir_path,
                          const char* relative_dir_path,
                          const Options* options) {
    int red_flag = 0;
    int finished = 0;
    struct dirent* entry = NULL;

    while (finished == 0) {
        errno = 0;
        entry = readdir(directory);

        if (entry == NULL) {
            if (errno != 0) {
                fprintf(stderr, "mangen: cannot read directory '%s': %s\n",
                        full_dir_path, strerror(errno));
                red_flag = 1;
            }
            finished = 1;
        } else if (process_entry(full_dir_path, relative_dir_path,
                                 entry->d_name, options) != 0) {
            red_flag = 1;
        }
    }

    return red_flag;
}

/* Пропускает служебные и исключённые шаблоном записи каталога.
 * Для остальных строит полный и относительный пути, обрабатывает запись,
 * освобождает выделенную память и возвращает статус ошибки.
 */
static int process_entry(const char* parent_full_path,
                         const char* parent_relative_path,
                         const char* entry_name, const Options* options) {
    int red_flag = 0;
    char* entry_full_path = NULL;
    char* entry_relative_path = NULL;

    if (!is_dot_entry(entry_name) &&
        (options->exclude_name == NULL ||
         !matches_pattern(options->exclude_name, entry_name))) {
        entry_full_path = join_path(parent_full_path, entry_name);
        entry_relative_path = join_path(parent_relative_path, entry_name);

        if (entry_full_path == NULL || entry_relative_path == NULL) {
            fprintf(stderr, "mangen: cannot allocate a path for '%s'\n",
                    entry_name);
            red_flag = 1;
        } else {
            red_flag =
                process_path(entry_full_path, entry_relative_path, options);
        }
    }

    free(entry_full_path);
    free(entry_relative_path);

    return red_flag;
}

/* Определяет тип объекта с помощью lstat() и выбирает обработчик.
 * Каталоги обходятся, обычные файлы хэшируются, ссылки проверяются отдельно;
 * объекты неподдерживаемых типов пропускаются.
 */
static int process_path(const char* full_path, const char* relative_path,
                        const Options* options) {
    int red_flag = 0;
    struct stat entry_info;

    if (lstat(full_path, &entry_info) != 0) {
        fprintf(stderr, "mangen: cannot inspect '%s': %s\n", full_path,
                strerror(errno));
        red_flag = 1;
    } else if (S_ISDIR(entry_info.st_mode)) {
        red_flag = walk_directory(full_path, relative_path, options);
    } else if (S_ISREG(entry_info.st_mode)) {
        red_flag = hash_and_print_file(full_path, relative_path);
    } else if (S_ISLNK(entry_info.st_mode)) {
        red_flag = process_symbolic_link(full_path, relative_path);
    }

    return red_flag;
}

/* Проверяет цель символической ссылки с помощью stat().
 * Хэширует цель, если это обычный файл; каталоги и другие типы пропускает.
 */
static int process_symbolic_link(const char* full_path,
                                 const char* relative_path) {
    int red_flag = 0;
    struct stat target_info;

    if (stat(full_path, &target_info) != 0) {
        fprintf(stderr, "mangen: cannot inspect link target '%s': %s\n",
                full_path, strerror(errno));
        red_flag = 1;
    } else if (S_ISREG(target_info.st_mode)) {
        red_flag = hash_and_print_file(full_path, relative_path);
    }

    return red_flag;
}

/* Вычисляет SHA-256 файла и печатает его относительный путь с хэшем.
 * Возвращает ненулевое значение при ошибке хэширования или вывода.
 */
static int hash_and_print_file(const char* full_path,
                               const char* relative_path) {
    int red_flag = 0;
    char hash_hex[65];

    if (sha256_file(full_path, hash_hex) != 0) {
        fprintf(stderr, "mangen: cannot hash file '%s'\n", full_path);
        red_flag = 1;
    } else if (printf("%s : %s\n", relative_path, hash_hex) < 0) {
        fprintf(stderr, "mangen: cannot write manifest entry for '%s'\n",
                relative_path);
        red_flag = 1;
    }

    return red_flag;
}

/* Выделяет память и объединяет родительский путь с именем записи.
 * При необходимости вставляет '/', возвращает NULL при ошибке выделения.
 */
static char* join_path(const char* parent_path, const char* child_name) {
    size_t parent_len = strlen(parent_path);
    size_t child_len = strlen(child_name);
    size_t separator_len =
        parent_len > 0 && parent_path[parent_len - 1] != '/' ? 1 : 0;
    char* joined_path = malloc(parent_len + separator_len + child_len + 1);

    if (joined_path != NULL) {
        memcpy(joined_path, parent_path, parent_len);
        if (separator_len != 0) {
            joined_path[parent_len] = '/';
        }
        memcpy(joined_path + parent_len + separator_len, child_name,
               child_len + 1);
    }

    return joined_path;
}

/* Сравнивает имя целиком с шаблоном исключения без учёта пути.
 * Точка соответствует одному символу, звёздочка — любой их последовательности;
 * возвращает 1 при совпадении и 0, если имя не подошло.
 */
static int matches_pattern(const char* pattern, const char* text) {
    const char* pattern_cursor = pattern;
    const char* text_cursor = text;
    const char* last_star = NULL;
    const char* star_text = NULL;
    int finished = 0;
    int matched = 0;

    while (*text_cursor != '\0' && finished == 0) {
        if (*pattern_cursor == '.' || *pattern_cursor == *text_cursor) {
            pattern_cursor++;
            text_cursor++;
        } else if (*pattern_cursor == '*') {
            last_star = pattern_cursor;
            pattern_cursor++;
            star_text = text_cursor;
        } else if (last_star != NULL) {
            pattern_cursor = last_star + 1;
            star_text++;
            text_cursor = star_text;
        } else {
            finished = 1;
        }
    }

    while (*pattern_cursor == '*') {
        pattern_cursor++;
    }

    if (*pattern_cursor == '\0' && *text_cursor == '\0') {
        matched = 1;
    }

    return matched;
}

/* Проверяет, является ли имя записью текущего или родительского каталога.
 * Возвращает 1 для "." или "..", чтобы обходчик мог пропустить их.
 */
static int is_dot_entry(const char* entry_name) {
    int is_dot = 0;

    if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
        is_dot = 1;
    }

    return is_dot;
}
