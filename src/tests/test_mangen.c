#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../mangen.h"

#define EXAMPLE_DIR "src/tests/example"
#define TEST_TMP_DIR "build/tests"

typedef struct {
    int status;
    char output[32768];
    char errors[8192];
} CapturedOutput;

typedef int (*CapturedFunction)(void* argument);

typedef struct {
    int argc;
    char** argv;
} ProgramArguments;

int mangen_program_main(int argc, char* argv[]);

static void read_stream(FILE* stream, char* buffer, size_t buffer_size) {
    size_t bytes_read = 0;

    rewind(stream);
    if (buffer_size > 0) {
        bytes_read = fread(buffer, 1, buffer_size - 1, stream);
        buffer[bytes_read] = '\0';
    }
}

static CapturedOutput capture_call(CapturedFunction function, void* argument) {
    CapturedOutput captured = {0};
    FILE* output_file = tmpfile();
    FILE* error_file = tmpfile();
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);

    if (output_file == NULL || error_file == NULL || saved_stdout < 0 ||
        saved_stderr < 0) {
        captured.status = 1;
    } else {
        fflush(NULL);
        if (dup2(fileno(output_file), STDOUT_FILENO) < 0 ||
            dup2(fileno(error_file), STDERR_FILENO) < 0) {
            captured.status = 1;
        } else {
            captured.status = function(argument);
            fflush(NULL);
        }

        dup2(saved_stdout, STDOUT_FILENO);
        dup2(saved_stderr, STDERR_FILENO);
        read_stream(output_file, captured.output, sizeof(captured.output));
        read_stream(error_file, captured.errors, sizeof(captured.errors));
    }

    if (saved_stdout >= 0) {
        close(saved_stdout);
    }
    if (saved_stderr >= 0) {
        close(saved_stderr);
    }
    if (output_file != NULL) {
        fclose(output_file);
    }
    if (error_file != NULL) {
        fclose(error_file);
    }

    return captured;
}

static int call_program(void* argument) {
    ProgramArguments* program_arguments = argument;

    return mangen_program_main(program_arguments->argc,
                               program_arguments->argv);
}

static int call_generate_manifest(void* argument) {
    return generate_manifest(argument);
}

static CapturedOutput run_program(int argc, char* argv[]) {
    ProgramArguments arguments = {argc, argv};

    return capture_call(call_program, &arguments);
}

static void write_test_file(const char* path, const unsigned char* data,
                            size_t data_len) {
    FILE* file = fopen(path, "wb");

    ck_assert_ptr_nonnull(file);
    if (data_len > 0) {
        ck_assert_uint_eq(fwrite(data, 1, data_len, file), data_len);
    }
    ck_assert_int_eq(fclose(file), 0);
}

START_TEST(test_sha256_known_vectors) {
    char hash[65];
    const unsigned char abc[] = "abc";
    const unsigned char padding_boundary[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    unsigned char long_data[5000];

    write_test_file(TEST_TMP_DIR "/empty.bin", NULL, 0);
    ck_assert_int_eq(sha256_file(TEST_TMP_DIR "/empty.bin", hash), 0);
    ck_assert_str_eq(
        hash,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    write_test_file(TEST_TMP_DIR "/abc.bin", abc, sizeof(abc) - 1);
    ck_assert_int_eq(sha256_file(TEST_TMP_DIR "/abc.bin", hash), 0);
    ck_assert_str_eq(
        hash,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    write_test_file(TEST_TMP_DIR "/boundary.bin", padding_boundary,
                    sizeof(padding_boundary) - 1);
    ck_assert_int_eq(sha256_file(TEST_TMP_DIR "/boundary.bin", hash), 0);
    ck_assert_str_eq(
        hash,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    memset(long_data, 'a', sizeof(long_data));
    write_test_file(TEST_TMP_DIR "/long.bin", long_data, sizeof(long_data));
    ck_assert_int_eq(sha256_file(TEST_TMP_DIR "/long.bin", hash), 0);
    ck_assert_str_eq(
        hash,
        "c526c6222044dab5674de9c4ac7f4566ebb5e4d8bf9d8ea34c9cc8a7cc3c869c");
}
END_TEST

START_TEST(test_sha256_missing_file) {
    char hash[65];

    ck_assert_int_ne(sha256_file(TEST_TMP_DIR "/no-such-file", hash), 0);
}
END_TEST

START_TEST(test_manifest_walks_files_and_links) {
    char* arguments[] = {"mangen", EXAMPLE_DIR};
    CapturedOutput captured = run_program(2, arguments);

    ck_assert_int_eq(captured.status, 0);
    ck_assert_ptr_nonnull(strstr(captured.output, "root.txt : "));
    ck_assert_ptr_nonnull(strstr(captured.output, "root-link.txt : "));
    ck_assert_ptr_nonnull(strstr(captured.output, "level1/one.txt : "));
    ck_assert_ptr_nonnull(strstr(captured.output, "level1/level2/two.txt : "));
    ck_assert_ptr_null(strstr(captured.output, "level1-link : "));
    ck_assert_ptr_null(strstr(captured.output, "sample-pipe : "));
}
END_TEST

START_TEST(test_exclude_patterns) {
    char* wildcard_arguments[] = {"mangen", EXAMPLE_DIR, "-e", "skip*end"};
    char* single_character_arguments[] = {"mangen", EXAMPLE_DIR, "-e", "s.t"};
    char* directory_arguments[] = {"mangen", EXAMPLE_DIR, "-e", "skip-*"};
    CapturedOutput wildcard = run_program(4, wildcard_arguments);
    CapturedOutput single_character =
        run_program(4, single_character_arguments);
    CapturedOutput directory = run_program(4, directory_arguments);

    ck_assert_int_eq(wildcard.status, 0);
    ck_assert_ptr_null(strstr(wildcard.output, "skip-middle-end : "));
    ck_assert_ptr_nonnull(strstr(wildcard.output, "sXt : "));

    ck_assert_int_eq(single_character.status, 0);
    ck_assert_ptr_null(strstr(single_character.output, "sXt : "));
    ck_assert_ptr_nonnull(
        strstr(single_character.output, "skip-middle-end : "));

    ck_assert_int_eq(directory.status, 0);
    ck_assert_ptr_null(strstr(directory.output, "skip-middle-end : "));
    ck_assert_ptr_null(strstr(directory.output, "skip-dir/hidden.txt : "));
}
END_TEST

START_TEST(test_exclude_all_entries) {
    char* arguments[] = {"mangen", EXAMPLE_DIR, "-e", "*"};
    CapturedOutput captured = run_program(4, arguments);

    ck_assert_int_eq(captured.status, 0);
    ck_assert_str_eq(captured.output, "");
}
END_TEST

START_TEST(test_help_and_version_can_be_combined) {
    char* arguments[] = {"mangen", "-h", "-v"};
    CapturedOutput captured = run_program(3, arguments);

    ck_assert_int_eq(captured.status, 0);
    ck_assert_ptr_nonnull(strstr(captured.output, "Использование:"));
    ck_assert_ptr_nonnull(strstr(captured.output, "test-commit-hash"));
}
END_TEST

START_TEST(test_invalid_arguments) {
    char* missing_pattern[] = {"mangen", "-e"};
    char* empty_pattern[] = {"mangen", "-e", ""};
    char* repeated_pattern[] = {"mangen", "-e", "one", "-e", "two"};
    char* two_directories[] = {"mangen", "first", "second"};
    char* hyphen_directory[] = {"mangen", "-x"};
    CapturedOutput missing = run_program(2, missing_pattern);
    CapturedOutput empty = run_program(3, empty_pattern);
    CapturedOutput repeated = run_program(5, repeated_pattern);
    CapturedOutput multiple = run_program(3, two_directories);
    CapturedOutput hyphen = run_program(2, hyphen_directory);

    ck_assert_int_ne(missing.status, 0);
    ck_assert_ptr_nonnull(strstr(missing.errors, "requires a pattern"));
    ck_assert_int_ne(empty.status, 0);
    ck_assert_ptr_nonnull(strstr(empty.errors, "non-empty pattern"));
    ck_assert_int_ne(repeated.status, 0);
    ck_assert_ptr_nonnull(strstr(repeated.errors, "only once"));
    ck_assert_int_ne(multiple.status, 0);
    ck_assert_ptr_nonnull(strstr(multiple.errors, "only one directory"));
    ck_assert_int_ne(hyphen.status, 0);
    ck_assert_ptr_nonnull(strstr(hyphen.errors, "cannot open directory '-x'"));
}
END_TEST

START_TEST(test_manifest_errors_for_missing_paths) {
    char* arguments[] = {"mangen", "src/tests/example/no-such-directory"};
    CapturedOutput captured = run_program(2, arguments);

    ck_assert_int_ne(captured.status, 0);
    ck_assert_ptr_nonnull(strstr(captured.errors, "cannot open directory"));
}
END_TEST

START_TEST(test_manifest_reports_dangling_link) {
    const char* link_path = EXAMPLE_DIR "/broken-link";
    char* arguments[] = {"mangen", EXAMPLE_DIR};

    ck_assert_int_eq(symlink("missing-target", link_path), 0);
    CapturedOutput captured = run_program(2, arguments);
    ck_assert_int_eq(unlink(link_path), 0);

    ck_assert_int_ne(captured.status, 0);
    ck_assert_ptr_nonnull(
        strstr(captured.errors, "cannot inspect link target"));
}
END_TEST

START_TEST(test_default_directory_and_null_options) {
    char current_directory[4096];
    char* arguments[] = {"mangen"};
    CapturedOutput default_directory;
    CapturedOutput null_options;

    ck_assert_ptr_nonnull(getcwd(current_directory, sizeof(current_directory)));
    ck_assert_int_eq(chdir(EXAMPLE_DIR), 0);
    default_directory = run_program(1, arguments);
    ck_assert_int_eq(chdir(current_directory), 0);
    null_options = capture_call(call_generate_manifest, NULL);

    ck_assert_int_eq(default_directory.status, 0);
    ck_assert_ptr_nonnull(strstr(default_directory.output, "root.txt : "));
    ck_assert_int_ne(null_options.status, 0);
    ck_assert_ptr_nonnull(strstr(null_options.errors, "options are missing"));
}
END_TEST

int main(void) {
    Suite* suite = suite_create("mangen");
    TCase* test_case = tcase_create("core");
    SRunner* runner;
    int failed;

    tcase_add_test(test_case, test_sha256_known_vectors);
    tcase_add_test(test_case, test_sha256_missing_file);
    tcase_add_test(test_case, test_manifest_walks_files_and_links);
    tcase_add_test(test_case, test_exclude_patterns);
    tcase_add_test(test_case, test_exclude_all_entries);
    tcase_add_test(test_case, test_help_and_version_can_be_combined);
    tcase_add_test(test_case, test_invalid_arguments);
    tcase_add_test(test_case, test_manifest_errors_for_missing_paths);
    tcase_add_test(test_case, test_manifest_reports_dangling_link);
    tcase_add_test(test_case, test_default_directory_and_null_options);
    suite_add_tcase(suite, test_case);

    runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
