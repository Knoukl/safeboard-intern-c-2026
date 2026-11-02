#ifndef MANGEN_H
#define MANGEN_H

/* Параметры, полученные при разборе командной строки. */
typedef struct {
    const char* directory_path; /* NULL означает текущий каталог. */
    const char* exclude_name;   /* NULL означает отсутствие фильтра. */
    int show_help;              /* Запрошена опция -h. */
    int show_version;           /* Запрошена опция -v. */
} Options;

/* Создаёт манифест; возвращает 0 при успехе, иначе ненулевое значение. */
int generate_manifest(const Options* options);

/*
 * Вычисляет SHA-256 файла; возвращает 0 при успехе, иначе ненулевое значение.
 * output_hex должен указывать на буфер минимум из 65 символов.
 */
int sha256_file(const char* file_path, char output_hex[65]);

#endif /* MANGEN_H */
