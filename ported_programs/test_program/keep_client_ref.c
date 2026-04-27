// keep_client_ref.c
// Keep a reference to test_program_main so the linker pulls Client_program
// into the final binary when the program is enabled.

extern int test_program_main(int argc, char *argv[]);

__attribute__((used)) static const void *test_program_main_ptr = (const void *)&test_program_main;
